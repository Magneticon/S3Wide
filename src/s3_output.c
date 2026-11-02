#include "s3_output.h"
#include "s3_pixel.h"

#define S3_OUTPUT_ROW_MAX S3_ROW_PIXELS_MAX

static S3_U8 S3_FAR_DATA s3_span_row[S3_ROW_BYTES_MAX];

S3_U8 S3_Rop2ToRop3(S3_I16 rop2)
{
    S3_U16 function, i;
    S3_U8 rop3 = 0;
    if (rop2 < 1 || rop2 > 16) return 0xFF;
    /* Bit (P << 1 | D) of rop2 - 1 is the result for pen P and destination D. */
    function = (S3_U16)(rop2 - 1);
    for (i = 0; i < 8; ++i) {
        if ((function >> ((((i >> 2) & 1) << 1) | (i & 1))) & 1) {
            rop3 = (S3_U8)(rop3 | (1U << i));
        }
    }
    return rop3;
}

static S3_U32 s3_mono(S3_U32 pixel)
{
    return S3_PixelIsLight(pixel) ? 0xFF : 0x00;
}

static int s3_clip_rect(const S3_TARGET S3_FAR *target,
                        const S3_RECT16 S3_FAR *clip,
                        S3_I16 S3_FAR *left, S3_I16 S3_FAR *top,
                        S3_I16 S3_FAR *right, S3_I16 S3_FAR *bottom)
{
    *left = 0;
    *top = 0;
    *right = (S3_I16)target->width;
    *bottom = (S3_I16)target->height;
    if (clip != (const S3_RECT16 S3_FAR *)0) {
        if (clip->left > *left) *left = clip->left;
        if (clip->top > *top) *top = clip->top;
        if (clip->right < *right) *right = clip->right;
        if (clip->bottom < *bottom) *bottom = clip->bottom;
    }
    return *left < *right && *top < *bottom;
}

int S3_OutputScanlines(void S3_FAR *destination, S3_U16 count,
                       const S3_POINT16 S3_FAR *points,
                       const S3_PPEN16 S3_FAR *pen,
                       const S3_PBRUSH16 S3_FAR *brush,
                       const S3_DRAWMODE16 S3_FAR *mode,
                       const S3_RECT16 S3_FAR *clip)
{
    S3_TARGET target;
    S3_PBRUSH16 pen_brush;
    S3_U8 rop;
    S3_I16 left, top, right, bottom, y, xs, xe, x;
    S3_U16 i, k, bp;
    S3_U32 p, pen_pixel;
    S3_U8 mask;
    S3_U8 S3_FAR *at;
    int transparent;

    if (points == (const S3_POINT16 S3_FAR *)0 || count < 1 ||
        mode == (const S3_DRAWMODE16 S3_FAR *)0 ||
        !S3_BindTarget(destination, &target)) {
        return 0;
    }
    rop = S3_Rop2ToRop3(mode->Rop2);
    if (rop == 0xFF && mode->Rop2 != 16) return 0;
    if (brush == (const S3_PBRUSH16 S3_FAR *)0) {
        if (pen == (const S3_PPEN16 S3_FAR *)0) return 0;
        pen_brush.style = S3_BS_SOLID;
        pen_brush.has_mask = 0;
        pen_pixel = S3_PhysicalToPixel(pen->color);
        for (k = 0; k < S3_BRUSH_SIZE * S3_BRUSH_SIZE; ++k) pen_brush.color[k] = pen_pixel;
        for (k = 0; k < sizeof(pen_brush.mask); ++k) pen_brush.mask[k] = 0xFF;
        brush = &pen_brush;
    }
    if (brush->style == S3_BS_HOLLOW) return 1;
    if (!s3_clip_rect(&target, clip, &left, &top, &right, &bottom)) return 1;
    y = points[0].y;
    if (y < top || y >= bottom) return 1;
    transparent = brush->has_mask && mode->bkMode == 1; /* TRANSPARENT */
    bp = target.bytes_pixel;

    for (i = 1; i < count; ++i) {
        xs = points[i].x;
        xe = points[i].y;
        if (xs < left) xs = left;
        if (xe > right) xe = right;
        if (xs >= xe) continue;
        if (!transparent) {
            if (!S3_RasterBitBlt(destination, xs, y, (void S3_FAR *)0, 0, 0,
                                 (S3_I16)(xe - xs), 1, (S3_U32)rop << 16,
                                 brush, mode)) {
                return 0;
            }
            continue;
        }
        /* Transparent hatch: only foreground pixels of the mask change. */
        if (xe - xs > S3_OUTPUT_ROW_MAX) return 0;
        if (!S3_TargetReadRow(&target, xs, y, (S3_U16)(xe - xs), s3_span_row)) return 0;
        mask = brush->mask[(S3_U16)y & 7];
        for (x = xs; x < xe; ++x) {
            if (!(mask & (0x80U >> ((S3_U16)x & 7)))) continue;
            p = brush->color[(((S3_U16)y & 7) << 3) | ((S3_U16)x & 7)];
            if (target.kind == S3_TARGET_MEM1) p = s3_mono(p);
            at = s3_span_row + (S3_U16)(x - xs) * bp;
            S3_PixelStore(at, S3_Rop3Long(rop, p, 0, S3_PixelLoad(at, bp)), bp);
        }
        if (!S3_TargetWriteRow(&target, xs, y, (S3_U16)(xe - xs), s3_span_row)) return 0;
    }
    return 1;
}

