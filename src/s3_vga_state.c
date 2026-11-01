#include "s3_vga_state.h"
#include <string.h>

#define SEQ_UNLOCK 0x06
#define CRTC_UNLOCK 0x48
#define DAC_WRITE_INDEX 0x03C8
#define DAC_READ_INDEX 0x03C7
#define DAC_DATA 0x03C9
typedef char s3_vga_state_fits_bridge[(sizeof(S3_VGA_STATE) <= S3_BRIDGE_MODE_STATE_BYTES) ? 1 : -1];

static int valid_io(const S3_VGA_STATE_IO S3_FAR *io)
{
    return io != 0 && (io->crtc_port == 0x03B4 || io->crtc_port == 0x03D4) &&
        io->read_seq && io->write_seq && io->read_crtc && io->write_crtc &&
        io->read_gc && io->write_gc && io->read_ac && io->write_ac &&
        io->read_misc && io->write_misc && io->read_dac && io->read_dac_mask && io->write_dac &&
        io->enable_ac_display;
}

int S3_VgaCaptureState(const S3_VGA_STATE_IO S3_FAR *io, S3_VGA_STATE S3_FAR *s)
{
    S3_U8 i, old_seq08, old_lock;
    S3_U16 j;
    if (!valid_io(io) || !s) return 0;
    memset(s, 0, sizeof(*s));
    s->misc = io->read_misc(io->context);
    old_seq08 = io->read_seq(io->context, 8);
    io->write_seq(io->context, 8, SEQ_UNLOCK);
    for (i=0; i<5; ++i) s->seq[i] = io->read_seq(io->context, i);
    s->seq[5] = old_seq08;
    s->seq10=io->read_seq(io->context,0x10); s->seq11=io->read_seq(io->context,0x11);
    s->seq12=io->read_seq(io->context,0x12); s->seq13=io->read_seq(io->context,0x13);
    s->seq15=io->read_seq(io->context,0x15);
    io->write_seq(io->context,8,old_seq08);
    for(i=0;i<S3_VGA_CRTC_BYTES;++i) s->crtc[i]=io->read_crtc(io->context,i);
    old_lock=io->read_crtc(io->context,0x38);
    io->write_crtc(io->context,0x38,CRTC_UNLOCK);
    for(i=0;i<S3_VGA_EXT_BYTES;++i) s->crtc_ext[i]=io->read_crtc(io->context,(S3_U8)(0x30+i));
    io->write_crtc(io->context,0x38,old_lock);
    for(i=0;i<S3_VGA_GC_BYTES;++i) s->gc[i]=io->read_gc(io->context,i);
    for(i=0;i<S3_VGA_AC_BYTES;++i) s->ac[i]=io->read_ac(io->context,i);
    s->dac_mask=io->read_dac_mask(io->context);
    io->write_dac(io->context,DAC_READ_INDEX,0);
    for(j=0;j<S3_VGA_DAC_BYTES;++j) s->dac[j]=io->read_dac(io->context);
    io->enable_ac_display(io->context);
    s->valid=1;
    return 1;
}

int S3_VgaRestoreState(const S3_VGA_STATE_IO S3_FAR *io, const S3_VGA_STATE S3_FAR *s)
{
    S3_U8 i, blank;
    S3_U16 j;
    S3_VGA_STATE observed;
    if (!valid_io(io) || !s || s->valid != 1) return 0;
    blank=(S3_U8)(io->read_seq(io->context,1)|0x20);
    io->write_seq(io->context,1,blank);
    io->write_seq(io->context,0,0x01); /* hold sequencer reset while restoring */
    io->write_seq(io->context,8,SEQ_UNLOCK);
    io->write_crtc(io->context,0x38,CRTC_UNLOCK);
    for(i=0;i<S3_VGA_CRTC_BYTES;++i) {
        if(i==0x11) io->write_crtc(io->context,0x11,(S3_U8)(s->crtc[i]&0x7f));
        else io->write_crtc(io->context,i,s->crtc[i]);
    }
    for(i=0;i<S3_VGA_EXT_BYTES;++i) {
        if(i!=8) io->write_crtc(io->context,(S3_U8)(0x30+i),s->crtc_ext[i]);
    }
    io->write_crtc(io->context,0x38,s->crtc_ext[8]); /* captured CR38 */
    for(i=1;i<5;++i) io->write_seq(io->context,i,s->seq[i]);
    io->write_seq(io->context,0x10,s->seq10); io->write_seq(io->context,0x11,s->seq11);
    io->write_seq(io->context,0x12,s->seq12); io->write_seq(io->context,0x13,s->seq13);
    io->write_seq(io->context,0x15,s->seq15);
    io->write_misc(io->context,s->misc);
    io->write_seq(io->context,8,s->seq[5]);
    for(i=0;i<S3_VGA_GC_BYTES;++i) io->write_gc(io->context,i,s->gc[i]);
    for(i=0;i<S3_VGA_AC_BYTES;++i) io->write_ac(io->context,i,s->ac[i]);
    io->write_dac(io->context,0x03C6,s->dac_mask);
    io->write_dac(io->context,DAC_WRITE_INDEX,0);
    for(j=0;j<S3_VGA_DAC_BYTES;++j) io->write_dac(io->context,DAC_DATA,s->dac[j]);
    io->write_crtc(io->context,0x38,s->crtc_ext[8]);
    io->write_crtc(io->context,0x11,s->crtc[0x11]);
    io->write_seq(io->context,0,s->seq[0]);
    io->write_seq(io->context,1,s->seq[1]);
    io->enable_ac_display(io->context);
    if (!S3_VgaCaptureState(io,&observed)) return 0;
    observed.valid=s->valid;
    return memcmp(&observed,s,sizeof(*s))==0;
}

int S3_VgaBridgeCapture(void S3_FAR *context,S3_U8 S3_FAR *bytes,S3_U16 size)
{
    S3_VGA_STATE state;
    if (!bytes || size < sizeof(state) || !S3_VgaCaptureState((const S3_VGA_STATE_IO S3_FAR *)context,&state)) return 0;
    memset(bytes,0,size);
    memcpy(bytes,&state,sizeof(state));
    return 1;
}

int S3_VgaBridgeRestore(void S3_FAR *context,const S3_U8 S3_FAR *bytes,S3_U16 size)
{
    S3_VGA_STATE state;
    if (!bytes || size < sizeof(state)) return 0;
    memcpy(&state,bytes,sizeof(state));
    return S3_VgaRestoreState((const S3_VGA_STATE_IO S3_FAR *)context,&state);
}
