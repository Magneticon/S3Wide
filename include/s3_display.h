#ifndef S3_DISPLAY_H
#define S3_DISPLAY_H

#include "s3_abi.h"
#include "s3_cursor.h"

/* Display-wide services owned by s3_exports.c. */

/* Copy bytes into video memory past the visible frame (S3 backend). */
int S3_DisplayWriteOffscreen(S3_U32 offset, const S3_U8 S3_FAR *data,
                             S3_U16 bytes);

/* Software-cursor exclusion around drawing calls. Exclude returns nonzero
   when either device is the display and the software cursor was hidden;
   pass that value to Unexclude. Both are no-ops with a hardware cursor. */
int S3_DisplayExclude(const void S3_FAR *device, const void S3_FAR *other);
void S3_DisplayUnexclude(int excluded);

/* Software cursor entry points (VBE backend). */
int S3_DisplaySwCursorActive(void);
int S3_DisplaySwCursorShape(const S3_CURSORSHAPE16 S3_FAR *shape);
void S3_DisplaySwCursorMove(S3_I16 x, S3_I16 y);
void S3_DisplaySwCursorCheck(void);

#endif
