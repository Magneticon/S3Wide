#include "s3_surface.h"
#include "s3_trace.h"
#include "s3_brush.h"
#if !defined(S3_HOST_TEST)
#include <i86.h>
#endif

static S3_SURFACE S3_FAR *s3_device_surface(void S3_FAR *device)
{
    S3_PDEV S3_FAR *pdev = (S3_PDEV S3_FAR *)device;
    if (pdev == (S3_PDEV S3_FAR *)0 || pdev->pdType != S3_PDEV_DISPLAY ||
        pdev->surface_state == (void S3_FAR *)0) {
        return (S3_SURFACE S3_FAR *)0;
    }
    return (S3_SURFACE S3_FAR *)pdev->surface_state;
}

static int s3_far_pointer_span_fits(const void S3_FAR *pointer, S3_U32 bytes)
{
    if (pointer == (const void S3_FAR *)0) return 0;
#if !defined(S3_HOST_TEST)
    return S3_FarSpanFitsSingleSegment((S3_U16)FP_OFF(pointer), bytes);
#else
    return S3_FarSpanFitsSingleSegment(0, bytes);
#endif
}

S3_I16 S3_FAR S3_PASCAL BitBlt(void S3_FAR *destination,
                               S3_U16 destination_x, S3_U16 destination_y,
                               void S3_FAR *source,
                               S3_U16 source_x, S3_U16 source_y,
                               S3_U16 width, S3_U16 height,
                               S3_I32 rop3, void S3_FAR *physical_brush,
                               void S3_FAR *draw_mode)
{
    S3_SURFACE S3_FAR *destination_surface;
    S3_BITMAP16 S3_FAR *source_bitmap;
    S3_DIB8_VIEW dib;
    S3_U32 data_bytes;
    S3_TRACE_EVENT("BitBlt", 0, 0);
    (void)physical_brush;
    (void)draw_mode;

    destination_surface = s3_device_surface(destination);
    source_bitmap = (S3_BITMAP16 S3_FAR *)source;
    if (destination_surface == (S3_SURFACE S3_FAR *)0 || source_bitmap ==
        (S3_BITMAP16 S3_FAR *)0 || rop3 != (S3_I32)S3_ROP3_SRCCOPY ||
        source_bitmap->bmType != 0 || source_bitmap->bmWidth <= 0 ||
        source_bitmap->bmHeight <= 0 || source_bitmap->bmWidthBytes < source_bitmap->bmWidth ||
        (source_bitmap->bmWidthBytes & 1) != 0 || source_bitmap->bmPlanes != 1 ||
        source_bitmap->bmBitsPixel != 8 || source_bitmap->bmBits == (S3_U8 S3_FAR *)0 ||
        destination_x > 32767U || destination_y > 32767U ||
        source_x > 32767U || source_y > 32767U) {
        return 0;
    }
    data_bytes = (S3_U32)(S3_U16)source_bitmap->bmWidthBytes *
                 (S3_U32)(S3_U16)source_bitmap->bmHeight;
    if (source_bitmap->bmWidthPlanes != data_bytes ||
        source_bitmap->bmSegmentIndex != 0 ||
        source_bitmap->bmScanSegment != 0 || source_bitmap->bmFillBytes != 0) {
        return 0;
    }
    /* BITMAP.bmBits is a far pointer, not a huge pointer. Never walk it across
       a 64-KiB selector/segment boundary. Larger DIBs need a banded DDI path. */
    if (data_bytes > 65536UL) {
        return 0;
    }
#if !defined(S3_HOST_TEST)
    if (!S3_FarSpanFitsSingleSegment((S3_U16)FP_OFF(source_bitmap->bmBits),
                                    data_bytes)) {
        return 0;
    }
#else
    if (!S3_FarSpanFitsSingleSegment(0, data_bytes)) {
        return 0;
    }
#endif
    dib.width = (S3_U16)source_bitmap->bmWidth;
    dib.height = (S3_U16)source_bitmap->bmHeight;
    dib.stride = (S3_U16)source_bitmap->bmWidthBytes;
    dib.data_bytes = data_bytes;
    dib.bits = source_bitmap->bmBits;
    dib.top_down = 1; /* Windows DDB/PBITMAP rows are top-down. */
    return (S3_I16)S3_SurfaceBlitDIB8(destination_surface,
                                     (S3_I16)destination_x,
                                     (S3_I16)destination_y,
                                     &dib, (S3_I16)source_x, (S3_I16)source_y,
                                     width, height, (S3_U32)rop3);
}

