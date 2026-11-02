#include "s3_abi.h"
#include "s3_device.h"
#include "s3_trace.h"
#include "s3_mode.h"
#include "s3_display.h"
#include "s3_swcursor.h"
#if !defined(S3_HOST_TEST)
#include <string.h>
#include <i86.h>
#include "s3_win16_hw.h"
#include "s3_win16_native.h"
#include "s3_vbe.h"
#endif

static S3_DEVICE_RUNTIME s3_display_runtime;

#if !defined(S3_HOST_TEST)
/* Software cursor (VBE backend): its own surface over the linear
   framebuffer through a second selector, moved with DPMI 0007h so the
   interrupt-time path needs no KERNEL call. */
static S3_SWCURSOR s3_swcursor;
static S3_SURFACE s3_cursor_surface;
static S3_U16 s3_cursor_selector;
static S3_U16 s3_swcursor_active;

static S3_U16 s3_dpmi_alloc_selector(void)
{
    S3_U16 selector, failed;
    __asm {
        xor ax, ax
        mov cx, 1
        int 31h
        sbb cx, cx
        mov selector, ax
        mov failed, cx
    }
    return failed ? 0 : selector;
}

static int s3_dpmi_set_base(S3_U16 selector, S3_U32 base)
{
    S3_U16 hi = (S3_U16)(base >> 16), lo = (S3_U16)base, failed;
    __asm {
        mov ax, 0007h
        mov bx, selector
        mov cx, hi
        mov dx, lo
        int 31h
        sbb ax, ax
        mov failed, ax
    }
    return !failed;
}

static int s3_dpmi_set_limit(S3_U16 selector, S3_U16 limit)
{
    S3_U16 failed;
    __asm {
        mov ax, 0008h
        mov bx, selector
        xor cx, cx
        mov dx, limit
        int 31h
        sbb ax, ax
        mov failed, ax
    }
    return !failed;
}

static void s3_dpmi_free_selector(S3_U16 selector)
{
    __asm {
        mov ax, 0001h
        mov bx, selector
        int 31h
    }
}

static S3_U8 S3_FAR *S3_FAR S3_PASCAL s3_cursor_map_bank(void S3_FAR *context,
                                                         S3_U32 bank)
{
    (void)context;
    if (!s3_dpmi_set_base(s3_cursor_selector,
                          s3_mode.lfb_linear + (bank << 16))) {
        return (S3_U8 S3_FAR *)0;
    }
    return (S3_U8 S3_FAR *)MK_FP(s3_cursor_selector, 0);
}

static void s3_swcursor_start(void)
{
    S3_U16 selector;
    if (s3_mode.backend != S3_BACKEND_VBE || s3_swcursor_active) return;
    selector = s3_dpmi_alloc_selector();
    if (selector == 0) return;
    if (!s3_dpmi_set_limit(selector, 0xFFFFU) ||
        !s3_dpmi_set_base(selector, s3_mode.lfb_linear)) {
        s3_dpmi_free_selector(selector);
        return;
    }
    s3_cursor_selector = selector;
    if (!S3_SurfaceInit(&s3_cursor_surface, s3_mode.width, s3_mode.height,
                        s3_mode.pitch, s3_mode.framebuffer_bytes,
                        S3_BANK_BYTES, S3_BANK_BYTES,
                        (S3_U8 S3_FAR *)MK_FP(selector, 0),
                        (void S3_FAR *)0, s3_cursor_map_bank)) {
        s3_dpmi_free_selector(selector);
        s3_cursor_selector = 0;
        return;
    }
    s3_cursor_surface.bytes_pixel = s3_mode.bytes_pixel;
    S3_SwCursorInit(&s3_swcursor, &s3_cursor_surface);
    s3_swcursor_active = 1;
    S3_TRACE_EVENT("swcursor.start", selector, 0);
}

static void s3_swcursor_stop(void)
{
    if (!s3_swcursor_active) return;
    /* Hide it and keep it held so interrupt-time calls draw nothing. */
    S3_SwCursorExclude(&s3_swcursor);
    s3_swcursor_active = 0;
    s3_dpmi_free_selector(s3_cursor_selector);
    s3_cursor_selector = 0;
}

