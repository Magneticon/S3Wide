#include "s3_surface.h"
#include "s3_trace.h"

static int s3_map(S3_SURFACE S3_FAR *surface, S3_U32 bank)
{
    S3_U8 S3_FAR *window;
    if (surface == (S3_SURFACE S3_FAR *)0 || surface->bank_bytes == 0 ||
        surface->aperture_bytes < surface->bank_bytes ||
        surface->window == (S3_U8 S3_FAR *)0 || surface->map_bank == 0) {
        return 0;
    }
    if (surface->current_bank == bank) {
        return 1;
    }
    window = surface->map_bank(surface->context, bank);
    if (window == (S3_U8 S3_FAR *)0) {
        surface->window = (S3_U8 S3_FAR *)0;
        surface->current_bank = 0xFFFFFFFFUL;
        return 0;
    }
    surface->window = window;
    surface->current_bank = bank;
    return 1;
}

S3_U8 S3_FAR *S3_SurfaceSpan(S3_SURFACE S3_FAR *surface, S3_I16 x, S3_I16 y,
                             S3_U16 S3_FAR *contiguous)
{
    S3_U32 offset, bank, in_bank, left_in_bank;
    if (surface == (S3_SURFACE S3_FAR *)0 ||
        contiguous == (S3_U16 S3_FAR *)0 || surface->bank_bytes == 0 ||
        x < 0 || y < 0 ||
        (S3_U16)x >= surface->width || (S3_U16)y >= surface->height) {
        return (S3_U8 S3_FAR *)0;
    }
    offset = (S3_U32)(S3_U16)y * (S3_U32)surface->pitch +
             (S3_U32)(S3_U16)x * (surface->bytes_pixel ? surface->bytes_pixel : 1);
    if (offset >= surface->framebuffer_bytes) {
        return (S3_U8 S3_FAR *)0;
    }
    bank = offset / surface->bank_bytes;
    in_bank = offset % surface->bank_bytes;
    if (in_bank >= surface->aperture_bytes || !s3_map(surface, bank)) {
        return (S3_U8 S3_FAR *)0;
    }
    left_in_bank = surface->bank_bytes - in_bank;
    *contiguous = left_in_bank > 0xFFFFUL ? 0xFFFFU : (S3_U16)left_in_bank;
    return surface->window + (S3_U16)in_bank;
}

int S3_FarSpanFitsSingleSegment(S3_U16 pointer_offset, S3_U32 bytes)
{
    return bytes <= 65536UL - (S3_U32)pointer_offset;
}

int S3_SurfaceInit(S3_SURFACE S3_FAR *surface,
                   S3_U16 width, S3_U16 height, S3_U16 pitch,
                   S3_U32 framebuffer_bytes,
                   S3_U32 bank_bytes, S3_U32 aperture_bytes,
                   S3_U8 S3_FAR *initial_window,
                   void S3_FAR *context, S3_MAP_BANK map_bank)
{
    S3_U32 minimum_bytes;
    if (surface == (S3_SURFACE S3_FAR *)0 || width == 0 || height == 0 ||
        pitch < width || bank_bytes == 0 || aperture_bytes < bank_bytes ||
        initial_window == (S3_U8 S3_FAR *)0 || map_bank == 0) {
        S3_TRACE_EVENT("surface.init.args", (S3_U16)aperture_bytes,
                       (S3_U16)(aperture_bytes >> 16));
        S3_TRACE_EVENT("surface.init.geometry", pitch, height);
        return 0;
    }
    minimum_bytes = (S3_U32)pitch * (S3_U32)height;
    if (framebuffer_bytes < minimum_bytes) {
        S3_TRACE_EVENT("surface.init.size", (S3_U16)framebuffer_bytes,
                       (S3_U16)(framebuffer_bytes >> 16));
        return 0;
    }
    surface->width = width;
    surface->height = height;
    surface->pitch = pitch;
    surface->bytes_pixel = 1;
    surface->framebuffer_bytes = framebuffer_bytes;
    surface->bank_bytes = bank_bytes;
    surface->aperture_bytes = aperture_bytes;
    surface->current_bank = 0xFFFFFFFFUL;
    surface->window = initial_window;
    surface->context = context;
    surface->map_bank = map_bank;
    return 1;
}

