#include "s3_abi.h"

#define S3_INDEX_MARKER 0xFF000000UL

static S3_U8 s3_quantize(S3_U8 component, S3_U16 levels)
{
    return (S3_U8)(((S3_U16)component * (levels - 1) + 127) / 255);
}

static S3_U8 s3_expand(S3_U8 value, S3_U16 levels)
{
    return (S3_U8)(((S3_U16)value * 255 + (levels - 1) / 2) / (levels - 1));
}

int S3_RGBToColorIndex(S3_U32 rgb, S3_U8 S3_FAR *index_out)
{
    S3_U8 red, green, blue;
    if (index_out == (S3_U8 S3_FAR *)0 || (rgb & 0xFF000000UL) != 0) {
        return 0;
    }
    red = s3_quantize((S3_U8)(rgb & 0xFF), 8);
    green = s3_quantize((S3_U8)((rgb >> 8) & 0xFF), 8);
    blue = s3_quantize((S3_U8)((rgb >> 16) & 0xFF), 4);
    *index_out = (S3_U8)((red << 5) | (green << 2) | blue);
    return 1;
}

int S3_ColorIndexToRGB(S3_U8 index, S3_U32 S3_FAR *rgb_out)
{
    S3_U8 red, green, blue;
    if (rgb_out == (S3_U32 S3_FAR *)0) {
        return 0;
    }
    red = s3_expand((S3_U8)(index >> 5), 8);
    green = s3_expand((S3_U8)((index >> 2) & 7), 8);
    blue = s3_expand((S3_U8)(index & 3), 4);
    *rgb_out = (S3_U32)red | ((S3_U32)green << 8) | ((S3_U32)blue << 16);
    return 1;
}

S3_U32 S3_ColorInfoCore(S3_U32 color_in, S3_U32 S3_FAR *physical_out)
{
    S3_U8 index;
    S3_U32 rgb;
    if ((color_in & S3_INDEX_MARKER) == S3_INDEX_MARKER) {
        index = (S3_U8)color_in;
        if (physical_out != (S3_U32 S3_FAR *)0) {
            *physical_out = color_in;
            return color_in;
        }
        (void)S3_ColorIndexToRGB(index, &rgb);
        return rgb;
    }

    if (physical_out == (S3_U32 S3_FAR *)0) {
        return color_in;
    }
    if (!S3_RGBToColorIndex(color_in, &index)) {
        return color_in;
    }
    *physical_out = S3_INDEX_MARKER | index;
    (void)S3_ColorIndexToRGB(index, &rgb);
    return rgb;
}

S3_U32 S3_FAR S3_PASCAL ColorInfo(S3_PDEV S3_FAR *device,
                                  S3_U32 color_in,
                                  S3_U32 S3_FAR *physical_out)
{
    (void)device;
    return S3_ColorInfoCore(color_in, physical_out);
}
