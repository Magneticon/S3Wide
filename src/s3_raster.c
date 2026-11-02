#include "s3_raster.h"
#include "s3_pixel.h"
#include "s3_trace.h"
#include "s3_copy.h"
#if !defined(S3_HOST_TEST)
#include <i86.h>
#endif
#define s3_fmemcpy S3_COPY
#define s3_fmemset S3_FILL

/* Widest run processed per pass; wider blits are split into chunks. */
#define S3_ROW_MAX S3_ROW_PIXELS_MAX

static S3_U8 S3_FAR_DATA s3_dst_row[S3_ROW_BYTES_MAX];
static S3_U8 S3_FAR_DATA s3_src_row[S3_ROW_BYTES_MAX];
static S3_U8 S3_FAR_DATA s3_pat_row[S3_ROW_BYTES_MAX];

int S3_BindTarget(void S3_FAR *device, S3_TARGET S3_FAR *target)
{
    const S3_PDEV S3_FAR *pdev = (const S3_PDEV S3_FAR *)device;
    const S3_BITMAP16 S3_FAR *bitmap = (const S3_BITMAP16 S3_FAR *)device;
    S3_SURFACE S3_FAR *surface;
    S3_U32 bytes;
    S3_U16 stride, height;

    if (device == (void S3_FAR *)0 || target == (S3_TARGET S3_FAR *)0) {
        return 0;
    }
    if (pdev->pdType == S3_PDEV_DISPLAY) {
        surface = (S3_SURFACE S3_FAR *)pdev->surface_state;
        if (surface == (S3_SURFACE S3_FAR *)0) return 0;
        target->kind = S3_TARGET_SCREEN;
        target->width = surface->width;
        target->height = surface->height;
        target->stride = surface->pitch;
        target->bytes_pixel = surface->bytes_pixel ? surface->bytes_pixel : 1;
        target->segment_index = 0;
        target->scan_segment = 0;
        target->surface = surface;
        target->bits = (S3_U8 S3_FAR *)0;
        return 1;
    }
    if (bitmap->bmType != 0 || bitmap->bmWidth <= 0 || bitmap->bmHeight <= 0 ||
        bitmap->bmWidthBytes <= 0 || bitmap->bmPlanes != 1 ||
        bitmap->bmBits == (S3_U8 S3_FAR *)0) {
        return 0;
    }
    stride = (S3_U16)bitmap->bmWidthBytes;
    height = (S3_U16)bitmap->bmHeight;
    if (bitmap->bmSegmentIndex != 0) {
        /* Huge bitmap: each segment holds bmScanSegment whole rows. */
        if (bitmap->bmScanSegment <= 0 ||
            (S3_U32)(S3_U16)bitmap->bmScanSegment * stride > 65536UL) {
            return 0;
        }
#if defined(S3_HOST_TEST)
        return 0; /* host tests use flat single-segment bitmaps */
#endif
    } else {
        bytes = (S3_U32)stride * height;
        if (bytes > 65536UL) return 0;
#if !defined(S3_HOST_TEST)
        if (!S3_FarSpanFitsSingleSegment((S3_U16)FP_OFF(bitmap->bmBits), bytes)) {
            return 0;
        }
#endif
    }
    if (bitmap->bmBitsPixel == s3_mode.bits_pixel &&
        (S3_U32)stride >= (S3_U32)(S3_U16)bitmap->bmWidth * s3_mode.bytes_pixel) {
        target->kind = S3_TARGET_MEMC;
        target->bytes_pixel = s3_mode.bytes_pixel;
    } else if (bitmap->bmBitsPixel == 1 &&
               (S3_U32)stride * 8UL >= (S3_U32)(S3_U16)bitmap->bmWidth) {
        target->kind = S3_TARGET_MEM1;
        target->bytes_pixel = 1;
    } else {
        return 0;
    }
    target->width = (S3_U16)bitmap->bmWidth;
    target->height = height;
    target->stride = stride;
    target->segment_index = (S3_U16)bitmap->bmSegmentIndex;
    target->scan_segment = (S3_U16)bitmap->bmScanSegment;
    target->surface = (S3_SURFACE S3_FAR *)0;
    target->bits = bitmap->bmBits;
    return 1;
}