int S3_DisplaySwCursorActive(void)
{
    return s3_swcursor_active;
}

int S3_DisplaySwCursorShape(const S3_CURSORSHAPE16 S3_FAR *shape)
{
    if (!s3_swcursor_active) return shape == (const S3_CURSORSHAPE16 S3_FAR *)0;
    return S3_SwCursorSetShape(&s3_swcursor, shape);
}

void S3_DisplaySwCursorMove(S3_I16 x, S3_I16 y)
{
    if (s3_swcursor_active) S3_SwCursorMove(&s3_swcursor, x, y);
}

void S3_DisplaySwCursorCheck(void)
{
    if (s3_swcursor_active) S3_SwCursorCheck(&s3_swcursor);
}

int S3_DisplayExclude(const void S3_FAR *device, const void S3_FAR *other)
{
    if (!s3_swcursor_active) return 0;
    if ((device == (const void S3_FAR *)0 ||
         ((const S3_PDEV S3_FAR *)device)->pdType != S3_PDEV_DISPLAY) &&
        (other == (const void S3_FAR *)0 ||
         ((const S3_PDEV S3_FAR *)other)->pdType != S3_PDEV_DISPLAY)) {
        return 0;
    }
    S3_SwCursorExclude(&s3_swcursor);
    return 1;
}

void S3_DisplayUnexclude(int excluded)
{
    if (excluded && s3_swcursor_active) S3_SwCursorUnexclude(&s3_swcursor);
}
#else
int S3_DisplayExclude(const void S3_FAR *device, const void S3_FAR *other)
{
    (void)device;
    (void)other;
    return 0;
}

void S3_DisplayUnexclude(int excluded)
{
    (void)excluded;
}
#endif

#if !defined(S3_HOST_TEST)
/* Program 1280x720x8 through direct VGA/S3 port I/O and map the 64 KiB A000
   aperture through the selector bridge, which owns rollback. */
static int s3_enable_display(S3_PDEV S3_FAR *device, S3_U16 style)
{
    S3_BRIDGE_OPS ops;

    if (!s3_display_runtime.initialized) {
        S3_DeviceRuntimeInit(&s3_display_runtime);
    }
    if (style & S3_INFO_CONTEXT) {
        /* Information context: describe the device, touch no hardware. */
        if (!S3_InitializePDEV(device)) return 0;
        device->surface_state = (void S3_FAR *)0;
        S3_TRACE_EVENT("Enable.info.ok", style, 0);
        return 1;
    }
    if (s3_display_runtime.enabled) {
        /* Another display PDEV shares the already programmed surface. */
        if (!S3_InitializePDEV(device)) return 0;
        device->surface_state = &s3_display_runtime.surface;
        S3_TRACE_EVENT("Enable.device.shared", style, 0);
        return 1;
    }
    _fmemset(&ops, 0, sizeof(ops));
    if (!S3_Win16NativeSelectorOps(&ops) ||
        !(s3_mode.backend == S3_BACKEND_VBE ? S3_VbeModeOps(&ops)
                                             : S3_Win16PortModeOps(&ops))) {
        S3_TRACE_EVENT("Enable.device.noops", style, 0);
        return 0;
    }
    if (!S3_DeviceEnableWithOps(device, &s3_display_runtime, &ops)) {
        S3_TRACE_EVENT("Enable.device.fail", style,
                       s3_display_runtime.cleanup_pending);
        return 0;
    }
    s3_swcursor_start();
    S3_TRACE_EVENT("Enable.device.ok", style, 0);
    return 1;
}
#endif

#if !defined(S3_HOST_TEST)
/* Copy bytes into video memory outside the visible framebuffer (one bank).
   The drawing surface remaps its bank on its next access. */
int S3_DisplayWriteOffscreen(S3_U32 offset, const S3_U8 S3_FAR *data,
                             S3_U16 bytes)
{
    S3_U8 S3_FAR *window;
    if (!s3_display_runtime.enabled || s3_mode.backend != S3_BACKEND_S3 ||
        (offset & 0xFFFFUL) + bytes > 0x10000UL ||
        !S3_Win16BridgeSetBank(&s3_display_runtime.bridge, offset >> 16)) {
        return 0;
    }
    window = (S3_U8 S3_FAR *)s3_display_runtime.bridge.window;
    _fmemcpy(window + (S3_U16)offset, data, bytes);
    s3_display_runtime.surface.current_bank = 0xFFFFFFFFUL;
    return 1;
}
#endif

