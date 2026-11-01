#ifndef S3_WIN16_NATIVE_H
#define S3_WIN16_NATIVE_H

#include "s3_win16_bridge.h"

/* Fill only the injected Win16 selector callbacks. This function does not
   connect mode, bank, port-I/O, or Enable operations. */
int S3_Win16NativeSelectorOps(S3_BRIDGE_OPS S3_FAR *ops);

#endif