/* First byte of row y of a memory target. */
static S3_U8 S3_FAR *s3_mem_row(const S3_TARGET S3_FAR *target, S3_U16 y)
{
#if !defined(S3_HOST_TEST)
    if (target->segment_index != 0) {
        S3_U16 segment = (S3_U16)(y / target->scan_segment);
        S3_U16 within = (S3_U16)(y % target->scan_segment);
        return (S3_U8 S3_FAR *)MK_FP(
            FP_SEG(target->bits) + segment * target->segment_index,
            FP_OFF(target->bits) + within * target->stride);
    }
    return target->bits + (S3_U16)(y * target->stride);
#else
    return target->bits + (S3_U32)y * target->stride;
#endif
}

S3_U8 S3_FAR *S3_TargetPixel(const S3_TARGET S3_FAR *target, S3_I16 x, S3_I16 y)
{
    S3_U16 contiguous;
    switch (target->kind) {
    case S3_TARGET_SCREEN:
        /* Pitches are whole pixels, so a pixel never straddles a bank. */
        return S3_SurfaceSpan(target->surface, x, y, &contiguous);
    case S3_TARGET_MEMC:
        return s3_mem_row(target, (S3_U16)y) + (S3_U16)x * target->bytes_pixel;
    default:
        return (S3_U8 S3_FAR *)0;
    }
}

int S3_TargetReadRow(const S3_TARGET S3_FAR *target, S3_I16 x, S3_I16 y,
                     S3_U16 count, S3_U8 S3_FAR *out)
{
    S3_U8 S3_FAR *span;
    const S3_U8 S3_FAR *row;
    S3_U16 contiguous, run, i, column, bp = target->bytes_pixel;

    switch (target->kind) {
    case S3_TARGET_SCREEN:
        while (count != 0) {
            span = S3_SurfaceSpan(target->surface, x, y, &contiguous);
            if (span == (S3_U8 S3_FAR *)0) return 0;
            run = (S3_U16)(contiguous / bp);
            if (run > count) run = count;
            if (run == 0) return 0;
            s3_fmemcpy(out, span, run * bp);
            out += run * bp;
            x = (S3_I16)(x + run);
            count = (S3_U16)(count - run);
        }
        return 1;
    case S3_TARGET_MEMC:
        s3_fmemcpy(out, s3_mem_row(target, (S3_U16)y) + (S3_U16)x * bp, count * bp);
        return 1;
    case S3_TARGET_MEM1:
        row = s3_mem_row(target, (S3_U16)y);
        for (i = 0; i < count; ++i) {
            column = (S3_U16)(x + i);
            out[i] = (row[column >> 3] & (0x80U >> (column & 7))) ? 0xFF : 0x00;
        }
        return 1;
    default:
        return 0;
    }
}

int S3_TargetWriteRow(const S3_TARGET S3_FAR *target, S3_I16 x, S3_I16 y,
                      S3_U16 count, const S3_U8 S3_FAR *in)
{
    S3_U8 S3_FAR *span;
    S3_U8 S3_FAR *row;
    S3_U16 contiguous, run, i, column, bp = target->bytes_pixel;
    S3_U8 bit;

    switch (target->kind) {
    case S3_TARGET_SCREEN:
        while (count != 0) {
            span = S3_SurfaceSpan(target->surface, x, y, &contiguous);
            if (span == (S3_U8 S3_FAR *)0) return 0;
            run = (S3_U16)(contiguous / bp);
            if (run > count) run = count;
            if (run == 0) return 0;
            s3_fmemcpy(span, in, run * bp);
            in += run * bp;
            x = (S3_I16)(x + run);
            count = (S3_U16)(count - run);
        }
        return 1;
    case S3_TARGET_MEMC:
        s3_fmemcpy(s3_mem_row(target, (S3_U16)y) + (S3_U16)x * bp, in, count * bp);
        return 1;
    case S3_TARGET_MEM1:
        row = s3_mem_row(target, (S3_U16)y);
        for (i = 0; i < count; ++i) {
            column = (S3_U16)(x + i);
            bit = (S3_U8)(0x80U >> (column & 7));
            if (in[i] & 0x80) {
                row[column >> 3] = (S3_U8)(row[column >> 3] | bit);
            } else {
                row[column >> 3] = (S3_U8)(row[column >> 3] & (S3_U8)~bit);
            }
        }
        return 1;
    default:
        return 0;
    }
}

