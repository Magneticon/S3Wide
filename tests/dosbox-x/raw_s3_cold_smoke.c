/* Cold 8-bpp S3 mode trial from standard VGA text mode 3, using the actual
   recovery driver helpers. DOSBox-X's VGA register model is the target. */
#include <dos.h>
#include <conio.h>
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

typedef struct DOS_IO_CONTEXT { int fault_enable,arm_fault,injected; } DOS_IO_CONTEXT;
typedef struct VGA_STATE {
    U8 misc, seq[SEQ_COUNT], crtc[S3_MODE_CRTC_REG_COUNT];
    U8 cr_ext[CR_EXT_COUNT], gc[GC_COUNT], ac[AC_COUNT], dac_mask;
    U8 dac[DAC_COUNT];
} VGA_STATE;
static FILE *logfile;
static DOS_IO_CONTEXT dos_context;
static volatile U8 __far *aperture;
static U16 crtc_port;
static VGA_STATE full_before,full_after,full_rolled;
static int console_silent;

static void say(const char *s) { if(!console_silent)printf("%s\r\n",s); if(logfile)fprintf(logfile,"%s\n",s); }
static U8 S3_FAR S3_PASCAL read_crtc(void S3_FAR *ctx,U16 port,U8 idx)
{
    U8 v; DOS_IO_CONTEXT *c=(DOS_IO_CONTEXT *)ctx;
    outp(port,idx);v=(U8)inp(port+1);
    if(c->fault_enable&&c->arm_fault&&idx==0){c->arm_fault=0;c->injected=1;return (U8)(v^1);}
    return v;
}
static void S3_FAR S3_PASCAL write_crtc(void S3_FAR *ctx,U16 port,U8 idx,U8 v)
{
    DOS_IO_CONTEXT *c=(DOS_IO_CONTEXT *)ctx;outp(port,idx);outp(port+1,v);
    if(c->fault_enable&&idx==0x35)c->arm_fault=1;
}
static U8 S3_FAR S3_PASCAL read_seq(void S3_FAR *ctx,U8 idx)
{ (void)ctx;outp(0x3C4,idx);return (U8)inp(0x3C5); }
static void S3_FAR S3_PASCAL write_seq(void S3_FAR *ctx,U8 idx,U8 v)
{ (void)ctx;outp(0x3C4,idx);outp(0x3C5,v); }
static U8 S3_FAR S3_PASCAL read_misc(void S3_FAR *ctx)
{ (void)ctx;return (U8)inp(0x3CC); }
static void S3_FAR S3_PASCAL write_misc(void S3_FAR *ctx,U8 v)
{ (void)ctx;outp(0x3C2,v); }
static void S3_FAR S3_PASCAL write_dac(void S3_FAR *ctx,U16 p,U8 v)
{ (void)ctx;outp(p,v); }
static void index_read(U16 index_port,U16 data_port,U8 *values,U16 count)
{ U16 i;for(i=0;i<count;i++){outp(index_port,(U8)i);values[i]=(U8)inp(data_port);} }
static void index_write(U16 index_port,U16 data_port,const U8 *values,U16 count)
{ U16 i;for(i=0;i<count;i++){outp(index_port,(U8)i);outp(data_port,values[i]);} }
static void ac_read(U8 *values)
{ U8 i;for(i=0;i<AC_COUNT;i++){(void)inp(0x3DA);outp(0x3C0,i);values[i]=(U8)inp(0x3C1);} }
static void ac_write(U8 index,U8 value)
{ (void)inp(0x3DA);outp(0x3C0,index);outp(0x3C0,value); }
static void capture_full(VGA_STATE *s)
{
    U8 i,old_lock,old_seq08;U16 j;
    memset(s,0,sizeof(*s));
    s->misc=read_misc(0);
    old_seq08=read_seq(0,0x08);
    write_seq(0,0x08,0x06);
    for(i=0;i<5;i++)s->seq[i]=read_seq(0,i);
    s->seq[0x08]=old_seq08;s->seq[0x10]=read_seq(0,0x10);s->seq[0x11]=read_seq(0,0x11);
    s->seq[0x12]=read_seq(0,0x12);s->seq[0x13]=read_seq(0,0x13);s->seq[0x15]=read_seq(0,0x15);
    write_seq(0,0x08,old_seq08);
    index_read(crtc_port,crtc_port+1,s->crtc,S3_MODE_CRTC_REG_COUNT);
    old_lock=read_crtc(0,crtc_port,0x38);write_crtc(0,crtc_port,0x38,0x48);
    for(i=0;i<CR_EXT_COUNT;i++)s->cr_ext[i]=read_crtc(0,crtc_port,(U8)(0x30+i));
    write_crtc(0,crtc_port,0x38,old_lock);
    index_read(0x3CE,0x3CF,s->gc,GC_COUNT);ac_read(s->ac);
    s->dac_mask=(U8)inp(0x3C6);outp(0x3C7,0);
    for(j=0;j<DAC_COUNT;j++)s->dac[j]=(U8)inp(0x3C9);
    /* AC index writes clear the VGA display-enable state. Restore it after
       inspection so snapshots cannot blank the mode before screenshot. */
    (void)inp(0x3DA);outp(0x3C0,0x20);
}
static int same_full(const VGA_STATE *a,const VGA_STATE *b)
{
    return a->misc==b->misc&&!memcmp(a->seq,b->seq,SEQ_COUNT)&&
      !memcmp(a->crtc,b->crtc,S3_MODE_CRTC_REG_COUNT)&&
      !memcmp(a->cr_ext,b->cr_ext,CR_EXT_COUNT)&&!memcmp(a->gc,b->gc,GC_COUNT)&&
      !memcmp(a->ac,b->ac,AC_COUNT)&&a->dac_mask==b->dac_mask&&
      !memcmp(a->dac,b->dac,DAC_COUNT);
}
static void dump_bytes(const char *tag,const char *label,const U8 *v,U16 n)
{
    U16 i,j;char line[96],*p;
    for(i=0;i<n;i+=16){p=line;p+=sprintf(p,"%s %s[%03X]",tag,label,i);
      for(j=i;j<n&&j<i+16;j++)p+=sprintf(p," %02X",v[j]);say(line);}
}
static void dump_full(const char *tag,const VGA_STATE *s)
{
    char line[112];sprintf(line,"%s MISC=%02X DACMASK=%02X SEQ_CAPTURED=00-04,08,10-13,15",tag,s->misc,s->dac_mask);say(line);
    dump_bytes(tag,"SEQ",s->seq,SEQ_COUNT);dump_bytes(tag,"CRTC",s->crtc,S3_MODE_CRTC_REG_COUNT);
    dump_bytes(tag,"CRTC_EXT_30",s->cr_ext,CR_EXT_COUNT);dump_bytes(tag,"GC",s->gc,GC_COUNT);
    dump_bytes(tag,"AC",s->ac,AC_COUNT);dump_bytes(tag,"DAC",s->dac,DAC_COUNT);
}
static void text_mode(void)
{ union REGS in,out;in.x.ax=3;int86(0x10,&in,&out); }
static U8 pixel(U32 off)
{ U16 x=(U16)(off%1280UL),y=(U16)(off/1280UL);return (U8)((((x/160)&7)<<5)|(((y/90)&7)<<2)|((x/320)&3)); }
static int paint_readback(S3_REGISTER_IO *bankio)
{
    U32 base,chunk,done,run,j,bank;U16 x;U8 expected,lo,hi;
    for(base=0;base<921600UL;base+=65536UL){chunk=921600UL-base;if(chunk>65536UL)chunk=65536UL;bank=base/65536UL;
      if(!S3_Select64KBank(bankio,bank))return 0;outp(crtc_port,0x35);lo=(U8)inp(crtc_port+1);outp(crtc_port,0x51);hi=(U8)inp(crtc_port+1);
      if((lo&15)!=(bank&15)||((hi>>2)&3)!=((bank>>4)&3))return 0;
      x=(U16)(base%1280UL);done=0;while(done<chunk){run=1280U-x;{U32 edge=160U-(x%160U);if(run>edge)run=edge;}if(run>chunk-done)run=chunk-done;expected=pixel(base+done);for(j=0;j<run;j++)aperture[done+j]=expected;done+=run;x=(U16)(x+run);if(x==1280)x=0;}}
    for(base=0;base<921600UL;base+=65536UL){chunk=921600UL-base;if(chunk>65536UL)chunk=65536UL;bank=base/65536UL;
      if(!S3_Select64KBank(bankio,bank))return 0;x=(U16)(base%1280UL);done=0;while(done<chunk){run=1280U-x;{U32 edge=160U-(x%160U);if(run>edge)run=edge;}if(run>chunk-done)run=chunk-done;expected=pixel(base+done);for(j=0;j<run;j++)if(aperture[done+j]!=expected)return 0;done+=run;x=(U16)(x+run);if(x==1280)x=0;}}
    return 1;
}
static void setup_vga_8bpp(void)
{
    static U8 gc[9]={0,0,0,0,0,0x40,0x05,0x0F,0xFF};
    U8 i;write_seq(0,0x00,0x01);write_seq(0,0x01,0x01);write_seq(0,0x02,0x0F);
    write_seq(0,0x03,0x00);write_seq(0,0x04,0x0E);write_seq(0,0x00,0x03);
    index_write(0x3CE,0x3CF,gc,9);
    for(i=0;i<16;i++)ac_write(i,i);
    ac_write(0x10,0x41);ac_write(0x11,0x00);ac_write(0x12,0x0F);ac_write(0x13,0);ac_write(0x14,0);
    (void)inp(0x3DA);outp(0x3C0,0x20);
}
static void id_write32(U16 port,U32 v)
{ outp(port,(U8)v);outp(port,(U8)(v>>8));outp(port,(U8)(v>>16));outp(port,(U8)(v>>24)); }
static U32 id_read32(U16 port)
{ U32 v;v=(U32)inp(port);v|=(U32)inp(port)<<8;v|=(U32)inp(port)<<16;v|=(U32)inp(port)<<24;return v; }
static U32 screenshot_status(void)
{ id_write32(0x28,0x00C54010UL);return id_read32(0x29); }
static void request_screenshot(void)
{
    U32 status;int i;
    status=screenshot_status();
    if(status&0xC0000000UL){say("SCREENSHOT unavailable through DOSBox-X integration register");return;}
    id_write32(0x28,0x00C54010UL);id_write32(0x29,1UL);
    for(i=0;i<20;i++){delay(100);status=screenshot_status();if(!(status&1))break;}
    {char line[96];sprintf(line,"SCREENSHOT integration status=%08lX",status);say(line);}
}
int main(void)
{
    S3_MODE_IO modeio;S3_REGISTER_IO bankio;S3_MODE_SNAPSHOT mode;
    char line[140];int ok=1,programmed;
    logfile=fopen("S3COLD.TXT","wt");say("Cold raw S3 test from BIOS VGA text mode 3; no VBE calls");
    text_mode();console_silent=1;request_screenshot();crtc_port=(read_misc(0)&1)?0x3D4:0x3B4;aperture=(volatile U8 __far *)MK_FP(0xA000,0);
    modeio.context=&dos_context;modeio.crtc_index_port=crtc_port;modeio.read_seq=read_seq;modeio.write_seq=write_seq;modeio.read_crtc=read_crtc;modeio.write_crtc=write_crtc;modeio.read_misc=read_misc;modeio.write_misc=write_misc;
    bankio.context=&dos_context;bankio.crtc_index_port=crtc_port;bankio.read_crtc=read_crtc;bankio.write_crtc=write_crtc;bankio.write_dac=write_dac;
    capture_full(&full_before);dump_full("TEXT_BEFORE",&full_before);
    if(!S3_CaptureModeSnapshot(&modeio,&mode)){say("FAIL candidate baseline capture");ok=0;goto done;}
    dos_context.fault_enable=1;dos_context.injected=0;dos_context.arm_fault=0;
    programmed=S3_Program720p8bppCandidate(&modeio,&mode);
    dos_context.fault_enable=0;
    capture_full(&full_rolled);dump_full("AFTER_FORCED_ROLLBACK",&full_rolled);
    if(programmed||!dos_context.injected||!same_full(&full_before,&full_rolled)){say("FAIL forced mismatch tracked text-state rollback");ok=0;}else say("PASS injected CR00 mismatch restored tracked text-mode VGA/S3/DAC register snapshot");
    dos_context.arm_fault=0;dos_context.injected=0;
    programmed=S3_Program720p8bppCandidate(&modeio,&mode);
    if(!programmed){say("FAIL candidate could not program from cold VGA text state");ok=0;goto done;}
    say("PASS driver timing/PLL candidate from cold VGA text state");
    setup_vga_8bpp();
    if(!S3_ProgramRGB332Palette6(&bankio)){say("FAIL driver DAC palette setup");ok=0;goto done;}
    capture_full(&full_after);dump_full("RAW_COLD_8BPP",&full_after);
    sprintf(line,"CLOCK model: MISC[3:2]=%u SR12=%02X SR13=%02X CR42 readback=%02X; pinned DOSBox-X GetClock uses MISC clock 3 PLL and does not consume CR42 low nibble",(full_after.misc>>2)&3,full_after.seq[0x12],full_after.seq[0x13],full_after.cr_ext[0x42-0x30]);say(line);
    if((full_after.misc&0xCC)!=0xCC||full_after.crtc[0]!=0xCB||full_after.crtc[1]!=0x9F||full_after.crtc[2]!=0xA0||full_after.crtc[0x09]!=0x60||full_after.crtc[0x13]!=0xA0||full_after.crtc[0x16]!=0xE7||full_after.crtc[0x17]!=0xA3||full_after.cr_ext[0x3A-0x30]!=0x15||full_after.cr_ext[0x50-0x30]!=0xC0||(full_after.cr_ext[0x51-0x30]&0x30)!=0||full_after.gc[5]!=0x40||full_after.gc[6]!=0x05||full_after.ac[0x10]!=0x41||full_after.seq[4]!=0x0E||full_after.seq[0x12]!=0x23||full_after.seq[0x13]!=0x32){say("FAIL cold raw register image expected fields");ok=0;}
    else say("PASS cold 8bpp CRTC pitch, GC/SEQ/AC packed mode, S3 width, and PLL fields");
    if(!paint_readback(&bankio)){say("FAIL cold raw S3 bank mapping or framebuffer readback");ok=0;}
    else say("PASS cold raw banks 0..14 selected via driver, CR35/CR51 verified, 921600-byte pattern readback");
done:
    say(ok?"RESULT PASS cold raw S3 text-start gate; register/memory proof only":"RESULT FAIL cold raw S3 text-start gate");
    if(ok)request_screenshot();
    capture_full(&full_after);
    dump_full("COLD_FINAL_STATE",&full_after);
    if(ok){say("SCREENSHOT delayed confirmation after AC display-enable restore");delay(500);request_screenshot();}
    if(logfile)fclose(logfile);
    return ok?0:1;
}
