/* Exercise the recovery project's real raw S3 routines under DOSBox-X.
   VBE is used only to establish/query the reference mode and 64 KiB window. */
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <string.h>
#include "s3_hw.h"

typedef unsigned char U8;
typedef unsigned short U16;
typedef unsigned long U32;
#define CR_EXT_COUNT (0x6A-0x30+1)
#define SEQ_COUNT 0x20
#define GC_COUNT 9
#define AC_COUNT 21
#define DAC_COUNT 768

typedef struct VBE_MODE_PREFIX {
    U16 attributes; U8 win_a_attributes, win_b_attributes;
    U16 win_granularity_kb, win_size_kb, win_a_segment, win_b_segment;
    U32 win_function; U16 bytes_per_scanline, x_resolution, y_resolution;
    U8 x_char_size, y_char_size, planes, bits_per_pixel, banks, memory_model;
} VBE_MODE_PREFIX;

typedef struct DOS_IO_CONTEXT { int fault_enable; int arm_fault; int injected; } DOS_IO_CONTEXT;
typedef struct VGA_STATE { U8 misc,seq[SEQ_COUNT],crtc[S3_MODE_CRTC_REG_COUNT],cr_ext[CR_EXT_COUNT],gc[GC_COUNT],ac[AC_COUNT],dac_mask,dac[DAC_COUNT]; } VGA_STATE;
static U8 vbe_mode_info[256];
static FILE *logfile;
static U32 granularity, window_bytes, frame_bytes;
static U16 active_vbe_bank = 0xFFFFU;
static volatile U8 __far *aperture;
static DOS_IO_CONTEXT dos_context;
static VGA_STATE vbe_full,raw_full;
static int console_silent;