S3_U16 S3_FAR S3_PASCAL Output(void S3_FAR *destination,
                               S3_U16 style, S3_U16 point_count,
                               S3_POINT16 S3_FAR *points,
                               void S3_FAR *physical_pen,
                               void S3_FAR *physical_brush,
                               void S3_FAR *draw_mode,
                               S3_RECT16 S3_FAR *clip_rect)
{
    S3_SURFACE S3_FAR *surface;
    S3_PPEN16 S3_FAR *pen;
    S3_DRAWMODE16 S3_FAR *mode;
    S3_U32 point_bytes;
    S3_U16 i;
    S3_TRACE_EVENT("Output", 0, 0);
    (void)clip_rect;
    if (style != S3_OS_POLYLINE) {
        /* The DDA contract asks GDI to simulate unsupported Output styles. */
        return S3_DDA_NOT_SUPPORTED;
    }
    surface = s3_device_surface(destination);
    if (surface == (S3_SURFACE S3_FAR *)0 || point_count < 2 ||
        points == (S3_POINT16 S3_FAR *)0 || physical_pen == (void S3_FAR *)0 ||
        draw_mode == (void S3_FAR *)0) {
        return 0;
    }
    point_bytes = (S3_U32)point_count * (S3_U32)sizeof(S3_POINT16);
    if (!s3_far_pointer_span_fits(points, point_bytes) ||
        !s3_far_pointer_span_fits(physical_pen, sizeof(S3_PPEN16)) ||
        !s3_far_pointer_span_fits(draw_mode, sizeof(S3_DRAWMODE16))) {
        return 0;
    }
    pen = (S3_PPEN16 S3_FAR *)physical_pen;
    if (pen->style == S3_LS_NOLINE || pen->width.x < 0) return 1;
    if (pen->style != S3_LS_SOLID || pen->width.x > 1 ||
        (pen->color & 0xFFFFFF00UL) != 0xFF000000UL) {
        return S3_DDA_NOT_SUPPORTED;
    }
    mode = (S3_DRAWMODE16 S3_FAR *)draw_mode;
    if (mode->Rop2 != S3_R2_COPYPEN) return S3_DDA_NOT_SUPPORTED;

    /* Win16 Output polyline segments draw their first point, but not the last.
       dpClip is zero in GDIINFO, so this DDA ignores lpClipRect and enforces
       the framebuffer bounds itself. */
    for (i = 1; i < point_count; ++i) {
        if (!S3_SurfaceDrawLineClipped(surface,
                                       points[i - 1].x, points[i - 1].y,
                                       points[i].x, points[i].y,
                                       (const S3_RECT16 S3_FAR *)0,
                                       (S3_U8)pen->color)) {
            return 0;
        }
    }
    (void)physical_brush;
    return 1;
}