typedef struct S3_ROP_PLOT {
    S3_TARGET target;
    S3_U32 color;
    S3_U8 rop;
    /* Last screen pixel plotted: neighbours step from its byte offset. */
    int have_last;
    S3_I16 last_x, last_y;
    S3_U32 last_offset;
} S3_ROP_PLOT;

/* Screen address of (x, y). A line's next pixel is a neighbour of the
   last one, so its offset is one add away and it is usually in the bank
   already mapped. */
static S3_U8 S3_FAR *s3_plot_screen(S3_ROP_PLOT S3_FAR *plot, S3_I16 x, S3_I16 y)
{
    S3_SURFACE S3_FAR *surface = plot->target.surface;
    S3_I16 step_x = (S3_I16)(x - plot->last_x), step_y = (S3_I16)(y - plot->last_y);
    S3_U32 offset;
    if (surface->bank_bytes != 65536UL) return S3_TargetPixel(&plot->target, x, y);
    if (plot->have_last && step_x >= -1 && step_x <= 1 && step_y >= -1 && step_y <= 1) {
        offset = plot->last_offset;
        if (step_x > 0) offset += plot->target.bytes_pixel;
        else if (step_x < 0) offset -= plot->target.bytes_pixel;
        if (step_y > 0) offset += surface->pitch;
        else if (step_y < 0) offset -= surface->pitch;
    } else {
        offset = (S3_U32)(S3_U16)y * surface->pitch +
                 (S3_U32)(S3_U16)x * plot->target.bytes_pixel;
    }
    plot->have_last = 1;
    plot->last_x = x;
    plot->last_y = y;
    plot->last_offset = offset;
    if ((offset >> 16) == surface->current_bank && surface->window != (S3_U8 S3_FAR *)0) {
        return surface->window + (S3_U16)offset;
    }
    return S3_TargetPixel(&plot->target, x, y);
}

static int s3_rop_plot(void S3_FAR *context, S3_I16 x, S3_I16 y)
{
    S3_ROP_PLOT S3_FAR *plot = (S3_ROP_PLOT S3_FAR *)context;
    S3_U8 pixel[4];
    S3_U16 bp = plot->target.bytes_pixel;
    S3_U8 S3_FAR *at = plot->target.kind == S3_TARGET_SCREEN
                           ? s3_plot_screen(plot, x, y)
                           : S3_TargetPixel(&plot->target, x, y);
    if (at != (S3_U8 S3_FAR *)0) {
        S3_PixelStore(at, plot->rop == 0xF0 ? plot->color
                                            : S3_Rop3Long(plot->rop, plot->color, 0,
                                                          S3_PixelLoad(at, bp)), bp);
        return 1;
    }
    if (!S3_TargetReadRow(&plot->target, x, y, 1, pixel)) return 0;
    S3_PixelStore(pixel, S3_Rop3Long(plot->rop, plot->color, 0,
                                     S3_PixelLoad(pixel, bp)), bp);
    return S3_TargetWriteRow(&plot->target, x, y, 1, pixel);
}

int S3_OutputPolyline(void S3_FAR *destination, S3_U16 count,
                      const S3_POINT16 S3_FAR *points, S3_U32 physical,
                      S3_I16 rop2, const S3_RECT16 S3_FAR *clip)
{
    S3_ROP_PLOT plot;
    S3_I16 left, top, right, bottom;
    S3_U16 i;

    if (points == (const S3_POINT16 S3_FAR *)0 || count < 2 ||
        !S3_BindTarget(destination, &plot.target)) {
        return 0;
    }
    plot.have_last = 0;
    plot.last_x = plot.last_y = 0;
    plot.last_offset = 0;
    plot.rop = S3_Rop2ToRop3(rop2);
    if (plot.rop == 0xFF && rop2 != 16) return 0;
    plot.color = S3_PhysicalToPixel(physical);
    if (plot.target.kind == S3_TARGET_MEM1) plot.color = s3_mono(plot.color);
    if (!s3_clip_rect(&plot.target, clip, &left, &top, &right, &bottom)) return 1;
    for (i = 1; i < count; ++i) {
        if (!S3_LineVisit(points[i - 1].x, points[i - 1].y, points[i].x, points[i].y,
                          left, top, right, bottom, s3_rop_plot, &plot)) {
            return 0;
        }
    }
    return 1;
}