S3_U32 S3_Rop3Long(S3_U8 rop, S3_U32 pattern, S3_U32 source,
                   S3_U32 destination)
{
    S3_U32 p = pattern, s = source, d = destination, result = 0;
    S3_U32 np = ~p, ns = ~s, nd = ~d;

    if (rop & 0x01) result |= np & ns & nd;
    if (rop & 0x02) result |= np & ns & d;
    if (rop & 0x04) result |= np & s & nd;
    if (rop & 0x08) result |= np & s & d;
    if (rop & 0x10) result |= p & ns & nd;
    if (rop & 0x20) result |= p & ns & d;
    if (rop & 0x40) result |= p & s & nd;
    if (rop & 0x80) result |= p & s & d;
    return result;
}

S3_U8 S3_Rop3(S3_U8 rop, S3_U8 pattern, S3_U8 source, S3_U8 destination)
{
    return (S3_U8)S3_Rop3Long(rop, pattern, source, destination);
}

/* d = rop(p, s, d) over bytes bytes of row buffers. A ROP3 is bitwise, so
   it applies to raw bytes whatever the pixel size. An operand the ROP does
   not use may be NULL; it then reads d, which cannot change the result.
   Dwords at a time (native with -3), common ROPs unrolled. */
#define S3_ROP_LOOP(expr) \
    for (i = 0; i < words; ++i) { \
        S3_U32 D = dw[i], S = sw[i], P = pw[i]; \
        (void)D; (void)S; (void)P; \
        dw[i] = (S3_U32)(expr); \
    } \
    break

