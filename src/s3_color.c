#include "s3_abi.h"
#include "s3_pixel.h"
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

S3_U32 S3_PixelLoad(const S3_U8 S3_FAR *p, S3_U16 bytes_pixel)
{
    return S3_PIXEL_GET(p, bytes_pixel);
}

void S3_PixelStore(S3_U8 S3_FAR *p, S3_U32 pixel, S3_U16 bytes_pixel)
{
    S3_PIXEL_PUT(p, pixel, bytes_pixel);
}

S3_U32 S3_RGBToPixel(S3_U32 rgb)
{
    S3_U16 red = (S3_U16)(rgb & 0xFF), green = (S3_U16)((rgb >> 8) & 0xFF);
    S3_U16 blue = (S3_U16)((rgb >> 16) & 0xFF);
    S3_U8 index = 0;
    switch (s3_mode.bytes_pixel) {
    case 2:
        return ((S3_U32)(red >> 3) << 11) | ((S3_U32)(green >> 2) << 5) |
               (S3_U32)(blue >> 3);
    case 4:
        return ((S3_U32)red << 16) | ((S3_U32)green << 8) | blue;
    default:
        (void)S3_RGBToColorIndex(rgb & 0x00FFFFFFUL, &index);
        return index;
    }
}

S3_U32 S3_PixelToRGB(S3_U32 pixel)
{
    S3_U32 rgb = 0;
    S3_U16 red, green, blue;
    switch (s3_mode.bytes_pixel) {
    case 2:
        /* Replicate the top bits so full scale maps to 255. */
        red = (S3_U16)((pixel >> 11) & 0x1F);
        green = (S3_U16)((pixel >> 5) & 0x3F);
        blue = (S3_U16)(pixel & 0x1F);
        red = (S3_U16)((red << 3) | (red >> 2));
        green = (S3_U16)((green << 2) | (green >> 4));
        blue = (S3_U16)((blue << 3) | (blue >> 2));
        return (S3_U32)red | ((S3_U32)green << 8) | ((S3_U32)blue << 16);
    case 4:
        return ((pixel >> 16) & 0xFF) | (pixel & 0xFF00UL) |
               ((pixel & 0xFF) << 16);
    default:
        (void)S3_ColorIndexToRGB((S3_U8)pixel, &rgb);
        return rgb;
    }
}

S3_U32 S3_PhysicalToPixel(S3_U32 physical)
{
    switch (s3_mode.bytes_pixel) {
    case 2:
        return physical & 0xFFFFUL;
    case 4:
        return physical & 0x00FFFFFFUL;
    default:
        return physical & 0xFFUL;
    }
}

S3_U32 S3_PixelToPhysical(S3_U32 pixel)
{
    return s3_mode.bytes_pixel == 1 ? S3_INDEX_MARKER | (pixel & 0xFF)
                                    : S3_PhysicalToPixel(pixel);
}

int S3_PixelIsLight(S3_U32 pixel)
{
    S3_U32 rgb = S3_PixelToRGB(pixel);
    S3_U32 luma = (rgb & 0xFF) * 30UL + ((rgb >> 8) & 0xFF) * 59UL +
                  ((rgb >> 16) & 0xFF) * 11UL;
    return luma >= 12750UL; /* half of 255 * 100 */
}

S3_U32 S3_ColorInfoCore(S3_U32 color_in, S3_U32 S3_FAR *physical_out)
{
    S3_U8 index;
    S3_U32 rgb, pixel;
    if (s3_mode.bytes_pixel != 1) {
        /* Direct colour: physical colours are pixel values. */
        if (physical_out == (S3_U32 S3_FAR *)0) {
            return S3_PixelToRGB(S3_PhysicalToPixel(color_in));
        }
        pixel = S3_RGBToPixel(color_in & 0x00FFFFFFUL);
        *physical_out = pixel;
        return S3_PixelToRGB(pixel);
    }
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
    /* No palette: PALETTERGB / PALETTEINDEX flag bytes are treated as the
       plain RGB colour, so *physical_out is always written. */
    color_in &= 0x00FFFFFFUL;
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
