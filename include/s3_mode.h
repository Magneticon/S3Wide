#ifndef S3_MODE_H
#define S3_MODE_H

#include "s3_abi.h"

/* The display mode the driver reports to GDI and draws into. It is chosen
   once, before the first GDIINFO request, and stays fixed while Windows
   runs. Two backends program the hardware:
     S3_BACKEND_S3   direct S3 Trio64 register programming of the native
                     1280x720x8 mode, banked through the A000 window, with
                     the S3 hardware cursor;
     S3_BACKEND_VBE  a VESA BIOS mode with a linear framebuffer, reached
                     through a protected-mode selector whose base is moved
                     in 64 KiB steps, with a software cursor. */
#define S3_BACKEND_S3   1U
#define S3_BACKEND_VBE  2U

#define S3_MODE_MAX_WIDTH  4096U
#define S3_MODE_MAX_HEIGHT 4096U

typedef struct S3_MODE {
    S3_U16 backend;
    S3_U16 width;
    S3_U16 height;
    S3_U16 bits_pixel;      /* 8, 16 or 32 */
    S3_U16 bytes_pixel;
    S3_U16 pitch;           /* bytes per scanline */
    S3_U32 framebuffer_bytes;
    S3_U16 vbe_mode;        /* VBE backend only */
    S3_U32 lfb_physical;    /* VBE backend only */
    S3_U32 lfb_linear;      /* VBE backend: mapped address, 0 until mapped */
} S3_MODE;

/* The selected mode; S3_ModeDefault() until a selection replaces it. */
extern S3_MODE s3_mode;

/* The native S3 mode: 1280x720x8 through the S3 backend. */
void S3_ModeDefault(S3_MODE S3_FAR *mode);

/* Fill a VBE-backend mode after checking it is drawable by this driver:
   8, 16 or 32 bpp, a pitch that holds a whole row and is a multiple of the
   pixel size, a frame that fits 4096x4096, and a nonzero framebuffer
   address. Returns nonzero on success and leaves mode untouched on
   failure. */
int S3_ModeSetVBE(S3_MODE S3_FAR *mode, S3_U16 width, S3_U16 height,
                  S3_U16 bits_pixel, S3_U16 pitch, S3_U16 vbe_mode,
                  S3_U32 lfb_physical);

/* Choose a mode from the SYSTEM.INI request and what the VBE offers.
   Requests of 0 mean "default". Win16 only; the host build keeps the
   default. Returns nonzero when s3_mode is usable. */
int S3_ModeSelect(void);

#endif
