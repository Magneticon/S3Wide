#ifndef S3_DEVICE_H
#define S3_DEVICE_H

#include "s3_surface.h"
#include "s3_win16_bridge.h"

/* Caller-owned state keeps the recovery handles alive if rollback fails. */
typedef struct S3_DEVICE_RUNTIME {
    S3_WIN16_BRIDGE bridge;
    S3_SURFACE surface;
    S3_PDEV S3_FAR *device;
    S3_U16 initialized;
    S3_U16 enabled;
    S3_U16 cleanup_pending;
    S3_U32 init_cookie;
    S3_U32 init_cookie_inverse;
} S3_DEVICE_RUNTIME;

/* Safe to call on a zero-initialized runtime; active cleanup ownership is kept. */
void S3_DeviceRuntimeInit(S3_DEVICE_RUNTIME S3_FAR *runtime);
int S3_DeviceEnableWithOps(S3_PDEV S3_FAR *device,
                           S3_DEVICE_RUNTIME S3_FAR *runtime,
                           const S3_BRIDGE_OPS S3_FAR *ops);
int S3_DeviceDisableWithOps(S3_PDEV S3_FAR *device,
                            S3_DEVICE_RUNTIME S3_FAR *runtime);

#endif
