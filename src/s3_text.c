#include "s3_text.h"
#include "s3_pixel.h"
#if !defined(S3_HOST_TEST)
#include <i86.h>
#endif

#define S3_TEXT_ROW_MAX S3_ROW_PIXELS_MAX

static S3_U8 S3_FAR_DATA s3_text_row[S3_ROW_BYTES_MAX];

/* Per-character glyph, width and position, worked out once per call
   instead of once per scan row, for strings up to S3_TEXT_CACHE long. */
#define S3_TEXT_CACHE 256
static const S3_U8 S3_FAR *S3_FAR_DATA s3_cache_glyph[S3_TEXT_CACHE];
static S3_U16 S3_FAR_DATA s3_cache_width[S3_TEXT_CACHE];
static S3_I16 S3_FAR_DATA s3_cache_x[S3_TEXT_CACHE];

/* Font segment fields by offset from fsVersion. */
#define S3_FS_VERSION      0
#define S3_FS_PIXWIDTH     86
#define S3_FS_PIXHEIGHT    88
#define S3_FS_FIRSTCHAR    95
#define S3_FS_LASTCHAR     96
#define S3_FS_DEFAULTCHAR  97
#define S3_FS_BREAKCHAR    98
#define S3_FS_TABLE20      118
#define S3_FS_TABLE30      148

typedef struct S3_FONT {
    const S3_U8 S3_FAR *base;   /* font segment offset 0 (fsVersion) */
    S3_U16 version;
    S3_U16 height;
    S3_U8 first;
    S3_U8 last;
    S3_U8 default_char;         /* biased by first */
    S3_U8 break_char;           /* biased by first */
} S3_FONT;

static S3_U16 s3_word(const S3_U8 S3_FAR *p)
{
    return (S3_U16)(p[0] | ((S3_U16)p[1] << 8));
}

static int s3_font_open(const void S3_FAR *font_info, S3_FONT S3_FAR *font)
{
    const S3_U8 S3_FAR *info = (const S3_U8 S3_FAR *)font_info;
#if !defined(S3_HOST_TEST)
    if (font_info == (const void S3_FAR *)0 ||
        FP_OFF(font_info) < S3_FONT_INFO_OFFSET) {
        return 0;
    }
#else
    if (font_info == (const void S3_FAR *)0) return 0;
#endif
    font->base = info - S3_FONT_INFO_OFFSET;
    font->version = s3_word(font->base + S3_FS_VERSION);
    if (font->version != 0x0200 && font->version != 0x0300) return 0;
    font->height = s3_word(font->base + S3_FS_PIXHEIGHT);
    font->first = font->base[S3_FS_FIRSTCHAR];
    font->last = font->base[S3_FS_LASTCHAR];
    font->default_char = font->base[S3_FS_DEFAULTCHAR];
    font->break_char = font->base[S3_FS_BREAKCHAR];
    return font->height != 0 && font->last >= font->first;
}

/* Character-table index for a character, falling back to the default. */
static S3_U16 s3_font_index(const S3_FONT S3_FAR *font, S3_U8 c)
{
    if (c < font->first || c > font->last) return font->default_char;
    return (S3_U16)(c - font->first);
}

static S3_U16 s3_font_width(const S3_FONT S3_FAR *font, S3_U16 index)
{
    if (font->version == 0x0300) {
        return s3_word(font->base + S3_FS_TABLE30 + index * 6U);
    }
    return s3_word(font->base + S3_FS_TABLE20 + index * 4U);
}

static const S3_U8 S3_FAR *s3_font_glyph(const S3_FONT S3_FAR *font, S3_U16 index)
{
    const S3_U8 S3_FAR *entry;
    if (font->version == 0x0300) {
        entry = font->base + S3_FS_TABLE30 + index * 6U;
        /* 3.00 offsets are 32-bit but stay within one segment here. */
        return font->base + s3_word(entry + 2);
    }
    entry = font->base + S3_FS_TABLE20 + index * 4U;
    return font->base + s3_word(entry + 2);
}

/* Advance after a character: explicit widths, or the glyph width plus the
   DRAWMODE inter-character and justification (break) extra. */
static S3_I16 s3_advance(const S3_FONT S3_FAR *font, S3_U8 c, S3_I16 i,
                         const S3_I16 S3_FAR *char_widths,
                         const S3_DRAWMODE16 S3_FAR *mode,
                         S3_I16 S3_FAR *break_err)
{
    S3_U16 index = s3_font_index(font, c);
    S3_I16 advance;
    if (char_widths != (const S3_I16 S3_FAR *)0) return char_widths[i];
    advance = (S3_I16)s3_font_width(font, index);
    if (mode != (const S3_DRAWMODE16 S3_FAR *)0) {
        advance = (S3_I16)(advance + mode->CharExtra);
        if (index == font->break_char && mode->BreakCount > 0) {
            advance = (S3_I16)(advance + mode->BreakExtra);
            *break_err = (S3_I16)(*break_err - mode->BreakRem);
            if (*break_err <= 0) {
                ++advance;
                *break_err = (S3_I16)(*break_err + mode->BreakCount);
            }
        }
    }
    return advance;
}