S3_U32 S3_FAR S3_PASCAL RealizeObject(S3_PDEV S3_FAR *device,
                                      S3_U16 style,
                                      void S3_FAR *logical_object,
                                      void S3_FAR *physical_object,
                                      void S3_FAR *text_xform)
{
    S3_I16 signed_style = (S3_I16)style;
    S3_U16 object_type;
    S3_LPEN16 S3_FAR *logical_pen;
    S3_PPEN16 physical_pen;
    S3_U32 physical_color;
    S3_TRACE_EVENT("RealizeObject", style, physical_object == (void S3_FAR *)0);
    (void)device;
    (void)text_xform;
    if (signed_style < 0) {
        object_type = (S3_U16)(0U - (S3_U16)signed_style);
        /* This driver allocates no side resources for realized pens or
           brushes, so deleting one only validates the pointers. */
        if ((object_type != S3_OBJ_PEN && object_type != S3_OBJ_BRUSH) ||
            logical_object == (void S3_FAR *)0 ||
            physical_object == (void S3_FAR *)0) {
            return 0;
        }
        return 1UL;
    }
    if (style == S3_OBJ_BRUSH) {
        if (logical_object == (void S3_FAR *)0 ||
            !s3_far_pointer_span_fits(logical_object, sizeof(S3_LBRUSH16))) {
            return 0;
        }
        if (physical_object == (void S3_FAR *)0) {
            return (S3_U32)sizeof(S3_PBRUSH16);
        }
        if (!s3_far_pointer_span_fits(physical_object, sizeof(S3_PBRUSH16)) ||
            !S3_RealizeBrush((const S3_LBRUSH16 S3_FAR *)logical_object,
                             (S3_PBRUSH16 S3_FAR *)physical_object)) {
            S3_TRACE_EVENT("RealizeObject.brush.fail",
                           ((S3_LBRUSH16 S3_FAR *)logical_object)->lbStyle, 0);
            return 0;
        }
        return (S3_U32)sizeof(S3_PBRUSH16);
    }
    /* OBJ_FONT and others: 0 tells GDI to use its own raster fonts. */
    if (style != S3_OBJ_PEN || logical_object == (void S3_FAR *)0 ||
        !s3_far_pointer_span_fits(logical_object, sizeof(S3_LPEN16))) {
        return 0;
    }
    logical_pen = (S3_LPEN16 S3_FAR *)logical_object;
    if (logical_pen->lopnWidth.x < 0) return 0;
    if ((logical_pen->lopnColor & 0xFF000000UL) != 0 &&
        (logical_pen->lopnColor & 0xFF000000UL) != 0xFF000000UL) {
        return 0;
    }
    (void)S3_ColorInfoCore(logical_pen->lopnColor, &physical_color);
    if ((physical_color & 0xFFFFFF00UL) != 0xFF000000UL) return 0;
    if (physical_object == (void S3_FAR *)0) return (S3_U32)sizeof(S3_PPEN16);
    if (!s3_far_pointer_span_fits(physical_object, sizeof(S3_PPEN16))) return 0;

    physical_pen.style = logical_pen->lopnStyle;
    physical_pen.width = logical_pen->lopnWidth;
    physical_pen.color = physical_color;
    if (logical_pen->lopnStyle == S3_LS_NOLINE) {
        physical_pen.style = S3_LS_NOLINE;
        physical_pen.width.x = -1;
        physical_pen.width.y = 0;
    } else {
        /* GDI can implement styled or wide lines when the driver reports no
           styled/wide-line capability; give it the required nominal pen. */
        physical_pen.style = S3_LS_SOLID;
        physical_pen.width.x = 1;
        physical_pen.width.y = 0;
    }
    *(S3_PPEN16 S3_FAR *)physical_object = physical_pen;
    return (S3_U32)sizeof(S3_PPEN16);
}

