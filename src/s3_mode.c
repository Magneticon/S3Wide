#include "s3_mode.h"

S3_MODE s3_mode = {
    S3_BACKEND_S3, S3_WIDTH, S3_HEIGHT, S3_BITS_PIXEL, 1, S3_PITCH,
    S3_FRAMEBUFFER_BYTES, 0, 0, 0
};

void S3_ModeDefault(S3_MODE S3_FAR *mode)
{
    if (mode == (S3_MODE S3_FAR *)0) return;
    mode->backend = S3_BACKEND_S3;
    mode->width = S3_WIDTH;
    mode->height = S3_HEIGHT;
    mode->bits_pixel = S3_BITS_PIXEL;
    mode->bytes_pixel = 1;
    mode->pitch = S3_PITCH;
    mode->framebuffer_bytes = S3_FRAMEBUFFER_BYTES;
    mode->vbe_mode = 0;
    mode->lfb_physical = 0;
    mode->lfb_linear = 0;
}

int S3_ModeSetVBE(S3_MODE S3_FAR *mode, S3_U16 width, S3_U16 height,
                  S3_U16 bits_pixel, S3_U16 pitch, S3_U16 vbe_mode,
                  S3_U32 lfb_physical)
{
    S3_U16 bytes_pixel;
    if (mode == (S3_MODE S3_FAR *)0 || width == 0 || height == 0 ||
        width > S3_MODE_MAX_WIDTH || height > S3_MODE_MAX_HEIGHT ||
        lfb_physical == 0) {
        return 0;
    }
    if (bits_pixel != 8 && bits_pixel != 16 && bits_pixel != 32) return 0;
    bytes_pixel = (S3_U16)(bits_pixel / 8);
    if ((S3_U32)pitch < (S3_U32)width * bytes_pixel ||
        pitch % bytes_pixel != 0) {
        return 0;
    }
    mode->backend = S3_BACKEND_VBE;
    mode->width = width;
    mode->height = height;
    mode->bits_pixel = bits_pixel;
    mode->bytes_pixel = bytes_pixel;
    mode->pitch = pitch;
    mode->framebuffer_bytes = (S3_U32)pitch * height;
    mode->vbe_mode = vbe_mode;
    mode->lfb_physical = lfb_physical;
    mode->lfb_linear = 0;
    return 1;
}

#if defined(S3_HOST_TEST)
int S3_ModeSelect(void)
{
    return 1;
}
#endif