void S3_RopBytes(S3_U8 rop, S3_U8 S3_FAR *d, const S3_U8 S3_FAR *p,
                 const S3_U8 S3_FAR *s, S3_U16 bytes)
{
    S3_U16 i, words = (S3_U16)(bytes / 4);
    S3_U32 S3_FAR *dw;
    const S3_U32 S3_FAR *pw;
    const S3_U32 S3_FAR *sw;

    if (p == (const S3_U8 S3_FAR *)0) p = d;
    if (s == (const S3_U8 S3_FAR *)0) s = d;
    dw = (S3_U32 S3_FAR *)d;
    pw = (const S3_U32 S3_FAR *)p;
    sw = (const S3_U32 S3_FAR *)s;

    /* The common ROPs as one or two passes of a dword row operation. */
    switch (rop) {
    case 0x55: S3_RowOp(S3_ROWOP_NOT, dw, dw, words); goto tail;
    case 0x5A: S3_RowOp(S3_ROWOP_XOR, dw, pw, words); goto tail;
    case 0xA0: S3_RowOp(S3_ROWOP_AND, dw, pw, words); goto tail;
    case 0xFA: S3_RowOp(S3_ROWOP_OR, dw, pw, words); goto tail;
    case 0x0A: S3_RowOp(S3_ROWOP_ANDN, dw, pw, words); goto tail;
    case 0x0F: S3_RowOp(S3_ROWOP_COPYN, dw, pw, words); goto tail;
    case 0xF0: s3_fmemcpy(dw, pw, words * 4); goto tail;
    case 0xCC: s3_fmemcpy(dw, sw, words * 4); goto tail;
    case 0x33: S3_RowOp(S3_ROWOP_COPYN, dw, sw, words); goto tail;
    case 0x66: S3_RowOp(S3_ROWOP_XOR, dw, sw, words); goto tail;
    case 0x88: S3_RowOp(S3_ROWOP_AND, dw, sw, words); goto tail;
    case 0xEE: S3_RowOp(S3_ROWOP_OR, dw, sw, words); goto tail;
    case 0x22: S3_RowOp(S3_ROWOP_ANDN, dw, sw, words); goto tail;
    case 0xBB: S3_RowOp(S3_ROWOP_ORN, dw, sw, words); goto tail;
    case 0x44: /* S & ~D */
        S3_RowOp(S3_ROWOP_NOT, dw, dw, words);
        S3_RowOp(S3_ROWOP_AND, dw, sw, words);
        goto tail;
    case 0x11: /* ~(D | S) */
        S3_RowOp(S3_ROWOP_OR, dw, sw, words);
        S3_RowOp(S3_ROWOP_NOT, dw, dw, words);
        goto tail;
    case 0x99: /* ~(D ^ S) */
        S3_RowOp(S3_ROWOP_XOR, dw, sw, words);
        S3_RowOp(S3_ROWOP_NOT, dw, dw, words);
        goto tail;
    default:
        break;
    }
    switch (rop) {
    case 0x00: S3_ROP_LOOP(0);
    case 0xFF: S3_ROP_LOOP(0xFFFFFFFFUL);
    case 0xC0: S3_ROP_LOOP(P & S);               /* MERGECOPY */
    case 0xFB: S3_ROP_LOOP(P | ~S | D);          /* PATPAINT */
    case 0xB8: S3_ROP_LOOP(((D ^ P) & S) ^ P);   /* S ? D : P */
    case 0xE2: S3_ROP_LOOP(((D ^ P) & S) ^ D);   /* S ? P : D */
    case 0xAC: S3_ROP_LOOP(((D ^ S) & P) ^ S);   /* P ? D : S */
    case 0xCA: S3_ROP_LOOP(((D ^ S) & P) ^ D);   /* P ? S : D */
    default:   S3_ROP_LOOP(S3_Rop3Long(rop, P, S, D));
    }
tail:
    for (i = (S3_U16)(words * 4); i < bytes; ++i) {
        d[i] = S3_Rop3(rop, p[i], s[i], d[i]);
    }
}
#undef S3_ROP_LOOP

void S3_FillPixels(S3_U8 S3_FAR *out, S3_U32 pixel, S3_U16 count,
                   S3_U16 bytes_pixel)
{
    S3_U16 done;
    if (count == 0) return;
    if (bytes_pixel == 1) {
        s3_fmemset(out, (S3_U8)pixel, count);
        return;
    }
    /* Store one pixel, then double the filled prefix. */
    S3_PixelStore(out, pixel, bytes_pixel);
    for (done = 1; done < count; done = (S3_U16)(done * 2)) {
        S3_U16 n = (S3_U16)(count - done < done ? count - done : done);
        s3_fmemcpy(out + done * bytes_pixel, out, n * bytes_pixel);
    }
}

/* Brush row for target row y, pixels x .. x+count-1, in target format.
   The pattern repeats every 8 pixels, so 8 are built and then doubled.
   A monochrome pattern brush takes the DRAWMODE colours when there is one
   (with_mode): 1 bits background, 0 bits foreground (text colour). */
static void s3_pattern_row(const S3_PBRUSH16 S3_FAR *brush, S3_U16 kind,
                           S3_U16 bp, S3_I16 x, S3_I16 y, S3_U16 count,
                           int with_mode, S3_U32 background, S3_U32 foreground)
{
    S3_U16 i, done, n = count < 8 ? count : 8;
    S3_U32 p;
    for (i = 0; i < n; ++i) {
        if (brush->mono && with_mode) {
            p = (brush->mask[(S3_U16)y & 7] & (0x80U >> ((S3_U16)(x + i) & 7)))
                    ? background : foreground;
        } else {
            p = brush->color[(((S3_U16)y & 7) << 3) | ((S3_U16)(x + i) & 7)];
        }
        if (kind == S3_TARGET_MEM1) p = S3_PixelIsLight(p) ? 0xFF : 0x00;
        S3_PixelStore(s3_pat_row + i * bp, p, bp);
    }
    for (done = n; done < count; done = (S3_U16)(done * 2)) {
        S3_U16 m = (S3_U16)(count - done < done ? count - done : done);
        s3_fmemcpy(s3_pat_row + done * bp, s3_pat_row, m * bp);
    }
}