S3_U32 S3_FAR S3_PASCAL Pixel(void S3_FAR *destination,
                               S3_U16 x, S3_U16 y, S3_U32 physical_color,
                               void S3_FAR *draw_mode)
{
    S3_SURFACE S3_FAR *surface = s3_device_surface(destination);
    S3_U8 index;
    S3_TRACE_EVENT("Pixel", 0, 0);
    if (surface == (S3_SURFACE S3_FAR *)0 || x > 32767U || y > 32767U) {
        return S3_PIXEL_ERROR;
    }
    if (draw_mode == (void S3_FAR *)0) {
        if (!S3_SurfaceGetPixel(surface, (S3_I16)x, (S3_I16)y, &index)) {
            return S3_PIXEL_ERROR;
        }
        return 0xFF000000UL | (S3_U32)index;
    }
    if (((S3_DRAWMODE16 S3_FAR *)draw_mode)->Rop2 != S3_R2_COPYPEN ||
        (physical_color & 0xFFFFFF00UL) != 0xFF000000UL ||
        !S3_SurfacePutPixel(surface, (S3_I16)x, (S3_I16)y,
                            (S3_U8)physical_color)) {
        return S3_PIXEL_ERROR;
    }
    return 1;
}

S3_U16 S3_FAR S3_PASCAL SetDIBitsToDevice(
                               void S3_FAR *destination,
                               S3_U16 x, S3_U16 y,
                               S3_U16 scan_start, S3_U16 scan_count,
                               S3_RECT16 S3_FAR *clip_rect,
                               S3_DRAWMODE16 S3_FAR *draw_mode,
                               S3_U8 S3_FAR *dib_bits,
                               S3_BITMAPINFO16 S3_FAR *bitmap_info,
                               S3_I16 S3_FAR *translate)
{
    S3_SURFACE S3_FAR *surface = s3_device_surface(destination);
    S3_BITMAPINFOHEADER16 S3_FAR *header;
    S3_U32 row_stride, full_image_bytes, band_bytes;
    S3_U16 width, height;
    S3_TRACE_EVENT("SetDIBitsToDevice", 0, 0);
    (void)draw_mode;

    if (surface == (S3_SURFACE S3_FAR *)0 || bitmap_info ==
        (S3_BITMAPINFO16 S3_FAR *)0 || dib_bits == (S3_U8 S3_FAR *)0 ||
        translate == (S3_I16 S3_FAR *)0 ||
        (clip_rect != (S3_RECT16 S3_FAR *)0 &&
         !s3_far_pointer_span_fits(clip_rect, sizeof(S3_RECT16))) ||
        !s3_far_pointer_span_fits(bitmap_info,
                                  (S3_U32)sizeof(S3_BITMAPINFOHEADER16) +
                                  256UL * sizeof(S3_RGBQUAD16)) ||
        !s3_far_pointer_span_fits(translate, 256UL * sizeof(S3_I16))) {
        return 0;
    }
    header = &bitmap_info->bmiHeader;
    if (header->biSize != sizeof(S3_BITMAPINFOHEADER16) ||
        header->biWidth == 0 || header->biWidth > 32767UL ||
        header->biHeight == 0 || header->biHeight > 32767UL ||
        header->biPlanes != 1 || header->biBitCount != 8 ||
        header->biCompression != 0 ||
        (header->biClrUsed != 0 && header->biClrUsed != 256UL) ||
        (header->biClrImportant > 256UL) ||
        (S3_U32)scan_start + scan_count > header->biHeight ||
        scan_count == 0) {
        return 0;
    }
    width = (S3_U16)header->biWidth;
    height = (S3_U16)header->biHeight;
    row_stride = (header->biWidth + 3UL) & ~3UL;
    if (row_stride > 65535UL) return 0;
    full_image_bytes = row_stride * header->biHeight;
    if (header->biSizeImage != 0 && header->biSizeImage < full_image_bytes) {
        return 0;
    }
    band_bytes = row_stride * scan_count;
    if (!s3_far_pointer_span_fits(dib_bits, band_bytes)) return 0;
    return S3_SurfaceSetDIB8Band(surface, (S3_I32)x, (S3_I32)y,
                                 width, height, (S3_U16)row_stride,
                                 scan_start, scan_count, dib_bits, band_bytes,
#if !defined(S3_HOST_TEST)
                                 (S3_U16)FP_OFF(dib_bits),
#else
                                 0,
#endif
                                 translate, 256, clip_rect);
}