/* A physical colour in target row format. */
static S3_U32 s3_target_color(const S3_TARGET S3_FAR *target, S3_U32 physical)
{
    S3_U32 pixel = S3_PhysicalToPixel(physical);
    if (target->kind != S3_TARGET_MEM1) return pixel;
    return S3_PixelIsLight(pixel) ? 0xFF : 0x00;
}

static void s3_intersect(S3_I16 S3_FAR *left, S3_I16 S3_FAR *top,
                         S3_I16 S3_FAR *right, S3_I16 S3_FAR *bottom,
                         const S3_RECT16 S3_FAR *rect)
{
    if (rect == (const S3_RECT16 S3_FAR *)0) return;
    if (rect->left > *left) *left = rect->left;
    if (rect->top > *top) *top = rect->top;
    if (rect->right < *right) *right = rect->right;
    if (rect->bottom < *bottom) *bottom = rect->bottom;
}

static int s3_fill_rows(const S3_TARGET S3_FAR *target, S3_I16 left, S3_I16 top,
                        S3_I16 right, S3_I16 bottom, S3_U32 color)
{
    S3_I16 y, x, n;
    if (left >= right) return 1;
    /* Spans wider than the row buffer (memory bitmaps) go in pieces. */
    n = (S3_I16)(right - left > S3_TEXT_ROW_MAX ? S3_TEXT_ROW_MAX : right - left);
    S3_FillPixels(s3_text_row, color, (S3_U16)n, target->bytes_pixel);
    for (y = top; y < bottom; ++y) {
        for (x = left; x < right; x = (S3_I16)(x + n)) {
            S3_U16 piece = (S3_U16)(right - x < n ? right - x : n);
            if (!S3_TargetWriteRow(target, x, y, piece, s3_text_row)) return 0;
        }
    }
    return 1;
}

