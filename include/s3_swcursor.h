#ifndef S3_SWCURSOR_H
#define S3_SWCURSOR_H

#include "s3_surface.h"
#include "s3_cursor.h"

/* Software mouse cursor for backends without a hardware cursor. It draws
   through its own surface (its own selector), so interrupt-time moves never
   disturb the bank window a drawing call is using. Drawing calls that touch
   the screen bracket themselves with Exclude/Unexclude; while any drawing is
   in progress, Move only records the position and Unexclude or Check draw
   it later. Masks are applied bytewise (AND with 00/FF, then XOR with
   00/FF), which is white-on-black at every depth with an all-ones white. */
#define S3_SWCURSOR_SIZE 32
#define S3_SWCURSOR_MASK_BYTES (S3_SWCURSOR_SIZE * S3_SWCURSOR_SIZE / 8)
#define S3_SWCURSOR_SAVE_BYTES (S3_SWCURSOR_SIZE * S3_SWCURSOR_SIZE * 4)

typedef struct S3_SWCURSOR {
    S3_SURFACE S3_FAR *surface;
    S3_U8 and_mask[S3_SWCURSOR_MASK_BYTES];
    S3_U8 xor_mask[S3_SWCURSOR_MASK_BYTES];
    S3_I16 hot_x, hot_y;
    volatile S3_I16 x, y;            /* requested hot-spot position */
    S3_I16 drawn_left, drawn_top;    /* clipped saved rectangle */
    S3_I16 drawn_right, drawn_bottom;
    S3_I16 drawn_x, drawn_y;         /* hot-spot position drawn */
    volatile S3_U16 busy;
    S3_U16 shape_valid;
    S3_U16 drawn;
    S3_U8 save[S3_SWCURSOR_SAVE_BYTES];
} S3_SWCURSOR;

void S3_SwCursorInit(S3_SWCURSOR S3_FAR *cursor, S3_SURFACE S3_FAR *surface);
/* Start drawing again on a (new) surface after a stop, keeping the shape,
   hot spot and position: nothing is held and nothing is on screen. */
void S3_SwCursorRestart(S3_SWCURSOR S3_FAR *cursor, S3_SURFACE S3_FAR *surface);
/* NULL shape hides the cursor. Returns nonzero on success. */
int S3_SwCursorSetShape(S3_SWCURSOR S3_FAR *cursor,
                        const S3_CURSORSHAPE16 S3_FAR *shape);
void S3_SwCursorMove(S3_SWCURSOR S3_FAR *cursor, S3_I16 x, S3_I16 y);
void S3_SwCursorCheck(S3_SWCURSOR S3_FAR *cursor);
/* Hold the cursor for a drawing call that touches bounds (screen
   coordinates, right/bottom exclusive; NULL = anywhere). The cursor is
   erased only when bounds overlap it; otherwise it stays on screen and
   moves are deferred until Unexclude. */
void S3_SwCursorExclude(S3_SWCURSOR S3_FAR *cursor,
                        const S3_RECT16 S3_FAR *bounds);
void S3_SwCursorUnexclude(S3_SWCURSOR S3_FAR *cursor);
/* A DOS session takes the screen: hold the cursor and forget what it
   covered, without touching the frame buffer. Resume (after the mode is
   set again) releases the hold and draws it afresh. */
void S3_SwCursorSuspend(S3_SWCURSOR S3_FAR *cursor);
void S3_SwCursorResume(S3_SWCURSOR S3_FAR *cursor);

#endif
