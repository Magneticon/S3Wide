#include "s3_vbe.h"
#include "s3_trace.h"

static S3_U16 s3_get16(const S3_U8 S3_FAR *p)
{
    return (S3_U16)(p[0] | ((S3_U16)p[1] << 8));
}

static S3_U32 s3_get32(const S3_U8 S3_FAR *p)
{
    return (S3_U32)s3_get16(p) | ((S3_U32)s3_get16(p + 2) << 16);
}

void S3_VbeDecodeModeInfo(const S3_U8 S3_FAR *raw, S3_U16 vbe_version,
                          S3_VBE_MODE_INFO S3_FAR *info)
{
    info->attributes = s3_get16(raw + 0x00);
    info->pitch = s3_get16(raw + 0x10);
    info->width = s3_get16(raw + 0x12);
    info->height = s3_get16(raw + 0x14);
    info->bits_pixel = raw[0x19];
    info->memory_model = raw[0x1B];
    info->red_mask_size = raw[0x1F];
    info->lfb_physical = s3_get32(raw + 0x28);
    /* VBE 3.0 reports the linear-mode pitch separately. */
    if (vbe_version >= 0x0300 && s3_get16(raw + 0x32) != 0) {
        info->pitch = s3_get16(raw + 0x32);
    }
}

int S3_VbeModeMatches(const S3_VBE_MODE_INFO S3_FAR *info, S3_U16 width,
                      S3_U16 height, S3_U16 bits_pixel)
{
    const S3_U16 required = S3_VBE_ATTR_SUPPORTED | S3_VBE_ATTR_GRAPHICS |
                            S3_VBE_ATTR_LFB;
    if ((info->attributes & required) != required ||
        info->width != width || info->height != height ||
        info->bits_pixel != bits_pixel || info->lfb_physical == 0) {
        return 0;
    }
    switch (bits_pixel) {
    case 8:
        return info->memory_model == S3_VBE_MODEL_PACKED;
    case 16:
        return info->memory_model == S3_VBE_MODEL_DIRECT &&
               info->red_mask_size == 5;
    case 32:
        return info->memory_model == S3_VBE_MODEL_DIRECT;
    }
    return 0;
}

#if !defined(S3_HOST_TEST)
#define NOGDI
#define NODRIVERS
#include <win/win16.h>
#include <i86.h>
#include <string.h>
#include "s3_win16_hw.h"

#define S3_INI_SECTION "s3wide.drv"
#define S3_INI_FILE    "system.ini"
#define S3_VBE_MAX_MODES 256
#define S3_VBE_INFO_OFFSET 0U
#define S3_VBE_MODE_OFFSET 512U

/* DPMI real-mode call structure (INT 31h AX=0300h). */
#pragma pack(push, 1)
typedef struct S3_RMCS {
    S3_U32 edi, esi, ebp, reserved, ebx, edx, ecx, eax;
    S3_U16 flags, es, ds, fs, gs, ip, cs, sp, ss;
} S3_RMCS;
#pragma pack(pop)

static S3_RMCS s3_rmcs;
static S3_U16 s3_saved_bios_mode;
static S3_U16 s3_modes[S3_VBE_MAX_MODES];

/* Run INT 10h in real mode with the registers in s3_rmcs. Returns AX, or
   0xFFFF when DPMI itself failed. */
static S3_U16 s3_int10(S3_U16 ax, S3_U16 bx, S3_U16 cx, S3_U16 es,
                       S3_U16 di)
{
    S3_RMCS S3_FAR *rm = &s3_rmcs;
    S3_U16 failed;
    _fmemset(rm, 0, sizeof(*rm));
    rm->eax = ax;
    rm->ebx = bx;
    rm->ecx = cx;
    rm->es = es;
    rm->edi = di;
    __asm {
        push es
        push di
        les di, rm
        mov ax, 0300h
        mov bx, 0010h
        xor cx, cx
        int 31h
        sbb ax, ax
        pop di
        pop es
        mov failed, ax
    }
    return failed ? 0xFFFFU : (S3_U16)rm->eax;
}

/* DPMI 0002h: a selector for a real-mode segment (never freed). */
static S3_U16 s3_segment_selector(S3_U16 segment)
{
    S3_U16 selector, failed;
    __asm {
        mov ax, 0002h
        mov bx, segment
        int 31h
        sbb cx, cx
        mov selector, ax
        mov failed, cx
    }
    return failed ? 0 : selector;
}

