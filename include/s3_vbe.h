#ifndef S3_VBE_H
#define S3_VBE_H

#include "s3_win16_bridge.h"
#include "s3_mode.h"

/* VESA BIOS mode-info fields the driver uses. */
typedef struct S3_VBE_MODE_INFO {
    S3_U16 attributes;
    S3_U16 width;
    S3_U16 height;
    S3_U16 bits_pixel;
    S3_U16 memory_model;
    S3_U16 red_mask_size;
    S3_U16 pitch;            /* linear pitch where VBE 3 reports one */
    S3_U32 lfb_physical;
} S3_VBE_MODE_INFO;

#define S3_VBE_ATTR_SUPPORTED 0x0001U
#define S3_VBE_ATTR_GRAPHICS  0x0010U
#define S3_VBE_ATTR_LFB       0x0080U
#define S3_VBE_MODEL_PACKED   4U
#define S3_VBE_MODEL_DIRECT   6U

/* Decode a raw 256-byte VBE mode-info block. */
void S3_VbeDecodeModeInfo(const S3_U8 S3_FAR *raw, S3_U16 vbe_version,
                          S3_VBE_MODE_INFO S3_FAR *info);

/* Nonzero when a decoded mode is a linear-framebuffer graphics mode of
   exactly width x height x bits_pixel that this driver can draw:
   8 bpp packed, 16 bpp 5:6:5 direct, or 32 bpp direct. */
int S3_VbeModeMatches(const S3_VBE_MODE_INFO S3_FAR *info, S3_U16 width,
                      S3_U16 height, S3_U16 bits_pixel);

#if !defined(S3_HOST_TEST)
/* Mode callbacks for the bridge: capture saves the current BIOS mode,
   program sets s3_mode.vbe_mode with its linear framebuffer (and the colour
   table at 8 bpp), restore returns to the saved mode. Selector callbacks
   come from S3_Win16NativeSelectorOps. */
int S3_VbeModeOps(S3_BRIDGE_OPS S3_FAR *ops);
#endif

#endif
