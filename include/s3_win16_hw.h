#ifndef S3_WIN16_HW_H
#define S3_WIN16_HW_H

#include "s3_win16_bridge.h"

/* Fill the mode and bank callbacks with direct VGA/S3 port I/O for the
   Windows driver build. capture_mode saves the S3 timing snapshot plus the
   sequencer, graphics and attribute controllers and the DAC; program_mode
   applies the 1280x720x8 sequence proven by tests/dosbox-x/raw_s3_cold_smoke.c;
   restore_mode writes the saved state back. Selector callbacks are filled
   separately by S3_Win16NativeSelectorOps. */
int S3_Win16PortModeOps(S3_BRIDGE_OPS S3_FAR *ops);

/* Load the driver's 256-entry colour table into the VGA DAC (6-bit). */
int S3_Win16LoadPalette(void);

/* Nonzero when the chip is an S3 Trio64 (device ID CR2D/CR2E = 88h/11h),
   the only chip the native register path programs. */
int S3_Win16IsTrio64(void);

/* S3 hardware cursor (CR45-CR4F). The pattern lives at the 1 KiB-aligned
   video memory offset S3_HW_CURSOR_OFFSET, just past the framebuffer and
   inside the bridge's bank 14. Colours: background 0 (black), foreground
   0xFF (white). S3_HwCursorMove may run at interrupt time: it saves and
   restores the CRTC index and the S3 lock registers. */
#define S3_HW_CURSOR_OFFSET 0x000E1000UL
void S3_HwCursorSetup(void);
void S3_HwCursorEnable(int enable);
void S3_HwCursorMove(S3_I16 x, S3_I16 y);

#endif
