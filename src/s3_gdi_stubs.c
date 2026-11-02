#include "s3_display.h"
#include "s3_surface.h"
#include "s3_trace.h"
#include "s3_brush.h"
#include "s3_raster.h"
#include "s3_dib.h"
#include "s3_output.h"
#include "s3_pixel.h"
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
    int ok, excluded;
    S3_RECT16 destination_rect, source_rect;
    S3_DisplaySetRect(&destination_rect, (S3_I16)destination_x, (S3_I16)destination_y,
                      (S3_I32)(S3_I16)destination_x + (S3_I16)width,
                      (S3_I32)(S3_I16)destination_y + (S3_I16)height);
    S3_DisplaySetRect(&source_rect, (S3_I16)source_x, (S3_I16)source_y,
                      (S3_I32)(S3_I16)source_x + (S3_I16)width,
                      (S3_I32)(S3_I16)source_y + (S3_I16)height);
    excluded = S3_DisplayExclude(destination, &destination_rect, source, &source_rect);
    ok = S3_RasterBitBlt(destination, (S3_I16)destination_x, (S3_I16)destination_y,
                         source, (S3_I16)source_x, (S3_I16)source_y,
                         (S3_I16)width, (S3_I16)height, (S3_U32)rop3,
                         (const S3_PBRUSH16 S3_FAR *)physical_brush,
                         (const S3_DRAWMODE16 S3_FAR *)draw_mode);
    S3_DisplayUnexclude(excluded);
    S3_TRACE_EVENT(ok ? "BitBlt" : "BitBlt.fail", (S3_U16)((S3_U32)rop3 >> 16),
                   ((S3_PDEV S3_FAR *)destination)->pdType);
    return (S3_I16)(ok ? 1 : 0);
}

static S3_U16 s3_output_body(void S3_FAR *destination,
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

    S3_TRACE_EVENT("Output.enter", style, point_count);
    if (style == S3_OS_SCANLINES) {
        if (!S3_OutputScanlines(destination, point_count, points,
                                (const S3_PPEN16 S3_FAR *)physical_pen,
                                (const S3_PBRUSH16 S3_FAR *)physical_brush,
                                (const S3_DRAWMODE16 S3_FAR *)draw_mode,
                                clip_rect)) {
            S3_TRACE_EVENT("Output.scanlines.fail", point_count, 0);
            return 0;
        }
        return 1;
    }
    if (style == S3_OS_BEGINNSCAN || style == S3_OS_ENDNSCAN) {
        /* Brackets around a batch of OS_SCANLINES calls; nothing to do. */
        return 1;
    }
    if (style != S3_OS_POLYLINE) {
        /* The DDA contract asks GDI to simulate unsupported Output styles. */
        S3_TRACE_EVENT("Output.unsupported", style, 0);
        return S3_DDA_NOT_SUPPORTED;
    }
    if (point_count < 2 || points == (S3_POINT16 S3_FAR *)0 ||
        physical_pen == (void S3_FAR *)0 || draw_mode == (void S3_FAR *)0) {
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
        (s3_mode.bytes_pixel == 1 &&
         (pen->color & 0xFFFFFF00UL) != 0xFF000000UL)) {
        return S3_DDA_NOT_SUPPORTED;
    }
    mode = (S3_DRAWMODE16 S3_FAR *)draw_mode;
    surface = s3_device_surface(destination);
    if (surface == (S3_SURFACE S3_FAR *)0 || mode->Rop2 != S3_R2_COPYPEN ||
        surface->bytes_pixel != 1) {
        /* Memory bitmaps, other pen ROPs and direct colour: generic
           per-pixel path. */
        return (S3_U16)(S3_OutputPolyline(destination, point_count, points,
                                          pen->color, mode->Rop2,
                                          clip_rect) ? 1 : 0);
    }

    /* Win16 Output polyline segments draw their first point, but not the last.
       GDIINFO advertises CP_RECTANGLE, so lpClipRect is honoured. */
    for (i = 1; i < point_count; ++i) {
        if (!S3_SurfaceDrawLineClipped(surface,
                                       points[i - 1].x, points[i - 1].y,
                                       points[i].x, points[i].y,
                                       clip_rect, (S3_U8)pen->color)) {
            return 0;
        }
    }
    (void)physical_brush;
    return 1;
}

