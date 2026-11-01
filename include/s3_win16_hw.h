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

#endif
