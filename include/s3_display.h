#ifndef S3_DISPLAY_H
#define S3_DISPLAY_H

#include "s3_abi.h"
#include "s3_cursor.h"

/* Display-wide services owned by s3_exports.c. */

/* Copy bytes into video memory past the visible frame (S3 backend). */
int S3_DisplayWriteOffscreen(S3_U32 offset, const S3_U8 S3_FAR *data,
                             S3_U16 bytes);

/* Software-cursor exclusion around drawing calls. Each device comes with
   the screen rectangle the call may read or write on it (right/bottom
   exclusive, NULL = anywhere); only display devices count. Exclude returns
   nonzero when the cursor was held, and hides it only if it overlaps those
   rectangles; pass the value to Unexclude. Both are no-ops with a hardware
   cursor. */
int S3_DisplayExclude(const void S3_FAR *device, const S3_RECT16 S3_FAR *rect,
                      const void S3_FAR *other, const S3_RECT16 S3_FAR *other_rect);
void S3_DisplayUnexclude(int excluded);
/* rect = (left, top, right, bottom) clamped to 16-bit coordinates. */
void S3_DisplaySetRect(S3_RECT16 S3_FAR *rect, S3_I32 left, S3_I32 top,
                       S3_I32 right, S3_I32 bottom);

/* Software cursor entry points (VBE backend). */
int S3_DisplaySwCursorActive(void);
int S3_DisplaySwCursorShape(const S3_CURSORSHAPE16 S3_FAR *shape);
void S3_DisplaySwCursorMove(S3_I16 x, S3_I16 y);
void S3_DisplaySwCursorCheck(void);

#endif