int S3_SurfacePutPixel(S3_SURFACE S3_FAR *surface,
                       S3_I16 x, S3_I16 y, S3_U8 color)
{
    S3_U32 offset, bank, in_bank;
    if (surface == (S3_SURFACE S3_FAR *)0 || surface->bank_bytes == 0 ||
        surface->aperture_bytes < surface->bank_bytes || surface->pitch < surface->width ||
        surface->window == (S3_U8 S3_FAR *)0 || surface->map_bank == 0 ||
        x < 0 || y < 0 ||
        (S3_U16)x >= surface->width || (S3_U16)y >= surface->height) {
        return 0;
    }
    offset = (S3_U32)(S3_U16)y * (S3_U32)surface->pitch + (S3_U16)x;
    if (offset >= surface->framebuffer_bytes) {
        return 0;
    }
    bank = offset / surface->bank_bytes;
    in_bank = offset % surface->bank_bytes;
    if (in_bank >= surface->aperture_bytes || !s3_map(surface, bank)) {
        return 0;
    }
    surface->window[in_bank] = color;
    return 1;
}

int S3_SurfaceDrawLine(S3_SURFACE S3_FAR *surface,
                       S3_I16 x0, S3_I16 y0,
                       S3_I16 x1, S3_I16 y1,
                       S3_U8 color)
{
    S3_I32 x, y, end_x, end_y;
    S3_I32 dx, dy, step_x, step_y, error, twice_error;
    if (surface == (S3_SURFACE S3_FAR *)0 || x0 < 0 || y0 < 0 ||
        x1 < 0 || y1 < 0 || (S3_U16)x0 >= surface->width ||
        (S3_U16)x1 >= surface->width || (S3_U16)y0 >= surface->height ||
        (S3_U16)y1 >= surface->height) {
        return 0;
    }

    x = x0;
    y = y0;
    end_x = x1;
    end_y = y1;
    dx = end_x >= x ? end_x - x : x - end_x;
    dy = end_y >= y ? y - end_y : end_y - y;
    step_x = x < end_x ? 1 : -1;
    step_y = y < end_y ? 1 : -1;
    error = dx + dy;
    for (;;) {
        if (!S3_SurfacePutPixel(surface, (S3_I16)x, (S3_I16)y, color)) {
            return 0;
        }
        if (x == end_x && y == end_y) {
            break;
        }
        twice_error = error * 2;
        if (twice_error >= dy) {
            error += dy;
            x += step_x;
        }
        if (twice_error <= dx) {
            error += dx;
            y += step_y;
        }
    }
    return 1;
}

typedef struct S3_LINE_COORDS {
    S3_I32 x0;
    S3_I32 y0;
    S3_I32 sx;
    S3_I32 sy;
    S3_U32 major;
    S3_U32 minor;
    int x_major;
} S3_LINE_COORDS;

static S3_I32 s3_line_coord_at(const S3_LINE_COORDS *line,
                               int x_axis, S3_U32 k)
{
    S3_U32 advance;
    if (line->x_major) {
        if (x_axis) return line->x0 + line->sx * (S3_I32)k;
        advance = (k * line->minor + line->major / 2) / line->major;
        return line->y0 + line->sy * (S3_I32)advance;
    }
    if (!x_axis) return line->y0 + line->sy * (S3_I32)k;
    advance = (k * line->minor + line->major / 2) / line->major;
    return line->x0 + line->sx * (S3_I32)advance;
}

static S3_U32 s3_lower_bound_coord(const S3_LINE_COORDS *line,
                                  int x_axis, S3_U32 end,
                                  S3_I32 threshold, int increasing,
                                  int strict)
{
    S3_U32 low = 0, high = end;
    while (low < high) {
        S3_U32 mid = low + (high - low) / 2;
        S3_I32 value = s3_line_coord_at(line, x_axis, mid);
        int reached;
        if (increasing) {
            reached = strict ? value > threshold : value >= threshold;
        } else {
            reached = strict ? value < threshold : value <= threshold;
        }
        if (reached) high = mid;
        else low = mid + 1;
    }
    return low;
}