int S3_FAR S3_PASCAL Enable(void S3_FAR *device, S3_I16 style,
                            char S3_FAR *device_type,
                            char S3_FAR *output_file,
                            char S3_FAR *extra_info)
{
    (void)device_type;
    (void)output_file;
    (void)extra_info;
    S3_TRACE_EVENT("Enable", style, device == (void S3_FAR *)0);
    if (!S3_ModeSelect()) {
        S3_TRACE_EVENT("Enable.nomode", style, 0);
        return 0;
    }
    if (style & (S3_I16)S3_INQUIRE_INFO) {
        if (device == (void S3_FAR *)0 || !S3_BuildGDIInfo((S3_GDIINFO S3_FAR *)device)) {
            S3_TRACE_EVENT("Enable.inquire.fail", style, 0);
            return 0;
        }
        S3_TRACE_EVENT("Enable.inquire.ok", style, sizeof(S3_GDIINFO));
        return (int)sizeof(S3_GDIINFO);
    }
    /* Unknown style bits are unsupported. */
    if (((S3_U16)style & (S3_U16)~S3_INFO_CONTEXT) != S3_ENABLE_DEVICE) {
        S3_TRACE_EVENT("Enable.unknown", style, 0);
        return 0;
    }
    if (device == (void S3_FAR *)0) {
        return 0;
    }
#if defined(S3_HOST_TEST)
    /* Host builds have no port I/O or selectors: device Enable fails closed;
       tests drive S3_DeviceEnableWithOps with injected operations instead. */
    return 0;
#else
    return s3_enable_display((S3_PDEV S3_FAR *)device, (S3_U16)style);
#endif
}

int S3_FAR S3_PASCAL GetDriverResourceID(int resource_id,
                                         char S3_FAR *resource_type)
{
    /* No resolution-specific resource remapping: preserve ID and type. */
#if defined(S3_TRACE) && !defined(S3_HOST_TEST)
    S3_TRACE_EVENT("GetDriverResourceID", resource_id,
                   (S3_U16)((S3_U32)resource_type >> 16));
    /* A string type has a nonzero selector: log its first four characters. */
    if (((S3_U32)resource_type >> 16) != 0) {
        S3_TRACE_EVENT("GetDriverResourceID.type",
                       ((S3_U16)(S3_U8)resource_type[0] << 8) | (S3_U8)resource_type[1],
                       resource_type[1] == '\0' ? 0 :
                       ((S3_U16)(S3_U8)resource_type[2] << 8) | (S3_U8)resource_type[3]);
    } else {
        S3_TRACE_EVENT("GetDriverResourceID.typeid", (S3_U16)(S3_U32)resource_type, 0);
    }
#endif
    (void)resource_type;
    return resource_id;
}

int S3_FAR S3_PASCAL Disable(void S3_FAR *device)
{
    /* The ordinary Enable path remains fail-closed until a verified Windows
       mode/I-O provider is available. If a future injected provider enabled
       the device, keep bridge state intact when restore/free needs retry. */
    S3_TRACE_EVENT("Disable", 0, 0);
#if !defined(S3_HOST_TEST)
    s3_swcursor_stop();
#endif
    if (!s3_display_runtime.initialized) {
        S3_DeviceRuntimeInit(&s3_display_runtime);
    }
    return S3_DeviceDisableWithOps((S3_PDEV S3_FAR *)device,
                                   &s3_display_runtime);
}

int S3_FAR S3_PASCAL LibMain(unsigned short instance,
                             unsigned short data_segment,
                             unsigned short heap_size,
                             char S3_FAR *command_line)
{
    (void)instance;
    (void)data_segment;
    (void)heap_size;
    (void)command_line;
    S3_TRACE_EVENT("LibMain", instance, data_segment);
    S3_DeviceRuntimeInit(&s3_display_runtime);
    return 1;
}

int S3_FAR S3_PASCAL WEP(int reason)
{
    S3_TRACE_EVENT("WEP", reason, 0);
    return reason;
}