static int s3_clip(S3_I16 S3_FAR *dx, S3_I16 S3_FAR *dy,
                   S3_I16 S3_FAR *sx, S3_I16 S3_FAR *sy,
                   S3_I16 S3_FAR *width, S3_I16 S3_FAR *height,
                   const S3_TARGET S3_FAR *destination,
                   const S3_TARGET S3_FAR *source)
{
    if (*dx < 0) { *width = (S3_I16)(*width + *dx); *sx = (S3_I16)(*sx - *dx); *dx = 0; }
    if (*dy < 0) { *height = (S3_I16)(*height + *dy); *sy = (S3_I16)(*sy - *dy); *dy = 0; }
    if (source != (const S3_TARGET S3_FAR *)0) {
        if (*sx < 0) { *width = (S3_I16)(*width + *sx); *dx = (S3_I16)(*dx - *sx); *sx = 0; }
        if (*sy < 0) { *height = (S3_I16)(*height + *sy); *dy = (S3_I16)(*dy - *sy); *sy = 0; }
        if ((S3_I32)*sx + *width > (S3_I32)source->width)
            *width = (S3_I16)(source->width - (S3_U16)*sx);
        if ((S3_I32)*sy + *height > (S3_I32)source->height)
            *height = (S3_I16)(source->height - (S3_U16)*sy);
    }
    if ((S3_I32)*dx + *width > (S3_I32)destination->width)
        *width = (S3_I16)(destination->width - (S3_U16)*dx);
    if ((S3_I32)*dy + *height > (S3_I32)destination->height)
        *height = (S3_I16)(destination->height - (S3_U16)*dy);
    return *width > 0 && *height > 0;
}

/* rop applied in place to count pixels of colour-target row y from x. */
static int s3_rop_target_row(const S3_TARGET S3_FAR *target, S3_I16 x, S3_I16 y,
                             S3_U16 count, S3_U8 rop, const S3_U8 S3_FAR *p,
                             const S3_U8 S3_FAR *s)
{
    S3_U8 S3_FAR *span;
    S3_U16 contiguous, run, bp = target->bytes_pixel;

    if (target->kind == S3_TARGET_MEMC) {
        S3_RopBytes(rop, s3_mem_row(target, (S3_U16)y) + (S3_U16)x * bp, p, s,
                    (S3_U16)(count * bp));
        return 1;
    }
    while (count != 0) {
        span = S3_SurfaceSpan(target->surface, x, y, &contiguous);
        if (span == (S3_U8 S3_FAR *)0) return 0;
        run = (S3_U16)(contiguous / bp);
        if (run > count) run = count;
        if (run == 0) return 0;
        S3_RopBytes(rop, span, p, s, (S3_U16)(run * bp));
        if (p != (const S3_U8 S3_FAR *)0) p += run * bp;
        if (s != (const S3_U8 S3_FAR *)0) s += run * bp;
        x = (S3_I16)(x + run);
        count = (S3_U16)(count - run);
    }
    return 1;
}

/* Constant fill: one prepared row copied into every row of the rectangle. */
static int s3_fill_rows(const S3_TARGET S3_FAR *target, S3_I16 x, S3_I16 y,
                        S3_I16 width, S3_I16 height, S3_U32 pixel)
{
    S3_I16 row, chunk;
    S3_I32 chunk_x;
    for (chunk_x = 0; chunk_x < width; chunk_x += S3_ROW_MAX) {
        chunk = (S3_I16)(width - chunk_x);
        if (chunk > S3_ROW_MAX) chunk = S3_ROW_MAX;
        S3_FillPixels(s3_dst_row, pixel, (S3_U16)chunk, target->bytes_pixel);
        for (row = 0; row < height; ++row) {
            if (!S3_TargetWriteRow(target, (S3_I16)(x + chunk_x), (S3_I16)(y + row),
                                   (S3_U16)chunk, s3_dst_row)) return 0;
        }
    }
    return 1;
}