static int s3_line_axis_range(const S3_LINE_COORDS *line,
                              int x_axis, S3_I32 minimum, S3_I32 maximum,
                              S3_U32 end, S3_U32 *first, S3_U32 *after)
{
    S3_I32 direction = x_axis ? line->sx : line->sy;
    S3_I32 initial = x_axis ? line->x0 : line->y0;
    if (direction == 0) {
        if (initial < minimum || initial >= maximum) return 0;
        *first = 0;
        *after = end;
        return 1;
    }
    if (direction > 0) {
        *first = s3_lower_bound_coord(line, x_axis, end, minimum, 1, 0);
        *after = s3_lower_bound_coord(line, x_axis, end, maximum, 1, 0);
    } else {
        *first = s3_lower_bound_coord(line, x_axis, end, maximum, 0, 1);
        *after = s3_lower_bound_coord(line, x_axis, end, minimum, 0, 1);
    }
    return *first < *after;
}

int S3_LineVisit(S3_I16 x0, S3_I16 y0, S3_I16 x1, S3_I16 y1,
                 S3_I32 left, S3_I32 top, S3_I32 right, S3_I32 bottom,
                 S3_LINE_PLOT plot, void S3_FAR *context)
{
    S3_LINE_COORDS line;
    S3_I32 dx, dy;
    S3_U32 x_first, x_after, y_first, y_after, k;

    if (left >= right || top >= bottom) return 1;
    line.x0 = x0;
    line.y0 = y0;
    dx = x1 >= x0 ? (S3_I32)x1 - x0 : (S3_I32)x0 - x1;
    dy = y1 >= y0 ? (S3_I32)y1 - y0 : (S3_I32)y0 - y1;
    line.sx = x0 < x1 ? 1 : -1;
    line.sy = y0 < y1 ? 1 : -1;
    line.x_major = dx >= dy;
    line.major = (S3_U32)(line.x_major ? dx : dy);
    line.minor = (S3_U32)(line.x_major ? dy : dx);
    /* DDA line segments include their first point and exclude the final point. */
    if (line.major == 0) return 1;
    if (!s3_line_axis_range(&line, 1, left, right, line.major,
                            &x_first, &x_after) ||
        !s3_line_axis_range(&line, 0, top, bottom, line.major,
                            &y_first, &y_after)) {
        return 1;
    }
    if (y_first > x_first) x_first = y_first;
    if (y_after < x_after) x_after = y_after;
    for (k = x_first; k < x_after; ++k) {
        S3_I32 x = s3_line_coord_at(&line, 1, k);
        S3_I32 y = s3_line_coord_at(&line, 0, k);
        if (!plot(context, (S3_I16)x, (S3_I16)y)) {
            return 0;
        }
    }
    return 1;
}

typedef struct S3_SURFACE_PLOT {
    S3_SURFACE S3_FAR *surface;
    S3_U8 color;
} S3_SURFACE_PLOT;

static int s3_surface_plot(void S3_FAR *context, S3_I16 x, S3_I16 y)
{
    S3_SURFACE_PLOT S3_FAR *plot = (S3_SURFACE_PLOT S3_FAR *)context;
    return S3_SurfacePutPixel(plot->surface, x, y, plot->color);
}

int S3_SurfaceDrawLineClipped(S3_SURFACE S3_FAR *surface,
                              S3_I16 x0, S3_I16 y0,
                              S3_I16 x1, S3_I16 y1,
                              const S3_RECT16 S3_FAR *clip_rect,
                              S3_U8 color)
{
    S3_SURFACE_PLOT plot;
    S3_I32 left, top, right, bottom;
    if (surface == (S3_SURFACE S3_FAR *)0 || surface->width == 0 ||
        surface->height == 0 || surface->pitch < surface->width ||
        surface->window == (S3_U8 S3_FAR *)0 || surface->map_bank == 0 ||
        surface->bank_bytes == 0 || surface->aperture_bytes < surface->bank_bytes) {
        return 0;
    }

    left = 0;
    top = 0;
    right = surface->width;
    bottom = surface->height;
    if (clip_rect != (const S3_RECT16 S3_FAR *)0) {
        if (clip_rect->right <= clip_rect->left ||
            clip_rect->bottom <= clip_rect->top) {
            return 1;
        }
        if (clip_rect->left > left) left = clip_rect->left;
        if (clip_rect->top > top) top = clip_rect->top;
        if (clip_rect->right < right) right = clip_rect->right;
        if (clip_rect->bottom < bottom) bottom = clip_rect->bottom;
    }
    plot.surface = surface;
    plot.color = color;
    return S3_LineVisit(x0, y0, x1, y1, left, top, right, bottom,
                        s3_surface_plot, &plot);
}