/* DPMI 0800h: map physical memory; returns the linear address or 0. */
static S3_U32 s3_map_physical(S3_U32 physical, S3_U32 bytes)
{
    S3_U16 phys_hi = (S3_U16)(physical >> 16), phys_lo = (S3_U16)physical;
    S3_U16 size_hi = (S3_U16)(bytes >> 16), size_lo = (S3_U16)bytes;
    S3_U16 lin_hi, lin_lo, failed;
    __asm {
        push si
        push di
        mov bx, phys_hi
        mov cx, phys_lo
        mov si, size_hi
        mov di, size_lo
        mov ax, 0800h
        int 31h
        sbb ax, ax
        pop di
        pop si
        mov failed, ax
        mov lin_hi, bx
        mov lin_lo, cx
    }
    return failed ? 0 : ((S3_U32)lin_hi << 16) | lin_lo;
}

static const S3_U8 S3_FAR *s3_real_pointer(S3_U32 real_far)
{
    S3_U16 selector = s3_segment_selector((S3_U16)(real_far >> 16));
    if (selector == 0) return (const S3_U8 S3_FAR *)0;
    return (const S3_U8 S3_FAR *)MK_FP(selector, (S3_U16)real_far);
}

static int s3_select_vbe(S3_U16 buffer_selector, S3_U16 buffer_segment,
                         S3_U16 width, S3_U16 height, S3_U16 depth,
                         int S3_FAR *is_s3)
{
    S3_U8 S3_FAR *buffer = (S3_U8 S3_FAR *)MK_FP(buffer_selector, 0);
    const S3_U8 S3_FAR *oem;
    const S3_U16 S3_FAR *list;
    S3_U16 version, count = 0, i;
    S3_VBE_MODE_INFO info;
    S3_U32 bytes, linear;

    _fmemcpy(buffer + S3_VBE_INFO_OFFSET, "VBE2", 4);
    if (s3_int10(0x4F00, 0, 0, buffer_segment, S3_VBE_INFO_OFFSET) != 0x004F ||
        _fmemcmp(buffer + S3_VBE_INFO_OFFSET, "VESA", 4) != 0) {
        S3_TRACE_EVENT("vbe.noinfo", 0, 0);
        return 0;
    }
    version = s3_get16(buffer + 4);
    oem = s3_real_pointer(s3_get32(buffer + 6));
    *is_s3 = oem != (const S3_U8 S3_FAR *)0 && oem[0] == 'S' && oem[1] == '3';
    list = (const S3_U16 S3_FAR *)s3_real_pointer(s3_get32(buffer + 14));
    S3_TRACE_EVENT("vbe.info", version, *is_s3);
    if (list == (const S3_U16 S3_FAR *)0) return 0;
    /* 4F01 may reuse the info block that holds the list: copy it first. */
    while (count < S3_VBE_MAX_MODES && list[count] != 0xFFFFU) {
        s3_modes[count] = list[count];
        ++count;
    }
    for (i = 0; i < count; ++i) {
        if (s3_int10(0x4F01, 0, s3_modes[i], buffer_segment,
                     S3_VBE_MODE_OFFSET) != 0x004F) {
            continue;
        }
        S3_VbeDecodeModeInfo(buffer + S3_VBE_MODE_OFFSET, version, &info);
        if (!S3_VbeModeMatches(&info, width, height, depth) ||
            !S3_ModeSetVBE(&s3_mode, info.width, info.height,
                           info.bits_pixel, info.pitch, s3_modes[i],
                           info.lfb_physical)) {
            continue;
        }
        bytes = (s3_mode.framebuffer_bytes + 0xFFFFUL) & ~0xFFFFUL;
        linear = s3_map_physical(s3_mode.lfb_physical, bytes);
        S3_TRACE_EVENT("vbe.mode", s3_modes[i], (S3_U16)(linear >> 16));
        if (linear == 0) {
            S3_ModeDefault(&s3_mode);
            return 0;
        }
        s3_mode.lfb_linear = linear;
        return 1;
    }
    S3_TRACE_EVENT("vbe.nomode", width, depth);
    return 0;
}