static void say(const char *s) { if(!console_silent)printf("%s\r\n", s); if (logfile) fprintf(logfile, "%s\n", s); }
static int vbe(U16 ax, U16 bx, U16 cx, U16 dx, U16 es, U16 di, union REGS *out)
{
    union REGS in; struct SREGS seg;
    in.x.ax=ax; in.x.bx=bx; in.x.cx=cx; in.x.dx=dx; in.x.si=0; in.x.di=di;
    segread(&seg); seg.es=es; int86x(0x10,&in,out,&seg); return out->x.ax==0x004FU;
}
static U8 S3_FAR S3_PASCAL read_crtc(void S3_FAR *ctx,U16 port,U8 idx)
{
    U8 v; DOS_IO_CONTEXT *c=(DOS_IO_CONTEXT *)ctx;
    outp(port,idx); v=(U8)inp(port+1);
    if (c->arm_fault && idx==0x00) { c->arm_fault=0; c->injected=1; return (U8)(v^1); }
    return v;
}
static void S3_FAR S3_PASCAL write_crtc(void S3_FAR *ctx,U16 port,U8 idx,U8 v)
{
    DOS_IO_CONTEXT *c=(DOS_IO_CONTEXT *)ctx;
    outp(port,idx); outp(port+1,v);
    /* Candidate's CR35 timing-lock write precedes its CRTC readback. */
    if (c->fault_enable && idx==0x35 && !c->arm_fault && !c->injected) c->arm_fault=1;
}
static U8 S3_FAR S3_PASCAL read_seq(void S3_FAR *ctx,U8 idx)
{ (void)ctx; outp(0x3C4,idx); return (U8)inp(0x3C5); }
static void S3_FAR S3_PASCAL write_seq(void S3_FAR *ctx,U8 idx,U8 v)
{ (void)ctx; outp(0x3C4,idx); outp(0x3C5,v); }
static U8 S3_FAR S3_PASCAL read_misc(void S3_FAR *ctx)
{ (void)ctx; return (U8)inp(0x3CC); }
static void S3_FAR S3_PASCAL write_misc(void S3_FAR *ctx,U8 v)
{ (void)ctx; outp(0x3C2,v); }
static void S3_FAR S3_PASCAL write_dac(void S3_FAR *ctx,U16 p,U8 v)
{ (void)ctx; outp(p,v); }
static void index_read(U16 ip,U16 dp,U8 *v,U16 n)
{ U16 i;for(i=0;i<n;i++){outp(ip,(U8)i);v[i]=(U8)inp(dp);} }
static void ac_read(U8 *v)
{ U8 i;for(i=0;i<AC_COUNT;i++){(void)inp(0x3DA);outp(0x3C0,i);v[i]=(U8)inp(0x3C1);} }
static void capture_full(U16 port,VGA_STATE *s)
{
    U8 i,lock,seq08;U16 j;memset(s,0,sizeof(*s));s->misc=read_misc(0);
    seq08=read_seq(0,8);write_seq(0,8,6);for(i=0;i<5;i++)s->seq[i]=read_seq(0,i);
    s->seq[8]=seq08;s->seq[0x10]=read_seq(0,0x10);s->seq[0x11]=read_seq(0,0x11);
    s->seq[0x12]=read_seq(0,0x12);s->seq[0x13]=read_seq(0,0x13);s->seq[0x15]=read_seq(0,0x15);write_seq(0,8,seq08);
    index_read(port,port+1,s->crtc,S3_MODE_CRTC_REG_COUNT);lock=read_crtc(0,port,0x38);write_crtc(0,port,0x38,0x48);
    for(i=0;i<CR_EXT_COUNT;i++)s->cr_ext[i]=read_crtc(0,port,(U8)(0x30+i));write_crtc(0,port,0x38,lock);
    index_read(0x3CE,0x3CF,s->gc,GC_COUNT);ac_read(s->ac);s->dac_mask=(U8)inp(0x3C6);outp(0x3C7,0);
    for(j=0;j<DAC_COUNT;j++)s->dac[j]=(U8)inp(0x3C9);
    (void)inp(0x3DA);outp(0x3C0,0x20);
}
static void dump_array(const char *tag,const char *name,const U8 *v,U16 n)
{ U16 i,j;char b[96],*p;for(i=0;i<n;i+=16){p=b;p+=sprintf(p,"%s %s[%03X]",tag,name,i);for(j=i;j<n&&j<i+16;j++)p+=sprintf(p," %02X",v[j]);say(b);} }
static void dump_full(const char *tag,const VGA_STATE *s)
{ char b[100];sprintf(b,"%s MISC=%02X DACMASK=%02X SEQ_CAPTURED=00-04,08,10-13,15",tag,s->misc,s->dac_mask);say(b);dump_array(tag,"SEQ",s->seq,SEQ_COUNT);dump_array(tag,"CRTC",s->crtc,S3_MODE_CRTC_REG_COUNT);dump_array(tag,"EXT30",s->cr_ext,CR_EXT_COUNT);dump_array(tag,"GC",s->gc,GC_COUNT);dump_array(tag,"AC",s->ac,AC_COUNT);dump_array(tag,"DAC",s->dac,DAC_COUNT); }
static void id_write32(U16 p,U32 v){outp(p,(U8)v);outp(p,(U8)(v>>8));outp(p,(U8)(v>>16));outp(p,(U8)(v>>24));}
static U32 id_read32(U16 p){U32 v;v=(U32)inp(p);v|=(U32)inp(p)<<8;v|=(U32)inp(p)<<16;v|=(U32)inp(p)<<24;return v;}
static void request_screenshot(void)
{ U32 st;int i;char b[64];id_write32(0x28,0x00C54010UL);st=id_read32(0x29);if(st&0xC0000000UL){say("SCREENSHOT unavailable");return;}id_write32(0x28,0x00C54010UL);id_write32(0x29,1);for(i=0;i<20;i++){delay(100);id_write32(0x28,0x00C54010UL);st=id_read32(0x29);if(!(st&1))break;}sprintf(b,"SCREENSHOT integration status=%08lX",st);say(b);}
static U16 crtc_port(void)
{ return (read_misc(0) & 1) ? 0x3D4 : 0x3B4; }
static int vbe_set_bank(U32 byte_offset)
{
    U32 bank=byte_offset/granularity; union REGS out;
    if (bank>0xFFFFUL || (U16)bank==active_vbe_bank) return bank<=0xFFFFUL;
    if (!vbe(0x4F05,0,0,(U16)bank,0,0,&out)) return 0;
    active_vbe_bank=(U16)bank; return 1;
}
static U8 pixel(U32 o)
{
    U16 x=(U16)(o%1280UL), y=(U16)(o/1280UL);
    return (U8)((((x/160)&7)<<5)|(((y/90)&7)<<2)|((x/320)&3));
}
static void dump(const char *tag,const S3_MODE_SNAPSHOT *s)
{
    U8 i; char b[96];
    sprintf(b,"%s misc=%02X SR01=%02X SR08=%02X SR12=%02X SR13=%02X SR15=%02X CR38=%02X",tag,
        s->misc_output,s->seq01,s->seq08,s->seq12,s->seq13,s->seq15,s->crtc38); say(b);
    for (i=0;i<S3_MODE_CRTC_REG_COUNT;i+=8) {
        U8 j; char *p=b; p+=sprintf(p,"%s CR%02X:",tag,i);
        for(j=i;j<S3_MODE_CRTC_REG_COUNT && j<i+8;j++) p+=sprintf(p," %02X",s->crtc[j]);
        say(b);
    }
    sprintf(b,"%s CR42=%02X CR43=%02X CR50=%02X CR51=%02X CR5D=%02X CR5E=%02X CR67=%02X CR31=%02X CR58=%02X CR35=%02X",
        tag,s->extended[0],s->extended[1],s->extended[2],s->extended[3],s->extended[4],s->extended[5],s->extended[6],s->extended[7],s->extended[8],s->extended[9]); say(b);
}
static int same(const S3_MODE_SNAPSHOT *a,const S3_MODE_SNAPSHOT *b)
{
    U8 i;
    if(a->misc_output!=b->misc_output||a->seq01!=b->seq01||a->seq08!=b->seq08||a->seq12!=b->seq12||a->seq13!=b->seq13||a->seq15!=b->seq15||a->crtc38!=b->crtc38||a->crtc3a!=b->crtc3a)return 0;
    for(i=0;i<S3_MODE_CRTC_REG_COUNT;i++)if(a->crtc[i]!=b->crtc[i])return 0;
    for(i=0;i<S3_MODE_EXT_REG_COUNT;i++)if(a->extended[i]!=b->extended[i])return 0;
    return 1;
}
static int paint_read_vbe(void)
{
    U32 base,chunk,done,run,j; U16 x; U8 expected;
    for(base=0;base<frame_bytes;base+=granularity){
        chunk=frame_bytes-base; if(chunk>granularity)chunk=granularity;
        if(!vbe_set_bank(base))return 0;
        x=(U16)(base%1280UL); done=0;
        while(done<chunk){
            run=1280U-x; if(x<1280U){U32 edge=160U-(x%160U);if(run>edge)run=edge;}
            if(run>chunk-done)run=chunk-done; expected=pixel(base+done);
            for(j=0;j<run;j++)aperture[done+j]=expected;
            done+=run;x=(U16)(x+run);if(x==1280U)x=0;
        }
    }
    for(base=0;base<frame_bytes;base+=granularity){
        chunk=frame_bytes-base; if(chunk>granularity)chunk=granularity;
        if(!vbe_set_bank(base))return 0;
        x=(U16)(base%1280UL); done=0;
        while(done<chunk){
            run=1280U-x;if(x<1280U){U32 edge=160U-(x%160U);if(run>edge)run=edge;}
            if(run>chunk-done)run=chunk-done;expected=pixel(base+done);
            for(j=0;j<run;j++)if(aperture[done+j]!=expected)return 0;
            done+=run;x=(U16)(x+run);if(x==1280U)x=0;
        }
    }
    return 1;
}
static int paint_read_raw(S3_REGISTER_IO *bankio)
{
    U32 base,chunk,done,run,j,bank; U16 x; U8 lo,hi,expected;
    for(base=0;base<frame_bytes;base+=granularity){
        chunk=frame_bytes-base; if(chunk>granularity)chunk=granularity; bank=base/granularity;
        if(!S3_Select64KBank(bankio,bank))return 0;
        outp(bankio->crtc_index_port,0x35); lo=(U8)inp(bankio->crtc_index_port+1);
        outp(bankio->crtc_index_port,0x51); hi=(U8)inp(bankio->crtc_index_port+1);
        if((lo&15)!=(bank&15)||((hi>>2)&3)!=((bank>>4)&3))return 0;
        x=(U16)(base%1280UL);done=0;
        while(done<chunk){run=1280U-x;if(x<1280U){U32 edge=160U-(x%160U);if(run>edge)run=edge;}if(run>chunk-done)run=chunk-done;expected=pixel(base+done);for(j=0;j<run;j++)aperture[done+j]=expected;done+=run;x=(U16)(x+run);if(x==1280U)x=0;}
    }
    for(base=0;base<frame_bytes;base+=granularity){
        chunk=frame_bytes-base; if(chunk>granularity)chunk=granularity; bank=base/granularity;
        if(!S3_Select64KBank(bankio,bank))return 0;
        x=(U16)(base%1280UL);done=0;
        while(done<chunk){run=1280U-x;if(x<1280U){U32 edge=160U-(x%160U);if(run>edge)run=edge;}if(run>chunk-done)run=chunk-done;expected=pixel(base+done);for(j=0;j<run;j++)if(aperture[done+j]!=expected)return 0;done+=run;x=(U16)(x+run);if(x==1280U)x=0;}
    }
    return 1;
}
int main(void)
{
    VBE_MODE_PREFIX __far *info=(VBE_MODE_PREFIX __far *)vbe_mode_info;
    union REGS out; S3_MODE_IO modeio; S3_REGISTER_IO bankio;
    S3_MODE_SNAPSHOT vberef,raw,rollback,after; U16 port; char b[150]; U8 old35,old51,new35,new51;
    int ok=1,raw_ok,rolled;
    logfile=fopen("S3RAW.TXT","wt");
    say("DOSBox-X S3 raw hardware-helper proof; no Windows guest / visible scanout claim");
    if(!vbe(0x4F01,0,0x023E,0,FP_SEG(vbe_mode_info),FP_OFF(vbe_mode_info),&out)||
       info->x_resolution!=1280||info->y_resolution!=720||info->bits_per_pixel!=8||info->bytes_per_scanline!=1280||info->win_granularity_kb!=64||info->win_size_kb!=64){say("FAIL VBE reference mode geometry/window");ok=0;goto done;}
    if(!vbe(0x4F02,0x023E,0,0,0,0,&out)){say("FAIL set VBE reference mode");ok=0;goto done;}
    console_silent=1;
    granularity=(U32)info->win_granularity_kb*1024UL; window_bytes=(U32)info->win_size_kb*1024UL; frame_bytes=1280UL*720UL;
    aperture=(volatile U8 __far *)MK_FP(info->win_a_segment,0); active_vbe_bank=0xFFFFU;
    sprintf(b,"VBE_REF mode=023E 1280x720x8 pitch=%u frame=%lu window=%lu granularity=%lu banks=0..14",info->bytes_per_scanline,frame_bytes,window_bytes,granularity);say(b);
    if(!paint_read_vbe()){say("FAIL VBE reference full-frame pattern/readback");ok=0;goto done;}
    say("PASS VBE reference 921600-byte RGB332 pattern write/readback via BIOS banks");
    port=crtc_port(); modeio.context=&dos_context;modeio.crtc_index_port=port;modeio.read_seq=read_seq;modeio.write_seq=write_seq;modeio.read_crtc=read_crtc;modeio.write_crtc=write_crtc;modeio.read_misc=read_misc;modeio.write_misc=write_misc;
    bankio.context=&dos_context;bankio.crtc_index_port=port;bankio.read_crtc=read_crtc;bankio.write_crtc=write_crtc;bankio.write_dac=write_dac;
    if(!S3_CaptureModeSnapshot(&modeio,&vberef)){say("FAIL capture VBE reference S3 state");ok=0;goto done;}
    if(!S3_ProgramRGB332Palette6(&bankio)){say("FAIL driver RGB332 DAC palette routine");ok=0;goto done;}
    say("PASS driver RGB332 palette routine called against real DAC ports");
    dump("VBE_REF_REGS",&vberef);
    capture_full(port,&vbe_full);dump_full("VBE_BASELINE_FULL",&vbe_full);request_screenshot();
    sprintf(b,"EXPECTED timing CR00..05=CB 9F A0 [computed] A7 [computed], CR06/07=E8/F0 CR10/12=D1/CF; pitch: VBE CR13=%02X CR51[5:4]=%X0 => %u bytes; raw CR13=A0 CR51[5:4]=00; PLL SR12/13=23/32",vberef.crtc[0x13],(vberef.extended[3]>>4)&3,(U16)(((vberef.extended[3]>>4)&3)*256+vberef.crtc[0x13])*8U);say(b);
    dos_context.arm_fault=0;dos_context.injected=0;
    raw_ok=S3_Program720p8bppCandidate(&modeio,&raw);
    if(!raw_ok){say("FAIL driver candidate raw timing readback (routine restored its captured state)");ok=0;}
    else {
        say("PASS driver candidate readback: raw timing/PLL registers accepted");
        if(!S3_CaptureModeSnapshot(&modeio,&raw)){say("FAIL capture programmed candidate");ok=0;goto rollback_test;}
        dump("RAW_MODE_REGS",&raw);
        if(raw.crtc[0]!=0xCB||raw.crtc[1]!=0x9F||raw.crtc[2]!=0xA0||raw.crtc[3]!=(U8)((vberef.crtc[3]&0xE0)|0x10)||raw.crtc[4]!=0xA7||raw.crtc[5]!=(U8)((vberef.crtc[5]&0x60)|0x18)||raw.crtc[6]!=0xE8||raw.crtc[7]!=0xF0||raw.crtc[9]!=(U8)(vberef.crtc[9]&0xE0)||raw.crtc3a!=(U8)(vberef.crtc3a|0x10)||raw.crtc[0x10]!=0xD1||raw.crtc[0x11]!=(U8)((vberef.crtc[0x11]&0xF0)|0x04)||raw.crtc[0x12]!=0xCF||raw.crtc[0x13]!=0xA0||raw.crtc[0x14]!=(U8)(vberef.crtc[0x14]|0x40)||raw.crtc[0x15]!=0xD0||raw.crtc[0x16]!=0xE7||raw.crtc[0x17]!=(U8)(0xA3|(vberef.crtc[0x17]&0x40))||raw.crtc[0x18]!=0xFF||(raw.extended[2]&0xC0)!=0xC0||((raw.extended[3]&0x30)!=(vberef.extended[3]&0x30))||raw.extended[1]!=0||raw.extended[4]!=(U8)(vberef.extended[4]&~0x17)||raw.extended[5]!=(U8)(vberef.extended[5]&~0x57)||raw.extended[6]!=0||raw.seq12!=0x23||raw.seq13!=0x32){sprintf(b,"FAIL candidate check detail CR09=%02X/%02X CR3A=%02X/%02X CR17=%02X CR51=%02X/%02X CR5D=%02X/%02X CR5E=%02X/%02X SR12/13=%02X/%02X",raw.crtc[9],(U8)(vberef.crtc[9]&0xE0),raw.crtc3a,(U8)(vberef.crtc3a|0x10),raw.crtc[0x17],raw.extended[3],vberef.extended[3],raw.extended[4],vberef.extended[4],raw.extended[5],vberef.extended[5],raw.seq12,raw.seq13);say(b);ok=0;}
        else if((U16)((((raw.extended[3]>>4)&3)*256+raw.crtc[0x13])*8U)!=1280U){say("FAIL raw pitch decoded from CR13/CR51 differs from 1280-byte VBE reference");ok=0;}
        else say("PASS raw timing/PLL and 1280-byte pitch match the BIOS VBE reference (CR13=A0, CR51[5:4]=00)");
        outp(port,0x35);old35=(U8)inp(port+1);outp(port,0x51);old51=(U8)inp(port+1);
        if(!S3_Select64KBank(&bankio,64)){outp(port,0x35);new35=(U8)inp(port+1);outp(port,0x51);new51=(U8)inp(port+1);if(old35==new35&&old51==new51)say("PASS bank 64 rejected with CR35/CR51 unchanged");else{say("FAIL bank 64 rejection changed bank registers");ok=0;}}
        else {say("FAIL bank 64 unexpectedly accepted");ok=0;}
        if(!paint_read_raw(&bankio)){say("FAIL raw driver bank 0..14 mapping or full-frame pattern readback");ok=0;}
        else {say("PASS driver S3_Select64KBank: CR35/CR51 mapping banks 0..14; 921600-byte pattern write/readback");capture_full(port,&raw_full);dump_full("RAW_VBE_SEEDED_FULL",&raw_full);request_screenshot();say("SCREENSHOT delayed confirmation after AC display-enable restore");delay(500);request_screenshot();}
    }
rollback_test:
    if(!S3_CaptureModeSnapshot(&modeio,&rollback)){say("FAIL capture rollback baseline");ok=0;goto done;}
    dos_context.fault_enable=1;dos_context.arm_fault=0;dos_context.injected=0;
    /* Force one CR00 candidate readback mismatch; every actual port write and
       the production rollback writer still execute against DOSBox-X. */
    raw_ok=S3_Program720p8bppCandidate(&modeio,&after);
    if(!dos_context.injected){say("FAIL rollback injection did not reach candidate readback");ok=0;}
    else if(raw_ok){say("FAIL injected mismatch did not fail candidate");ok=0;}
    else if(!S3_CaptureModeSnapshot(&modeio,&after)){say("FAIL capture after forced rollback");ok=0;}
    else {
        dump("AFTER_FORCED_ROLLBACK",&after);
        rolled=same(&rollback,&after);
        sprintf(b,"FORCED_READBACK_MISMATCH injected=1 exact_snapshot_rollback=%s",rolled?"PASS":"FAIL");say(b);
        if(!rolled)ok=0;
    }
done:
    if(ok)say("RESULT PASS bounded raw S3 helper gate; no scanout/GDI assertion"); else say("RESULT FAIL bounded raw S3 helper gate");
    if(logfile)fclose(logfile);
    return ok?0:1;
}