int S3_SurfaceGetPixel(S3_SURFACE S3_FAR *surface,
                       S3_I16 x, S3_I16 y, S3_U8 S3_FAR *color)
{
    S3_U32 offset, bank, in_bank;
    if (surface == (S3_SURFACE S3_FAR *)0 || color == (S3_U8 S3_FAR *)0 ||
        surface->bank_bytes == 0 || surface->aperture_bytes < surface->bank_bytes ||
        surface->pitch < surface->width || surface->window == (S3_U8 S3_FAR *)0 ||
        surface->map_bank == 0 ||
        x < 0 || y < 0 || (S3_U16)x >= surface->width ||
        (S3_U16)y >= surface->height) {
        return 0;
    }
    offset = (S3_U32)(S3_U16)y * (S3_U32)surface->pitch + (S3_U16)x;
    if (offset >= surface->framebuffer_bytes) {
        return 0;
    }
    bank = offset / surface->bank_bytes;
    in_bank = offset % surface->bank_bytes;
    if (in_bank >= surface->aperture_bytes || !s3_map(surface, bank)) {
        return 0;
    }
    *color = surface->window[in_bank];
    return 1;
}

int S3_SurfaceBlitDIB8(S3_SURFACE S3_FAR *destination,
                       S3_I16 destination_x, S3_I16 destination_y,
                       S3_DIB8_VIEW S3_FAR *source,
                       S3_I16 source_x, S3_I16 source_y,
                       S3_U16 width, S3_U16 height, S3_U32 rop3)
{
    S3_U32 required_bytes;
    S3_I32 left, top, right, bottom;
    S3_I32 x, y;
    if (destination == (S3_SURFACE S3_FAR *)0 ||
        source == (S3_DIB8_VIEW S3_FAR *)0 ||
        source->bits == (S3_U8 S3_FAR *)0 ||
        source->width == 0 || source->height == 0 ||
        source->width > 32767U || source->height > 32767U ||
        destination->width > 32767U || destination->height > 32767U ||
        source->stride < source->width || source->top_down > 1 ||
        rop3 != S3_ROP3_SRCCOPY) {
        return 0;
    }
    required_bytes = (S3_U32)source->stride * (S3_U32)source->height;
    if (required_bytes > source->data_bytes) {
        return 0;
    }
    if (width == 0 || height == 0) {
        return 1;
    }

    left = 0;
    top = 0;
    right = (S3_I32)width;
    bottom = (S3_I32)height;
    if (source_x < 0 && -(S3_I32)source_x > left) left = -(S3_I32)source_x;
    if (destination_x < 0 && -(S3_I32)destination_x > left) {
        left = -(S3_I32)destination_x;
    }
    if (source_y < 0 && -(S3_I32)source_y > top) top = -(S3_I32)source_y;
    if (destination_y < 0 && -(S3_I32)destination_y > top) {
        top = -(S3_I32)destination_y;
    }
    if ((S3_I32)source_x + right > source->width) {
        right = (S3_I32)source->width - source_x;
    }
    if ((S3_I32)destination_x + right > destination->width) {
        right = (S3_I32)destination->width - destination_x;
    }
    if ((S3_I32)source_y + bottom > source->height) {
        bottom = (S3_I32)source->height - source_y;
    }
    if ((S3_I32)destination_y + bottom > destination->height) {
        bottom = (S3_I32)destination->height - destination_y;
    }
    if (left >= right || top >= bottom) {
        return 1;
    }

    for (y = top; y < bottom; ++y) {
        S3_I32 logical_y = (S3_I32)source_y + y;
        S3_I32 memory_y = source->top_down ? logical_y :
                          (S3_I32)source->height - 1 - logical_y;
        S3_U32 row_offset = (S3_U32)memory_y * source->stride;
        for (x = left; x < right; ++x) {
            S3_I32 source_column = (S3_I32)source_x + x;
            S3_U32 source_offset = row_offset + (S3_U32)source_column;
            S3_I32 dest_x = (S3_I32)destination_x + x;
            S3_I32 dest_y = (S3_I32)destination_y + y;
            if (source_offset >= source->data_bytes ||
                !S3_SurfacePutPixel(destination, (S3_I16)dest_x,
                                    (S3_I16)dest_y, source->bits[source_offset])) {
                return 0;
            }
        }
    }
    return 1;
}

