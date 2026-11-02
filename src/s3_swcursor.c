#include "s3_swcursor.h"

#if defined(S3_HOST_TEST)
static S3_U16 s3_irq_save(void) { return 0; }
static void s3_irq_restore(S3_U16 flags) { (void)flags; }
#else
#include <i86.h>
static S3_U16 s3_irq_save(void)
{
    S3_U16 flags;
    __asm {
        pushf
        pop ax
        cli
        mov flags, ax
    }
    return flags;
}

static void s3_irq_restore(S3_U16 flags)
{
    if (flags & 0x0200U) _enable();
}
#endif

/* Claim the cursor for an interrupt-time redraw; fails while drawing or
   another redraw holds it. */
static int s3_try_enter(S3_SWCURSOR S3_FAR *cursor)
{
    S3_U16 flags = s3_irq_save();
    int entered = cursor->busy == 0;
    if (entered) cursor->busy = 1;
    s3_irq_restore(flags);
    return entered;
}

static void s3_leave(S3_SWCURSOR S3_FAR *cursor)
{
    S3_U16 flags = s3_irq_save();
    --cursor->busy;
    s3_irq_restore(flags);
}

/* Visit the saved rectangle row by row in bank-contiguous pixel runs.
   restore != 0 writes the save buffer back; otherwise it saves the screen
   and applies the masks. */
static void s3_cursor_pass(S3_SWCURSOR S3_FAR *cursor, int restore)
{
    S3_SURFACE S3_FAR *surface = cursor->surface;
    S3_U16 bp = surface->bytes_pixel ? surface->bytes_pixel : 1;
    S3_U16 row_bytes = (S3_U16)(S3_SWCURSOR_SIZE * bp);
    S3_I16 image_left = (S3_I16)(cursor->drawn_x - cursor->hot_x);
    S3_I16 image_top = (S3_I16)(cursor->drawn_y - cursor->hot_y);
    S3_I16 x, y;
    for (y = cursor->drawn_top; y < cursor->drawn_bottom; ++y) {
        S3_U16 iy = (S3_U16)(y - image_top);
        S3_U8 S3_FAR *save = cursor->save + (S3_U16)(y - cursor->drawn_top) * row_bytes;
        x = cursor->drawn_left;
        while (x < cursor->drawn_right) {
            S3_U16 contiguous = 0, run, i, k;
            S3_U8 S3_FAR *p = S3_SurfaceSpan(surface, x, y, &contiguous);
            if (p == (S3_U8 S3_FAR *)0) return;
            run = (S3_U16)(cursor->drawn_right - x);
            if (contiguous / bp < run) run = (S3_U16)(contiguous / bp);
            if (run == 0) return;
            for (i = 0; i < run; ++i, ++x) {
                S3_U16 ix = (S3_U16)(x - image_left);
                S3_U8 S3_FAR *s = save + (S3_U16)(x - cursor->drawn_left) * bp;
                S3_U8 bit = (S3_U8)(0x80U >> (ix & 7));
                S3_U16 m = (S3_U16)(iy * (S3_SWCURSOR_SIZE / 8) + (ix >> 3));
                S3_U8 a = (cursor->and_mask[m] & bit) ? 0xFF : 0x00;
                S3_U8 xr = (cursor->xor_mask[m] & bit) ? 0xFF : 0x00;
                for (k = 0; k < bp; ++k, ++p) {
                    if (restore) {
                        *p = s[k];
                    } else {
                        s[k] = *p;
                        *p = (S3_U8)((*p & a) ^ xr);
                    }
                }
            }
        }
    }
}

static void s3_erase(S3_SWCURSOR S3_FAR *cursor)
{
    if (!cursor->drawn) return;
    s3_cursor_pass(cursor, 1);
    cursor->drawn = 0;
}

static void s3_draw(S3_SWCURSOR S3_FAR *cursor)
{
    S3_SURFACE S3_FAR *surface = cursor->surface;
    S3_I16 x = cursor->x, y = cursor->y;
    S3_I32 left = (S3_I32)x - cursor->hot_x, top = (S3_I32)y - cursor->hot_y;
    S3_I32 right = left + S3_SWCURSOR_SIZE, bottom = top + S3_SWCURSOR_SIZE;
    if (!cursor->shape_valid || cursor->drawn) return;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > (S3_I32)surface->width) right = surface->width;
    if (bottom > (S3_I32)surface->height) bottom = surface->height;
    cursor->drawn_x = x;
    cursor->drawn_y = y;
    if (left >= right || top >= bottom) {
        /* Entirely off screen: nothing saved, nothing to erase. */
        return;
    }
    cursor->drawn_left = (S3_I16)left;
    cursor->drawn_top = (S3_I16)top;
    cursor->drawn_right = (S3_I16)right;
    cursor->drawn_bottom = (S3_I16)bottom;
    s3_cursor_pass(cursor, 0);
    cursor->drawn = 1;
}

