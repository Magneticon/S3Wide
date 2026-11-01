#ifndef S3_HW_H
#define S3_HW_H

#include "s3_abi.h"

typedef S3_U8 (S3_FAR S3_PASCAL *S3_READ_CRTC)(void S3_FAR *context,
                                               S3_U16 index_port,
                                               S3_U8 index);
typedef void (S3_FAR S3_PASCAL *S3_WRITE_CRTC)(void S3_FAR *context,
                                                S3_U16 index_port,
                                                S3_U8 index,
                                                S3_U8 value);
typedef void (S3_FAR S3_PASCAL *S3_WRITE_DAC)(void S3_FAR *context,
                                               S3_U16 port,
                                               S3_U8 value);

typedef struct S3_REGISTER_IO {
    S3_U16 crtc_index_port;
    void S3_FAR *context;
    S3_READ_CRTC read_crtc;
    S3_WRITE_CRTC write_crtc;
    S3_WRITE_DAC write_dac;
} S3_REGISTER_IO;

typedef S3_U8 (S3_FAR S3_PASCAL *S3_READ_INDEXED)(void S3_FAR *context,
                                                   S3_U8 index);
typedef void (S3_FAR S3_PASCAL *S3_WRITE_INDEXED)(void S3_FAR *context,
                                                   S3_U8 index,
                                                   S3_U8 value);
typedef S3_U8 (S3_FAR S3_PASCAL *S3_READ_MISC)(void S3_FAR *context);
typedef void (S3_FAR S3_PASCAL *S3_WRITE_MISC)(void S3_FAR *context,
                                                S3_U8 value);

typedef struct S3_MODE_IO {
    void S3_FAR *context;
    S3_U16 crtc_index_port;
    S3_READ_INDEXED read_seq;
    S3_WRITE_INDEXED write_seq;
    S3_READ_CRTC read_crtc;
    S3_WRITE_CRTC write_crtc;
    S3_READ_MISC read_misc;
    S3_WRITE_MISC write_misc;
} S3_MODE_IO;

#define S3_MODE_CRTC_REG_COUNT 0x19
#define S3_MODE_EXT_REG_COUNT  10

/* Snapshot covers exactly the registers changed by the timing/PLL candidate.
   It does not claim to capture a complete VGA mode or memory mapping. */
typedef struct S3_MODE_SNAPSHOT {
    S3_U8 valid;
    S3_U8 misc_output;
    S3_U8 seq01;
    S3_U8 seq08;
    S3_U8 seq12;
    S3_U8 seq13;
    S3_U8 seq15;
    S3_U8 crtc[S3_MODE_CRTC_REG_COUNT];
    S3_U8 crtc38;
    S3_U8 crtc3a;
    S3_U8 extended[S3_MODE_EXT_REG_COUNT];
} S3_MODE_SNAPSHOT;

/* S3 CRTC 35h low nibble and 51h bits 2-3 select 64 KiB CPU banks. */
int S3_Select64KBank(S3_REGISTER_IO S3_FAR *io, S3_U32 bank);

/* Program the driver's fixed 3:3:2 palette for the standard 6-bit VGA DAC. */
int S3_ProgramRGB332Palette6(S3_REGISTER_IO S3_FAR *io);

/* DOSBox-X modeled Trio PLL byte fields: SR12 packs N[4:0], R[7:5];
   SR13 packs M[6:0]. The search range is limited to the modeled R=0..3. */
int S3_EncodeVideoPLL(S3_U8 m, S3_U8 n, S3_U8 r,
                      S3_U8 S3_FAR *sr12, S3_U8 S3_FAR *sr13);
int S3_DecodeVideoPLL(S3_U8 sr12, S3_U8 sr13,
                      S3_U8 S3_FAR *m, S3_U8 S3_FAR *n, S3_U8 S3_FAR *r);
S3_U32 S3_VideoPLLClockKHz(S3_U8 m, S3_U8 n, S3_U8 r);

/* Model-only 1280x720x8 timing/PLL overlay. It snapshots and changes only
   listed VGA/S3 timing fields; framebuffer aperture, GC/AC state, display
   start, and CR42's DCLK-select nibble remain outside this candidate. */
int S3_CaptureModeSnapshot(const S3_MODE_IO S3_FAR *io,
                           S3_MODE_SNAPSHOT S3_FAR *snapshot);
int S3_Program720p8bppCandidate(const S3_MODE_IO S3_FAR *io,
                                S3_MODE_SNAPSHOT S3_FAR *snapshot);
int S3_RestoreModeSnapshot(const S3_MODE_IO S3_FAR *io,
                           const S3_MODE_SNAPSHOT S3_FAR *snapshot);

/* The target's 8-bpp CRTC offset is pitch/8. This helper handles the base
   8-bit CRTC offset only (0..255); higher offsets need S3 extension fields. */
int S3_Encode8BppPitch(S3_U16 pitch, S3_U8 S3_FAR *crtc_offset);
int S3_Decode8BppPitch(S3_U8 crtc_offset, S3_U16 S3_FAR *pitch);

#endif