/* Convert a source row between monochrome (one byte per pixel) and colour
   (bp bytes per pixel, the colour side's size) target formats, in place. Mono 1 = background,
   0 = text colour; colour -> mono is 1 where the pixel is the background. */
static void s3_convert_source(S3_U16 src_kind, S3_U16 dst_kind, S3_U16 count,
                              S3_U16 bp, S3_U32 background, S3_U32 foreground)
{
    S3_I16 i;
    if (src_kind == S3_TARGET_MEM1 && dst_kind != S3_TARGET_MEM1) {
        /* Expanding: walk backwards so unread bytes are not overwritten. */
        for (i = (S3_I16)count - 1; i >= 0; --i) {
            S3_PixelStore(s3_src_row + (S3_U16)i * bp,
                          s3_src_row[i] ? background : foreground, bp);
        }
    } else if (src_kind != S3_TARGET_MEM1 && dst_kind == S3_TARGET_MEM1) {
        for (i = 0; i < (S3_I16)count; ++i) {
            s3_src_row[i] = (S3_U8)(S3_PixelLoad(s3_src_row + (S3_U16)i * bp, bp) ==
                                    background ? 0xFF : 0x00);
        }
    }
}

int S3_RasterBitBlt(void S3_FAR *destination, S3_I16 dx, S3_I16 dy,
                    void S3_FAR *source, S3_I16 sx, S3_I16 sy,
                    S3_I16 width, S3_I16 height, S3_U32 rop3,
                    const S3_PBRUSH16 S3_FAR *brush,
                    const S3_DRAWMODE16 S3_FAR *mode)
{
    S3_TARGET dst, src;
    S3_U8 rop = (S3_U8)(rop3 >> 16);
    int use_p = (((rop >> 4) ^ rop) & 0x0F) != 0;
    int use_s = (((rop >> 2) ^ rop) & 0x33) != 0;
    int use_d = (((rop >> 1) ^ rop) & 0x55) != 0;
    S3_U32 background, foreground, p;
    S3_I16 row, first_row, step, chunk;
    S3_I32 chunk_x;
    S3_U16 chunks, k;
    int right_to_left;
    S3_I16 ty, sy_row;
    S3_U16 bp;
    int plain_copy, in_place;

    if (!S3_BindTarget(destination, &dst)) return 0;
    if (use_s && !S3_BindTarget(source, &src)) return 0;
    S3_TRACE_EVENT("blt.enter", rop, (dst.kind << 8) | (use_s ? src.kind : 0));
    S3_TRACE_EVENT("blt.dst", dst.stride, dst.height);
    S3_TRACE_EVENT("blt.rect", (S3_U16)dx, (S3_U16)dy);
    S3_TRACE_EVENT("blt.size", (S3_U16)width, (S3_U16)height);
    if (!s3_clip(&dx, &dy, &sx, &sy, &width, &height, &dst,
                 use_s ? &src : (S3_TARGET S3_FAR *)0)) {
        return 1; /* fully clipped */
    }
    if (use_p) {
        if (brush == (const S3_PBRUSH16 S3_FAR *)0) return 0;
        if (brush->style == S3_BS_HOLLOW) return 1;
    }
    background = S3_RGBToPixel(0x00FFFFFFUL);
    foreground = S3_RGBToPixel(0);
    if (mode != (const S3_DRAWMODE16 S3_FAR *)0) {
        background = S3_PhysicalToPixel(mode->bkColor);
        foreground = S3_PhysicalToPixel(mode->TextColor);
    }
    bp = dst.bytes_pixel;

    /* Constant results and solid pattern fills avoid the per-pixel path. */
    if (!use_s && !use_d && (!use_p || brush->style == S3_BS_SOLID)) {
        p = use_p ? brush->color[0] : 0;
        if (dst.kind == S3_TARGET_MEM1) p = S3_PixelIsLight(p) ? 0xFF : 0x00;
        return s3_fill_rows(&dst, dx, dy, width, height, S3_Rop3Long(rop, p, 0, 0));
    }

    /* Overlapping copies within one target must read rows before they are
       overwritten: walk bottom-up when the source lies above. Each source
       row is buffered before writing, which handles horizontal overlap. */
    first_row = 0;
    step = 1;
    if (use_s && src.kind == dst.kind && src.bits == dst.bits &&
        src.surface == dst.surface && sy < dy) {
        first_row = (S3_I16)(height - 1);
        step = -1;
    }
    /* ROPs that read the destination combine straight into a colour
       target (frame buffer or bitmap) instead of a copy of its row. Any
       source row is buffered first, so overlap is still safe. */
    in_place = use_d && dst.kind != S3_TARGET_MEM1;
    plain_copy = rop == 0xCC && use_s &&
                 (src.kind == S3_TARGET_MEM1) == (dst.kind == S3_TARGET_MEM1);

    /* Wider than one row buffer: chunks run right to left when a copy
       within one target moves pixels right, so no chunk overwrites source
       columns a later chunk still has to read. */
    right_to_left = use_s && src.kind == dst.kind && src.bits == dst.bits &&
                    src.surface == dst.surface && dx > sx;
    chunks = (S3_U16)(((S3_I32)width + S3_ROW_MAX - 1) / S3_ROW_MAX);
    for (k = 0; k < chunks; ++k) {
        chunk_x = (S3_I32)(right_to_left ? chunks - 1 - k : k) * S3_ROW_MAX;
        chunk = (S3_I16)(width - chunk_x);
        if (chunk > S3_ROW_MAX) chunk = S3_ROW_MAX;
        for (row = first_row; row >= 0 && row < height; row = (S3_I16)(row + step)) {
            ty = (S3_I16)(dy + row);
            if (use_s) {
                sy_row = (S3_I16)(sy + row);
                if (!S3_TargetReadRow(&src, (S3_I16)(sx + chunk_x), sy_row,
                                      (S3_U16)chunk, s3_src_row)) return 0;
                if (plain_copy) {
                    if (!S3_TargetWriteRow(&dst, (S3_I16)(dx + chunk_x), ty,
                                           (S3_U16)chunk, s3_src_row)) return 0;
                    continue;
                }
                s3_convert_source(src.kind, dst.kind, (S3_U16)chunk,
                                  src.kind == S3_TARGET_MEM1 ? bp : src.bytes_pixel,
                                  background, foreground);
            }
            if (use_d && !in_place) {
                if (!S3_TargetReadRow(&dst, (S3_I16)(dx + chunk_x), ty,
                                      (S3_U16)chunk, s3_dst_row)) return 0;
            }
            if (use_p) {
                s3_pattern_row(brush, dst.kind, bp, (S3_I16)(dx + chunk_x), ty,
                               (S3_U16)chunk, mode != (const S3_DRAWMODE16 S3_FAR *)0,
                               background, foreground);
                if (rop == 0xF0) {
                    /* PATCOPY with a pattern brush: the row is the result. */
                    if (!S3_TargetWriteRow(&dst, (S3_I16)(dx + chunk_x), ty,
                                           (S3_U16)chunk, s3_pat_row)) return 0;
                    continue;
                }
            }
            if (in_place) {
                if (!s3_rop_target_row(&dst, (S3_I16)(dx + chunk_x), ty, (S3_U16)chunk,
                                       rop, use_p ? s3_pat_row : (const S3_U8 S3_FAR *)0,
                                       use_s ? s3_src_row : (const S3_U8 S3_FAR *)0)) {
                    return 0;
                }
                continue;
            }
            S3_RopBytes(rop, s3_dst_row, use_p ? s3_pat_row : (const S3_U8 S3_FAR *)0,
                        use_s ? s3_src_row : (const S3_U8 S3_FAR *)0,
                        (S3_U16)(chunk * bp));
            if (!S3_TargetWriteRow(&dst, (S3_I16)(dx + chunk_x), ty,
                                   (S3_U16)chunk, s3_dst_row)) return 0;
        }
    }
    return 1;
}
