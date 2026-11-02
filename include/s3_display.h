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

/* Full-screen DOS sessions (Enhanced Mode screen switching, s3_switch.c).
   Start once the display is enabled, stop after it is disabled. While a DOS
   session owns the screen, drawing on the display is skipped (Blocked is
   nonzero for a call that involves the display) and USER repaints
   everything when Windows gets the screen back. */
void S3_SwitchStart(void);
void S3_SwitchStop(void);
int S3_SwitchInBackground(void);
/* Nonzero while the display is enabled (between Enable and Disable; GDI's
   Death disables it while a full-screen DIB player such as DISPDIB owns the
   screen). The hardware cursor is left alone while it is not. */
int S3_DisplayIsEnabled(void);
int S3_DisplayBlocked(const void S3_FAR *device, const void S3_FAR *other);
void S3_SwitchRepaintDisable(S3_U16 disable);
/* s3_exports.c: the screen is going to a DOS session / coming back (mode
   set again, cursors restored). */
void S3_DisplaySuspend(void);
int S3_DisplayResume(void);
/* s3_dda_entries.c: reload and re-enable the S3 hardware cursor. */
void S3_HwCursorRestore(void);

#if !defined(S3_HOST_TEST)
/* The driver is compiled for the 386 (-3), so its code may use the upper
   halves of 32-bit registers, which 16-bit interrupt handlers do not save.
   Interrupt-time entries hand all of them back unchanged to whatever they
   interrupted, which may be this driver's own drawing code. */
void S3_PUSH32(void);
#pragma aux S3_PUSH32 = 0x66 0x60;  /* pushad */
void S3_POP32(void);
#pragma aux S3_POP32 = 0x66 0x61;   /* popad */
#else
#define S3_PUSH32()
#define S3_POP32()
#endif

/* Software cursor entry points (VBE backend). */
int S3_DisplaySwCursorActive(void);
int S3_DisplaySwCursorShape(const S3_CURSORSHAPE16 S3_FAR *shape);
void S3_DisplaySwCursorMove(S3_I16 x, S3_I16 y);
void S3_DisplaySwCursorCheck(void);

#endif
