#include "s3_device.h"
#include "s3_trace.h"
#include "s3_mode.h"
#include <string.h>

#define S3_DEVICE_INIT_COOKIE 0x53334452UL
#define S3_DEVICE_INIT_COOKIE_INVERSE 0xACCCBBADUL

static S3_U8 S3_FAR *S3_FAR S3_PASCAL s3_device_map_bank(
    void S3_FAR *opaque, S3_U32 bank)
{
    S3_DEVICE_RUNTIME S3_FAR *runtime =
        (S3_DEVICE_RUNTIME S3_FAR *)opaque;
    if (runtime == (S3_DEVICE_RUNTIME S3_FAR *)0 ||
        !runtime->enabled ||
        !S3_Win16BridgeSetBank(&runtime->bridge, bank)) {
        return (S3_U8 S3_FAR *)0;
    }
    return (S3_U8 S3_FAR *)runtime->bridge.window;
}

static int s3_bridge_needs_recovery(const S3_WIN16_BRIDGE S3_FAR *bridge)
{
    return bridge->selector_owned || bridge->mode_snapshot_valid ||
           bridge->mode_active;
}

void S3_DeviceRuntimeInit(S3_DEVICE_RUNTIME S3_FAR *runtime)
{
    if (runtime == (S3_DEVICE_RUNTIME S3_FAR *)0) return;
    if (runtime->init_cookie == S3_DEVICE_INIT_COOKIE &&
        runtime->init_cookie_inverse == S3_DEVICE_INIT_COOKIE_INVERSE &&
        runtime->initialized &&
        (runtime->enabled || runtime->cleanup_pending ||
         runtime->device != (S3_PDEV S3_FAR *)0 ||
         s3_bridge_needs_recovery(&runtime->bridge))) {
        return;
    }
    memset(runtime, 0, sizeof(*runtime));
    S3_Win16BridgeInit(&runtime->bridge);
    runtime->initialized = 1;
    runtime->init_cookie = S3_DEVICE_INIT_COOKIE;
    runtime->init_cookie_inverse = S3_DEVICE_INIT_COOKIE_INVERSE;
}

int S3_DeviceEnableWithOps(S3_PDEV S3_FAR *device,
                           S3_DEVICE_RUNTIME S3_FAR *runtime,
                           const S3_BRIDGE_OPS S3_FAR *ops)
{
    if (device == (S3_PDEV S3_FAR *)0 ||
        runtime == (S3_DEVICE_RUNTIME S3_FAR *)0 ||
        !runtime->initialized || runtime->enabled ||
        runtime->cleanup_pending || runtime->device != 0 ||
        s3_bridge_needs_recovery(&runtime->bridge)) {
        return 0;
    }

    /* Open owns mode snapshot and selector cleanup from this point on. Keep
       the caller's PDEV address when any rollback handle must survive. */
    if (s3_mode.backend == S3_BACKEND_VBE
            ? (s3_mode.lfb_linear == 0 ||
               !S3_Win16BridgeOpenLinear(&runtime->bridge, ops,
                                         s3_mode.lfb_linear,
                                         (s3_mode.framebuffer_bytes - 1UL) >> 16))
            : !S3_Win16BridgeOpen(&runtime->bridge, ops, S3_APERTURE_PHYS,
                                  S3_BRIDGE_APERTURE_BYTES)) {
        if (s3_bridge_needs_recovery(&runtime->bridge)) {
            runtime->device = device;
            runtime->cleanup_pending = 1;
        }
        return 0;
    }

    if (!S3_SurfaceInit(&runtime->surface, s3_mode.width, s3_mode.height,
                        s3_mode.pitch, s3_mode.framebuffer_bytes, S3_BANK_BYTES,
                        S3_BRIDGE_APERTURE_BYTES,
                        (S3_U8 S3_FAR *)runtime->bridge.window,
                        runtime, s3_device_map_bank) ||
        !S3_InitializePDEV(device)) {
        S3_TRACE_EVENT("device.surface.fail",
                       (S3_U16)((S3_U32)runtime->bridge.window >> 16),
                       (S3_U16)(S3_U32)runtime->bridge.window);
        if (!S3_Win16BridgeClose(&runtime->bridge)) {
            runtime->device = device;
            runtime->cleanup_pending = 1;
            return 0;
        }
        memset(&runtime->surface, 0, sizeof(runtime->surface));
        return 0;
    }

    runtime->surface.bytes_pixel = s3_mode.bytes_pixel;
    /* Bank 0 was selected by BridgeOpen; avoid an unnecessary remap on the
       first drawing operation while preserving callback validation. */
    runtime->surface.current_bank = 0;
    device->surface_state = &runtime->surface;
    runtime->device = device;
    runtime->enabled = 1;
    return 1;
}

int S3_DeviceDisableWithOps(S3_PDEV S3_FAR *device,
                            S3_DEVICE_RUNTIME S3_FAR *runtime)
{
    /* The PDEV may already be gone on a retry. Never dereference it here. */
    (void)device;
    if (runtime == (S3_DEVICE_RUNTIME S3_FAR *)0 || !runtime->initialized) {
        return 0;
    }
    /* A failed restore/free must not leave a drawable surface that bypasses
       the bridge callback via its cached current-bank window. Detach only the
       PDEV supplied to this call; never dereference the saved pointer on a
       later retry, when the original PDEV may already have been released. */
    runtime->surface.window = (S3_U8 S3_FAR *)0;
    runtime->surface.map_bank = 0;
    runtime->surface.current_bank = 0xFFFFFFFFUL;
    if (device != (S3_PDEV S3_FAR *)0 && device == runtime->device) {
        device->surface_state = 0;
    }
    if (!S3_Win16BridgeClose(&runtime->bridge)) {
        runtime->cleanup_pending = 1;
        return 0;
    }
    memset(&runtime->surface, 0, sizeof(runtime->surface));
    runtime->device = 0;
    runtime->enabled = 0;
    runtime->cleanup_pending = 0;
    return 1;
}