int S3_ModeSelect(void)
{
    static int result; /* 0 = not yet, 1 = usable, -1 = failed */
    S3_U16 width, height, depth;
    DWORD dos;
    int is_s3 = 0, found;

    if (result != 0) return result > 0;
    width = (S3_U16)GetPrivateProfileInt(S3_INI_SECTION, "Width", 0, S3_INI_FILE);
    height = (S3_U16)GetPrivateProfileInt(S3_INI_SECTION, "Height", 0, S3_INI_FILE);
    depth = (S3_U16)GetPrivateProfileInt(S3_INI_SECTION, "Depth", 0, S3_INI_FILE);
    if (width == 0 || height == 0) {
        width = S3_WIDTH;
        height = S3_HEIGHT;
    }
    if (depth == 0) depth = S3_BITS_PIXEL;
    S3_TRACE_EVENT("mode.request", width, (S3_U16)(height << 6 | depth));

    dos = GlobalDosAlloc(1024);
    if (dos == 0) {
        result = -1;
        return 0;
    }
    S3_ModeDefault(&s3_mode);
    found = 0;
    /* The S3 backend serves its native mode on S3 hardware; everything else
       goes through the VESA BIOS. */
    {
        S3_U8 S3_FAR *buffer = (S3_U8 S3_FAR *)MK_FP(LOWORD(dos), 0);
        _fmemcpy(buffer, "VBE2", 4);
        if (s3_int10(0x4F00, 0, 0, HIWORD(dos), 0) == 0x004F) {
            const S3_U8 S3_FAR *oem = s3_real_pointer(s3_get32(buffer + 6));
            is_s3 = oem != (const S3_U8 S3_FAR *)0 && oem[0] == 'S' &&
                    oem[1] == '3';
        }
    }
    if (is_s3 && width == S3_WIDTH && height == S3_HEIGHT &&
        depth == S3_BITS_PIXEL) {
        found = 1;
    } else {
        found = s3_select_vbe(LOWORD(dos), HIWORD(dos), width, height, depth,
                              &is_s3);
        if (!found && is_s3) {
            /* Unavailable request on S3 hardware: native mode instead. */
            S3_ModeDefault(&s3_mode);
            found = 1;
        }
    }
    GlobalDosFree(LOWORD(dos));
    S3_TRACE_EVENT("mode.selected", s3_mode.backend, s3_mode.vbe_mode);
    result = found ? 1 : -1;
    return found;
}

static int S3_FAR S3_PASCAL s3_vbe_capture_mode(void S3_FAR *context,
                                                S3_U8 S3_FAR *buffer,
                                                S3_U16 bytes)
{
    (void)context;
    (void)buffer;
    (void)bytes;
    if (s3_int10(0x4F03, 0, 0, 0, 0) == 0x004F) {
        s3_saved_bios_mode = (S3_U16)s3_rmcs.ebx & 0x3FFFU;
    } else {
        s3_saved_bios_mode = (S3_U16)(s3_int10(0x0F00, 0, 0, 0, 0) & 0x7F);
    }
    S3_TRACE_EVENT("vbe.capture", s3_saved_bios_mode, 0);
    return 1;
}

static int S3_FAR S3_PASCAL s3_vbe_program_mode(void S3_FAR *context)
{
    S3_U16 ax;
    (void)context;
    ax = s3_int10(0x4F02, (S3_U16)(s3_mode.vbe_mode | 0x4000U), 0, 0, 0);
    S3_TRACE_EVENT("vbe.set", s3_mode.vbe_mode, ax);
    if (ax != 0x004F) return 0;
    if (s3_mode.bits_pixel == 8 && !S3_Win16LoadPalette()) return 0;
    return 1;
}

static int S3_FAR S3_PASCAL s3_vbe_restore_mode(void S3_FAR *context,
                                                const S3_U8 S3_FAR *buffer,
                                                S3_U16 bytes)
{
    (void)context;
    (void)buffer;
    (void)bytes;
    if (s3_saved_bios_mode < 0x100) {
        (void)s3_int10(s3_saved_bios_mode, 0, 0, 0, 0);
    } else if (s3_int10(0x4F02, s3_saved_bios_mode, 0, 0, 0) != 0x004F) {
        (void)s3_int10(0x0003, 0, 0, 0, 0);
    }
    return 1;
}

/* Banks move the selector base (see S3_Win16BridgeOpenLinear). */
static int S3_FAR S3_PASCAL s3_vbe_set_bank(void S3_FAR *context, S3_U32 bank)
{
    (void)context;
    return bank == 0;
}

int S3_VbeModeOps(S3_BRIDGE_OPS S3_FAR *ops)
{
    if (ops == (S3_BRIDGE_OPS S3_FAR *)0) return 0;
    ops->context = (void S3_FAR *)0;
    ops->capture_mode = s3_vbe_capture_mode;
    ops->program_mode = s3_vbe_program_mode;
    ops->restore_mode = s3_vbe_restore_mode;
    ops->set_bank = s3_vbe_set_bank;
    return 1;
}
#endif
