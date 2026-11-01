#include "s3_abi.h"
#include "s3_trace.h"

#define S3_INDEX_MARKER 0xFF000000UL

static S3_U8 s3_quantize(S3_U8 component, S3_U16 levels)
{
    return (S3_U8)(((S3_U16)component * (levels - 1) + 127) / 255);
}

static S3_U8 s3_expand(S3_U8 value, S3_U16 levels)
{
    return (S3_U8)(((S3_U16)value * 255 + (levels - 1) / 2) / (levels - 1));
}

/* Windows' 20 static colours. Those the 3:3:2 cube cannot represent take
   over the cube entry nearest to them (two collisions moved to neighbours:
   cream to 0xFE, medium grey to 0x96), so system colours are exact. */
typedef struct S3_SYSTEM_COLOR {
    S3_U8 index;
    S3_U8 red, green, blue;
} S3_SYSTEM_COLOR;

static const S3_SYSTEM_COLOR s3_system_colors[] = {
    {0x80, 128, 0, 0},     {0x10, 0, 128, 0},     {0x90, 128, 128, 0},
    {0x02, 0, 0, 128},     {0x82, 128, 0, 128},   {0x12, 0, 128, 128},
    {0xB6, 192, 192, 192}, {0x92, 128, 128, 128}, {0xBA, 192, 220, 192},
    {0xBB, 166, 202, 240}, {0xFE, 255, 251, 240}, {0x96, 160, 160, 164}
};

#define S3_SYSTEM_COLOR_COUNT (sizeof(s3_system_colors) / sizeof(s3_system_colors[0]))

static S3_U32 s3_system_rgb(const S3_SYSTEM_COLOR *color)
{
    return (S3_U32)color->red | ((S3_U32)color->green << 8) | ((S3_U32)color->blue << 16);
}

int S3_RGBToColorIndex(S3_U32 rgb, S3_U8 S3_FAR *index_out)
{
    S3_U8 red, green, blue;
    S3_U16 i;
    if (index_out == (S3_U8 S3_FAR *)0 || (rgb & 0xFF000000UL) != 0) {
        return 0;
    }
    for (i = 0; i < S3_SYSTEM_COLOR_COUNT; ++i) {
        if (s3_system_rgb(&s3_system_colors[i]) == rgb) {
            *index_out = s3_system_colors[i].index;
            return 1;
        }
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
    S3_U16 i;
    if (rgb_out == (S3_U32 S3_FAR *)0) {
        return 0;
    }
    for (i = 0; i < S3_SYSTEM_COLOR_COUNT; ++i) {
        if (s3_system_colors[i].index == index) {
            *rgb_out = s3_system_rgb(&s3_system_colors[i]);
            return 1;
        }
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
    S3_TRACE_EVENT("ColorInfo", (S3_U16)color_in, (S3_U16)(color_in >> 16));
    return S3_ColorInfoCore(color_in, physical_out);
}