S3_U32 S3_TextOut(void S3_FAR *device, S3_I16 x, S3_I16 y,
                  const S3_RECT16 S3_FAR *clip, const S3_U8 S3_FAR *text,
                  S3_I16 count, const void S3_FAR *font_info,
                  S3_DRAWMODE16 S3_FAR *mode,
                  const S3_I16 S3_FAR *char_widths,
                  const S3_RECT16 S3_FAR *opaque_rect, S3_U16 options)
{
    S3_FONT font;
    S3_TARGET target;
    S3_I16 i, extent = 0, break_err, row_y, px, glyph_x, left, top, right, bottom;
    S3_I16 cell_left, cell_right, first, last;
    S3_U16 index, width, column_bytes, bp;
    S3_U32 fg, bg;
    S3_U8 bits;
    const S3_U8 S3_FAR *glyph;
    int opaque, cached;

    if (count == 0) {
        /* No characters: only the opaque rectangle, a common way to fill a
           rectangle with the background colour (Word paints its toolbars,
           ruler and dialog panels so). No font or text is needed. */
        if ((options & S3_ETO_OPAQUE) && opaque_rect != (const S3_RECT16 S3_FAR *)0) {
            if (!S3_BindTarget(device, &target)) return 0;
            left = 0;
            top = 0;
            right = (S3_I16)target.width;
            bottom = (S3_I16)target.height;
            s3_intersect(&left, &top, &right, &bottom, clip);
            s3_intersect(&left, &top, &right, &bottom, opaque_rect);
            bg = s3_target_color(&target, mode != (S3_DRAWMODE16 S3_FAR *)0
                                              ? mode->bkColor : 0x00FFFFFFUL);
            if (top < bottom && !s3_fill_rows(&target, left, top, right, bottom, bg)) {
                return 0;
            }
        }
        return 1;
    }
    if (text == (const S3_U8 S3_FAR *)0 || !s3_font_open(font_info, &font)) {
        return 0;
    }
    if (count < 0) {
        /* Extent query: measure without consuming the justification error. */
        break_err = mode != (S3_DRAWMODE16 S3_FAR *)0 ? mode->BreakErr : 0;
        for (i = 0; i < (S3_I16)-count; ++i) {
            extent = (S3_I16)(extent + s3_advance(&font, text[i], i, char_widths,
                                                  mode, &break_err));
        }
        return ((S3_U32)font.height << 16) | (S3_U16)extent;
    }
    if (!S3_BindTarget(device, &target)) return 0;
    bp = target.bytes_pixel;

    left = 0;
    top = 0;
    right = (S3_I16)target.width;
    bottom = (S3_I16)target.height;
    s3_intersect(&left, &top, &right, &bottom, clip);
    fg = s3_target_color(&target, mode != (S3_DRAWMODE16 S3_FAR *)0 ? mode->TextColor : 0);
    bg = s3_target_color(&target, mode != (S3_DRAWMODE16 S3_FAR *)0 ? mode->bkColor
                                                                     : 0x00FFFFFFUL);
    if ((options & S3_ETO_OPAQUE) && opaque_rect != (const S3_RECT16 S3_FAR *)0) {
        S3_I16 ol = left, ot = top, or_ = right, ob = bottom;
        s3_intersect(&ol, &ot, &or_, &ob, opaque_rect);
        if (ot < ob && !s3_fill_rows(&target, ol, ot, or_, ob, bg)) return 0;
    }
    if ((options & S3_ETO_CLIPPED) && opaque_rect != (const S3_RECT16 S3_FAR *)0) {
        s3_intersect(&left, &top, &right, &bottom, opaque_rect);
    }
    opaque = mode != (S3_DRAWMODE16 S3_FAR *)0 && mode->bkMode == S3_BK_OPAQUE;

    /* Text cell extent along x, for the opaque background. */
    break_err = mode != (S3_DRAWMODE16 S3_FAR *)0 ? mode->BreakErr : 0;
    for (i = 0; i < count; ++i) {
        extent = (S3_I16)(extent + s3_advance(&font, text[i], i, char_widths,
                                              mode, &break_err));
    }
    cell_left = x > left ? x : left;
    cell_right = (S3_I16)(x + extent) < right ? (S3_I16)(x + extent) : right;
    if (cell_right - cell_left > S3_TEXT_ROW_MAX) return 0;

    cached = cell_left < cell_right && count <= S3_TEXT_CACHE;
    if (cached) {
        glyph_x = x;
        break_err = mode != (S3_DRAWMODE16 S3_FAR *)0 ? mode->BreakErr : 0;
        for (i = 0; i < count; ++i) {
            index = s3_font_index(&font, text[i]);
            s3_cache_width[i] = s3_font_width(&font, index);
            s3_cache_glyph[i] = s3_font_glyph(&font, index);
            s3_cache_x[i] = glyph_x;
            glyph_x = (S3_I16)(glyph_x + s3_advance(&font, text[i], i, char_widths,
                                                    mode, &break_err));
        }
    }
    if (cell_left < cell_right) {
        for (row_y = (S3_I16)(y > top ? y : top);
             row_y < (S3_I16)(y + (S3_I16)font.height) && row_y < bottom; ++row_y) {
            if (opaque) {
                S3_FillPixels(s3_text_row, bg, (S3_U16)(cell_right - cell_left), bp);
            } else if (!S3_TargetReadRow(&target, cell_left, row_y,
                                         (S3_U16)(cell_right - cell_left), s3_text_row)) {
                return 0;
            }
            glyph_x = x;
            if (!cached) break_err = mode != (S3_DRAWMODE16 S3_FAR *)0 ? mode->BreakErr : 0;
            for (i = 0; i < count; ++i) {
                if (cached) {
                    width = s3_cache_width[i];
                    glyph = s3_cache_glyph[i];
                    glyph_x = s3_cache_x[i];
                } else {
                    index = s3_font_index(&font, text[i]);
                    width = s3_font_width(&font, index);
                    glyph = s3_font_glyph(&font, index);
                }
                column_bytes = font.height;
                /* Columns of this glyph inside the cells. */
                first = (S3_I16)(cell_left - glyph_x);
                if (first < 0) first = 0;
                last = (S3_I16)(cell_right - glyph_x);
                if (last > (S3_I16)width) last = (S3_I16)width;
                if (first < last) {
                    const S3_U8 S3_FAR *column = glyph + (S3_U16)(row_y - y);
                    S3_U8 S3_FAR *out = s3_text_row +
                                        (S3_U16)(glyph_x + first - cell_left) * bp;
                    px = first;
                    while (px < last) {
                        /* One glyph byte covers 8 columns. */
                        S3_I16 stop = (S3_I16)((px | 7) + 1);
                        bits = column[(S3_U16)(px >> 3) * column_bytes];
                        if (stop > last) stop = last;
                        if (bits == 0) {
                            out += (S3_U16)(stop - px) * bp;
                            px = stop;
                            continue;
                        }
                        for (; px < stop; ++px, out += bp) {
                            if (bits & (0x80U >> (px & 7))) S3_PIXEL_PUT(out, fg, bp);
                        }
                    }
                }
                if (!cached) {
                    glyph_x = (S3_I16)(glyph_x + s3_advance(&font, text[i], i, char_widths,
                                                            mode, &break_err));
                }
            }
            if (!S3_TargetWriteRow(&target, cell_left, row_y,
                                   (S3_U16)(cell_right - cell_left), s3_text_row)) {
                return 0;
            }
        }
    }
    /* The justification error carries over to the next call on this line. */
    if (mode != (S3_DRAWMODE16 S3_FAR *)0) mode->BreakErr = break_err;
    return ((S3_U32)font.height << 16) | (S3_U16)extent;
}

int S3_TextCharWidths(const void S3_FAR *font_info, S3_U16 first,
                      S3_U16 last, S3_I16 S3_FAR *buffer)
{
    S3_FONT font;
    S3_U16 c;
    if (buffer == (S3_I16 S3_FAR *)0 || last < first || last > 255 ||
        !s3_font_open(font_info, &font)) {
        return 0;
    }
    for (c = first; c <= last; ++c) {
        buffer[c - first] = (S3_I16)s3_font_width(&font, s3_font_index(&font, (S3_U8)c));
    }
    return 1;
}