S3_U16 S3_SurfaceSetDIB8Band(S3_SURFACE S3_FAR *destination,
                             S3_I32 destination_x, S3_I32 destination_y,
                             S3_U16 bitmap_width, S3_U16 bitmap_height,
                             S3_U16 stride, S3_U16 scan_start,
                             S3_U16 scan_count,
                             S3_U8 S3_FAR *bits, S3_U32 data_bytes,
                             S3_U16 segment_offset,
                             S3_I16 S3_FAR *translate,
                             S3_U16 translate_entries,
                             S3_RECT16 S3_FAR *clip_rect)
{
    S3_U32 required_bytes;
    S3_U16 row, index, rows_copied;
    S3_I32 left, right;

    if (destination == (S3_SURFACE S3_FAR *)0 || bits == (S3_U8 S3_FAR *)0 ||
        translate == (S3_I16 S3_FAR *)0 || translate_entries < 256 ||
        bitmap_width == 0 || bitmap_width > 32767U || bitmap_height == 0 ||
        bitmap_height > 32767U || stride < bitmap_width || (stride & 3U) != 0 ||
        scan_count == 0 || (S3_U32)scan_start + scan_count > bitmap_height ||
        destination->width > 32767U || destination->height > 32767U ||
        destination_x < -32767L || destination_x > 32767L ||
        destination_y < -32767L || destination_y > 32767L) {
        return 0;
    }
    required_bytes = (S3_U32)stride * (S3_U32)scan_count;
    if (required_bytes > data_bytes ||
        !S3_FarSpanFitsSingleSegment(segment_offset, required_bytes)) {
        return 0;
    }
    if (clip_rect != (S3_RECT16 S3_FAR *)0 &&
        (clip_rect->right <= clip_rect->left ||
         clip_rect->bottom <= clip_rect->top)) {
        return 0;
    }
    for (index = 0; index < 256; ++index) {
        if (translate[index] < 0 || translate[index] > 255) {
            return 0;
        }
    }

    left = 0;
    right = (S3_I32)bitmap_width;
    if (destination_x < 0 && -destination_x > left) left = -destination_x;
    if (destination_x + right > destination->width) {
        right = (S3_I32)destination->width - destination_x;
    }
    if (clip_rect != (S3_RECT16 S3_FAR *)0) {
        if ((S3_I32)clip_rect->left - destination_x > left) {
            left = (S3_I32)clip_rect->left - destination_x;
        }
        if ((S3_I32)clip_rect->right - destination_x < right) {
            right = (S3_I32)clip_rect->right - destination_x;
        }
    }
    if (left < 0) left = 0;
    if (right > bitmap_width) right = bitmap_width;
    if (left >= right) return 0;

    rows_copied = 0;
    for (row = 0; row < scan_count; ++row) {
        S3_U32 row_offset = (S3_U32)row * stride;
        S3_I32 image_scan = (S3_I32)scan_start + row;
        S3_I32 device_y = destination_y + (S3_I32)bitmap_height - 1 - image_scan;
        S3_I32 column;
        int y_visible = device_y >= 0 && device_y < destination->height;
        if (clip_rect != (S3_RECT16 S3_FAR *)0 &&
            (device_y < clip_rect->top || device_y >= clip_rect->bottom)) {
            y_visible = 0;
        }
        if (!y_visible) continue;
        for (column = left; column < right; ++column) {
            S3_U8 source_index = bits[row_offset + (S3_U32)column];
            if (!S3_SurfacePutPixel(destination,
                                    (S3_I16)(destination_x + column),
                                    (S3_I16)device_y,
                                    (S3_U8)translate[source_index])) {
                return 0;
            }
        }
        ++rows_copied;
    }
    return rows_copied;
}