S3_U16 S3_FAR S3_PASCAL Output(void S3_FAR *destination,
                               S3_U16 style, S3_U16 point_count,
                               S3_POINT16 S3_FAR *points,
                               void S3_FAR *physical_pen,
                               void S3_FAR *physical_brush,
                               void S3_FAR *draw_mode,
                               S3_RECT16 S3_FAR *clip_rect)
{
    S3_RECT16 bounds;
    const S3_RECT16 S3_FAR *use = (const S3_RECT16 S3_FAR *)0;
    S3_I32 left, top, right, bottom;
    S3_U16 i;
    int excluded;
    S3_U16 result;

    if (style != S3_OS_SCANLINES && style != S3_OS_POLYLINE) {
        /* Scan brackets and styles GDI simulates draw nothing here. */
        return s3_output_body(destination, style, point_count, points, physical_pen,
                              physical_brush, draw_mode, clip_rect);
    }
    if (S3_DisplaySwCursorActive() && points != (S3_POINT16 S3_FAR *)0 &&
        point_count >= 1 &&
        s3_far_pointer_span_fits(points, (S3_U32)point_count * sizeof(S3_POINT16))) {
        if (style == S3_OS_SCANLINES) {
            /* points[0].y is the line; later points are (start, end) x pairs. */
            top = points[0].y;
            bottom = top + 1;
            left = 32767L;
            right = -32768L;
            for (i = 1; i < point_count; ++i) {
                if (points[i].x < left) left = points[i].x;
                if (points[i].y > right) right = points[i].y;
            }
        } else {
            left = right = points[0].x;
            top = bottom = points[0].y;
            for (i = 1; i < point_count; ++i) {
                if (points[i].x < left) left = points[i].x;
                if (points[i].x > right) right = points[i].x;
                if (points[i].y < top) top = points[i].y;
                if (points[i].y > bottom) bottom = points[i].y;
            }
            ++right;
            ++bottom;
        }
        S3_DisplaySetRect(&bounds, left, top, right, bottom);
        use = &bounds;
    }
    excluded = S3_DisplayExclude(destination, use, (void S3_FAR *)0,
                                 (const S3_RECT16 S3_FAR *)0);
    result = s3_output_body(destination, style, point_count, points, physical_pen,
                                    physical_brush, draw_mode, clip_rect);
    S3_DisplayUnexclude(excluded);
    return result;
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
    if (s3_mode.bytes_pixel == 1 &&
        (physical_color & 0xFFFFFF00UL) != 0xFF000000UL) return 0;
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

static S3_U32 s3_pixel_body(void S3_FAR *destination,
                               S3_U16 x, S3_U16 y, S3_U32 physical_color,
                               void S3_FAR *draw_mode)
{
    S3_TARGET target;
    S3_U8 pixel[4];
    S3_U8 rop;
    S3_U16 bp;
    S3_U32 value, pen;
    S3_TRACE_EVENT("Pixel", 0, 0);
    if (!S3_BindTarget(destination, &target) ||
        x >= target.width || y >= target.height) {
        return S3_PIXEL_ERROR;
    }
    bp = target.bytes_pixel;
    if (!S3_TargetReadRow(&target, (S3_I16)x, (S3_I16)y, 1, pixel)) {
        return S3_PIXEL_ERROR;
    }
    value = target.kind == S3_TARGET_MEM1
            ? S3_RGBToPixel(pixel[0] ? 0x00FFFFFFUL : 0)
            : S3_PixelLoad(pixel, bp);
    if (draw_mode == (void S3_FAR *)0) {
        return S3_PixelToPhysical(value);
    }
    rop = S3_Rop2ToRop3(((S3_DRAWMODE16 S3_FAR *)draw_mode)->Rop2);
    if (rop == 0xFF && ((S3_DRAWMODE16 S3_FAR *)draw_mode)->Rop2 != 16) {
        return S3_PIXEL_ERROR;
    }
    pen = S3_PhysicalToPixel(physical_color);
    if (target.kind == S3_TARGET_MEM1) {
        pen = S3_PixelIsLight(pen) ? 0xFF : 0x00;
        pixel[0] = (S3_U8)S3_Rop3Long(rop, pen, 0, pixel[0]);
    } else {
        S3_PixelStore(pixel, S3_Rop3Long(rop, pen, 0, value), bp);
    }
    if (!S3_TargetWriteRow(&target, (S3_I16)x, (S3_I16)y, 1, pixel)) {
        return S3_PIXEL_ERROR;
    }
    return 1;
}

S3_U32 S3_FAR S3_PASCAL Pixel(void S3_FAR *destination,
                               S3_U16 x, S3_U16 y, S3_U32 physical_color,
                               void S3_FAR *draw_mode)
{
    S3_RECT16 bounds;
    int excluded;
    S3_U32 result;
    S3_DisplaySetRect(&bounds, (S3_I16)x, (S3_I16)y, (S3_I32)(S3_I16)x + 1,
                      (S3_I32)(S3_I16)y + 1);
    excluded = S3_DisplayExclude(destination, &bounds, (void S3_FAR *)0,
                                 (const S3_RECT16 S3_FAR *)0);
    result = s3_pixel_body(destination, x, y, physical_color, draw_mode);
    S3_DisplayUnexclude(excluded);
    return result;
}

static S3_U16 s3_setdibitstodevice_body(
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
    S3_TRACE_EVENT("SetDIBitsToDevice", scan_start, scan_count);
    (void)draw_mode;

    /* Without a translate table (non-palette device) the DIB carries RGB
       colours: convert any 1/4/8/24-bpp DIB through the generic DIB path.
       Direct-colour modes always take it. */
    if (translate == (S3_I16 S3_FAR *)0 || s3_mode.bytes_pixel != 1) {
        return S3_DibToDevice(destination, (S3_I16)x, (S3_I16)y, scan_start,
                              scan_count, clip_rect, dib_bits, bitmap_info);
    }

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
    S3_RECT16 bounds;
    const S3_RECT16 S3_FAR *use = (const S3_RECT16 S3_FAR *)0;
    int excluded;
    S3_U16 result;
    if (bitmap_info != (S3_BITMAPINFO16 S3_FAR *)0) {
        /* The DIB's rows land between y and y + biHeight. */
        S3_DisplaySetRect(&bounds, (S3_I16)x, (S3_I16)y,
                          (S3_I32)(S3_I16)x + (S3_I32)bitmap_info->bmiHeader.biWidth,
                          (S3_I32)(S3_I16)y + (S3_I32)bitmap_info->bmiHeader.biHeight);
        use = &bounds;
    }
    excluded = S3_DisplayExclude(destination, use, (void S3_FAR *)0,
                                 (const S3_RECT16 S3_FAR *)0);
    result = s3_setdibitstodevice_body(destination, x, y, scan_start, scan_count,
                                    clip_rect, draw_mode, dib_bits, bitmap_info, translate);
    S3_DisplayUnexclude(excluded);
    return result;
}
