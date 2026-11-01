#ifndef S3_VGA_STATE_H
#define S3_VGA_STATE_H

#include "s3_win16_bridge.h"

#define S3_VGA_SEQ_BYTES 6
#define S3_VGA_CRTC_BYTES 0x19
#define S3_VGA_EXT_BYTES 0x3B
#define S3_VGA_GC_BYTES 9
#define S3_VGA_AC_BYTES 21
#define S3_VGA_DAC_BYTES 768

typedef S3_U8 (S3_FAR S3_PASCAL *S3_VGA_READ_INDEX)(void S3_FAR *, S3_U8);
typedef void (S3_FAR S3_PASCAL *S3_VGA_WRITE_INDEX)(void S3_FAR *, S3_U8, S3_U8);
typedef S3_U8 (S3_FAR S3_PASCAL *S3_VGA_READ_MISC)(void S3_FAR *);
typedef void (S3_FAR S3_PASCAL *S3_VGA_WRITE_MISC)(void S3_FAR *, S3_U8);
typedef S3_U8 (S3_FAR S3_PASCAL *S3_VGA_READ_DAC)(void S3_FAR *);
typedef S3_U8 (S3_FAR S3_PASCAL *S3_VGA_READ_DAC_MASK)(void S3_FAR *);
typedef void (S3_FAR S3_PASCAL *S3_VGA_WRITE_DAC)(void S3_FAR *, S3_U16, S3_U8);
typedef void (S3_FAR S3_PASCAL *S3_VGA_AC_DISPLAY_ENABLE)(void S3_FAR *);

typedef struct S3_VGA_STATE_IO {
    void S3_FAR *context;
    S3_U16 crtc_port;
    S3_VGA_READ_INDEX read_seq;
    S3_VGA_WRITE_INDEX write_seq;
    S3_VGA_READ_INDEX read_crtc;
    S3_VGA_WRITE_INDEX write_crtc;
    S3_VGA_READ_INDEX read_gc;
    S3_VGA_WRITE_INDEX write_gc;
    S3_VGA_READ_INDEX read_ac;
    S3_VGA_WRITE_INDEX write_ac;
    S3_VGA_READ_MISC read_misc;
    S3_VGA_WRITE_MISC write_misc;
    S3_VGA_READ_DAC read_dac;
    S3_VGA_READ_DAC_MASK read_dac_mask;
    S3_VGA_WRITE_DAC write_dac;
    S3_VGA_AC_DISPLAY_ENABLE enable_ac_display;
} S3_VGA_STATE_IO;

typedef struct S3_VGA_STATE {
    /* Indexed VGA/S3 programming registers and DAC palette only. This does
       not save VRAM, font planes, or a full text-console image. */
    S3_U8 valid;
    S3_U8 misc;
    S3_U8 seq[S3_VGA_SEQ_BYTES]; /* indices 0..4 and 8 */
    S3_U8 seq10, seq11, seq12, seq13, seq15;
    S3_U8 crtc[S3_VGA_CRTC_BYTES];
    S3_U8 crtc_ext[S3_VGA_EXT_BYTES]; /* indices 30h..6Ah */
    S3_U8 gc[S3_VGA_GC_BYTES];
    S3_U8 ac[S3_VGA_AC_BYTES];
    S3_U8 dac_mask;
    S3_U8 dac[S3_VGA_DAC_BYTES];
} S3_VGA_STATE;

int S3_VgaCaptureState(const S3_VGA_STATE_IO S3_FAR *io, S3_VGA_STATE S3_FAR *state);
int S3_VgaRestoreState(const S3_VGA_STATE_IO S3_FAR *io, const S3_VGA_STATE S3_FAR *state);

/* These callbacks let the Win16 lifecycle own a concrete VGA snapshot. The
   context points to S3_VGA_STATE_IO; no hardware provider is selected here. */
int S3_VgaBridgeCapture(void S3_FAR *context, S3_U8 S3_FAR *bytes, S3_U16 size);
int S3_VgaBridgeRestore(void S3_FAR *context, const S3_U8 S3_FAR *bytes, S3_U16 size);

#endif