int S3_SurfaceCopyRect(S3_SURFACE S3_FAR *destination,
                       S3_I16 destination_x, S3_I16 destination_y,
                       S3_SURFACE S3_FAR *source,
                       S3_I16 source_x, S3_I16 source_y,
                       S3_U16 width, S3_U16 height)
{
    S3_I32 left, top, right, bottom;
    S3_I32 src_x, src_y, dst_x, dst_y;
    S3_I32 row, column, row_begin, row_end, row_step;
    S3_I32 column_begin, column_end, column_step;
    if (destination == (S3_SURFACE S3_FAR *)0 ||
        source == (S3_SURFACE S3_FAR *)0) {
        return 0;
    }
    if (width == 0 || height == 0) {
        return 1;
    }

    left = 0;
    top = 0;
    right = (S3_I32)width;
    bottom = (S3_I32)height;
    if (source_x < 0 && -(S3_I32)source_x > left) left = -(S3_I32)source_x;
    if (destination_x < 0 && -(S3_I32)destination_x > left) {
        left = -(S3_I32)destination_x;
    }
    if (source_y < 0 && -(S3_I32)source_y > top) top = -(S3_I32)source_y;
    if (destination_y < 0 && -(S3_I32)destination_y > top) {
        top = -(S3_I32)destination_y;
    }
    if ((S3_I32)source_x + right > source->width) {
        right = (S3_I32)source->width - source_x;
    }
    if ((S3_I32)destination_x + right > destination->width) {
        right = (S3_I32)destination->width - destination_x;
    }
    if ((S3_I32)source_y + bottom > source->height) {
        bottom = (S3_I32)source->height - source_y;
    }
    if ((S3_I32)destination_y + bottom > destination->height) {
        bottom = (S3_I32)destination->height - destination_y;
    }
    if (left >= right || top >= bottom) {
        return 1;
    }

    src_x = (S3_I32)source_x + left;
    src_y = (S3_I32)source_y + top;
    dst_x = (S3_I32)destination_x + left;
    dst_y = (S3_I32)destination_y + top;
    row_begin = 0;
    row_end = bottom - top;
    row_step = 1;
    if (destination == source && dst_y > src_y) {
        row_begin = row_end - 1;
        row_end = -1;
        row_step = -1;
    }
    column_begin = 0;
    column_end = right - left;
    column_step = 1;
    if (destination == source && dst_y == src_y && dst_x > src_x) {
        column_begin = column_end - 1;
        column_end = -1;
        column_step = -1;
    }

    for (row = row_begin; row != row_end; row += row_step) {
        for (column = column_begin; column != column_end; column += column_step) {
            S3_U8 pixel;
            if (!S3_SurfaceGetPixel(source, (S3_I16)(src_x + column),
                                    (S3_I16)(src_y + row), &pixel) ||
                !S3_SurfacePutPixel(destination, (S3_I16)(dst_x + column),
                                    (S3_I16)(dst_y + row), pixel)) {
                return 0;
            }
        }
    }
    return 1;
}

int S3_SurfaceFillRect(S3_SURFACE S3_FAR *surface,
                       S3_I16 left, S3_I16 top,
                       S3_I16 right, S3_I16 bottom,
                       S3_U8 color)
{
    S3_I16 x, y;
    if (surface == (S3_SURFACE S3_FAR *)0 || left < 0 || top < 0 ||
        right < left || bottom < top ||
        (S3_U16)right > surface->width ||
        (S3_U16)bottom > surface->height) {
        return 0;
    }
    if (left == right || top == bottom) {
        return 1;
    }
    for (y = top; y < bottom; ++y) {
        for (x = left; x < right; ++x) {
            if (!S3_SurfacePutPixel(surface, x, y, color)) {
                return 0;
            }
        }
    }
    return 1;
}

int S3_SurfaceFillRectClipped(S3_SURFACE S3_FAR *surface,
                              S3_I16 left, S3_I16 top,
                              S3_I16 right, S3_I16 bottom,
                              S3_U8 color)
{
    if (surface == (S3_SURFACE S3_FAR *)0 || right < left || bottom < top) {
        return 0;
    }
    if (right <= 0 || bottom <= 0 || left >= (S3_I16)surface->width ||
        top >= (S3_I16)surface->height || left == right || top == bottom) {
        return 1;
    }
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > (S3_I16)surface->width) right = (S3_I16)surface->width;
    if (bottom > (S3_I16)surface->height) bottom = (S3_I16)surface->height;
    if (left >= right || top >= bottom) {
        return 1;
    }
    return S3_SurfaceFillRect(surface, left, top, right, bottom, color);
}