static void s3_redraw_if_moved(S3_SWCURSOR S3_FAR *cursor)
{
    if (cursor->drawn &&
        (cursor->drawn_x != cursor->x || cursor->drawn_y != cursor->y)) {
        s3_erase(cursor);
    }
    s3_draw(cursor);
}

void S3_SwCursorInit(S3_SWCURSOR S3_FAR *cursor, S3_SURFACE S3_FAR *surface)
{
    S3_U16 i;
    S3_U8 S3_FAR *p = (S3_U8 S3_FAR *)cursor;
    for (i = 0; i < sizeof(*cursor); ++i) p[i] = 0;
    cursor->surface = surface;
}

int S3_SwCursorSetShape(S3_SWCURSOR S3_FAR *cursor,
                        const S3_CURSORSHAPE16 S3_FAR *shape)
{
    const S3_U8 S3_FAR *and_mask;
    const S3_U8 S3_FAR *xor_mask;
    S3_U16 row, col, width_bytes, rows, cols;

    /* Task time: hold the cursor like a drawing call would. */
    S3_SwCursorExclude(cursor, (const S3_RECT16 S3_FAR *)0);
    cursor->shape_valid = 0;
    if (shape == (const S3_CURSORSHAPE16 S3_FAR *)0) {
        S3_SwCursorUnexclude(cursor);
        return 1;
    }
    if (shape->csWidth <= 0 || shape->csHeight <= 0 ||
        shape->csWidthBytes <= 0 ||
        (S3_U16)shape->csWidthBytes * 8U < (S3_U16)shape->csWidth) {
        S3_SwCursorUnexclude(cursor);
        return 0;
    }
    width_bytes = (S3_U16)shape->csWidthBytes;
    rows = (S3_U16)shape->csHeight < S3_SWCURSOR_SIZE ? (S3_U16)shape->csHeight
                                                      : S3_SWCURSOR_SIZE;
    cols = width_bytes < S3_SWCURSOR_SIZE / 8 ? width_bytes : S3_SWCURSOR_SIZE / 8;
    and_mask = (const S3_U8 S3_FAR *)(shape + 1);
    xor_mask = and_mask + width_bytes * (S3_U16)shape->csHeight;
    for (row = 0; row < S3_SWCURSOR_SIZE; ++row) {
        for (col = 0; col < S3_SWCURSOR_SIZE / 8; ++col) {
            S3_U16 i = (S3_U16)(row * (S3_SWCURSOR_SIZE / 8) + col);
            int inside = row < rows && col < cols;
            cursor->and_mask[i] = inside ? and_mask[row * width_bytes + col] : 0xFF;
            cursor->xor_mask[i] = inside ? xor_mask[row * width_bytes + col] : 0x00;
        }
    }
    cursor->hot_x = shape->csHotX;
    cursor->hot_y = shape->csHotY;
    cursor->shape_valid = 1;
    S3_SwCursorUnexclude(cursor);
    return 1;
}

void S3_SwCursorMove(S3_SWCURSOR S3_FAR *cursor, S3_I16 x, S3_I16 y)
{
    cursor->x = x;
    cursor->y = y;
    if (!s3_try_enter(cursor)) return; /* drawn when the holder finishes */
    s3_redraw_if_moved(cursor);
    s3_leave(cursor);
}

void S3_SwCursorCheck(S3_SWCURSOR S3_FAR *cursor)
{
    if (!s3_try_enter(cursor)) return;
    s3_redraw_if_moved(cursor);
    s3_leave(cursor);
}

void S3_SwCursorExclude(S3_SWCURSOR S3_FAR *cursor,
                        const S3_RECT16 S3_FAR *bounds)
{
    S3_U16 flags = s3_irq_save();
    ++cursor->busy;
    s3_irq_restore(flags);
    /* Held: an interrupt-time move can no longer redraw it elsewhere. */
    if (bounds != (const S3_RECT16 S3_FAR *)0 && cursor->drawn &&
        (bounds->right <= cursor->drawn_left || bounds->left >= cursor->drawn_right ||
         bounds->bottom <= cursor->drawn_top || bounds->top >= cursor->drawn_bottom)) {
        return;
    }
    s3_erase(cursor);
}

void S3_SwCursorUnexclude(S3_SWCURSOR S3_FAR *cursor)
{
    if (cursor->busy == 1) s3_redraw_if_moved(cursor);
    s3_leave(cursor);
}

void S3_SwCursorSuspend(S3_SWCURSOR S3_FAR *cursor)
{
    S3_U16 flags = s3_irq_save();
    ++cursor->busy;
    cursor->drawn = 0;
    s3_irq_restore(flags);
}

void S3_SwCursorResume(S3_SWCURSOR S3_FAR *cursor)
{
    S3_SwCursorUnexclude(cursor);
}
