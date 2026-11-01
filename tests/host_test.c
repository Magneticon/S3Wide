#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include "s3_abi.h"
#include "s3_hw.h"
#include "s3_vga_state.h"
#include "s3_win16_bridge.h"
#include "s3_surface.h"
#include "s3_device.h"

static int failures;

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        ++failures; \
    } \
} while (0)

typedef struct BANK_TEST_CONTEXT {
    S3_U8 vram[256];
    S3_U8 *window;
    S3_U32 calls;
    S3_U32 last_bank;
    S3_U32 fail_bank;
} BANK_TEST_CONTEXT;

typedef struct LARGE_BANK_CONTEXT {
    S3_U8 *vram;
    S3_U32 framebuffer_bytes;
    S3_U32 bank_bytes;
    S3_U32 calls;
} LARGE_BANK_CONTEXT;

typedef struct REGISTER_TEST_CONTEXT {
    S3_U8 crtc[256];
    S3_U8 writes[16][2];
    S3_U16 write_count;
    S3_U8 palette[768];
    S3_U16 palette_count;
    S3_U8 palette_index;
} REGISTER_TEST_CONTEXT;

typedef struct MODE_EVENT {
    S3_U8 kind;
    S3_U8 index;
    S3_U8 value;
} MODE_EVENT;

typedef struct MODE_TEST_CONTEXT {
    S3_U8 crtc[256];
    S3_U8 seq[256];
    S3_U8 misc;
    MODE_EVENT writes[512];
    unsigned int write_count;
    S3_U8 drop_crtc_index;
    S3_U8 drop_crtc_once;
} MODE_TEST_CONTEXT;

typedef struct FULL_VGA_TEST_CONTEXT {
    S3_U8 seq[256], crtc[256], gc[16], ac[32], misc, dac_mask;
    S3_U8 dac[768];
    S3_U16 dac_read_pos, dac_write_pos;
    int drop_dac_once;
} FULL_VGA_TEST_CONTEXT;

static S3_U8 full_read_seq(void *p,S3_U8 i){return ((FULL_VGA_TEST_CONTEXT*)p)->seq[i];}
static void full_write_seq(void *p,S3_U8 i,S3_U8 v){((FULL_VGA_TEST_CONTEXT*)p)->seq[i]=v;}
static S3_U8 full_read_crtc(void *p,S3_U8 i){return ((FULL_VGA_TEST_CONTEXT*)p)->crtc[i];}
static void full_write_crtc(void *p,S3_U8 i,S3_U8 v){((FULL_VGA_TEST_CONTEXT*)p)->crtc[i]=v;}
static S3_U8 full_read_gc(void *p,S3_U8 i){return ((FULL_VGA_TEST_CONTEXT*)p)->gc[i];}
static void full_write_gc(void *p,S3_U8 i,S3_U8 v){((FULL_VGA_TEST_CONTEXT*)p)->gc[i]=v;}
static S3_U8 full_read_ac(void *p,S3_U8 i){return ((FULL_VGA_TEST_CONTEXT*)p)->ac[i];}
static void full_write_ac(void *p,S3_U8 i,S3_U8 v){((FULL_VGA_TEST_CONTEXT*)p)->ac[i]=v;}
static S3_U8 full_read_misc(void *p){return ((FULL_VGA_TEST_CONTEXT*)p)->misc;}
static void full_write_misc(void *p,S3_U8 v){((FULL_VGA_TEST_CONTEXT*)p)->misc=v;}
static S3_U8 full_read_dac(void *p){FULL_VGA_TEST_CONTEXT*c=p;return c->dac[c->dac_read_pos++%768];}
static S3_U8 full_read_dac_mask(void *p){return ((FULL_VGA_TEST_CONTEXT*)p)->dac_mask;}
static void full_write_dac(void *p,S3_U16 port,S3_U8 v){FULL_VGA_TEST_CONTEXT*c=p;
    if(port==0x03C6)c->dac_mask=v;
    else if(port==0x03C7)c->dac_read_pos=(S3_U16)(v%768);
    else if(port==0x03C8)c->dac_write_pos=(S3_U16)(v%768);
    else if(port==0x03C9){if(c->drop_dac_once)c->drop_dac_once=0;else c->dac[c->dac_write_pos++%768]=v;}}
static void full_enable_ac(void *p){(void)p;}

static S3_U8 test_read_crtc(void *opaque, S3_U16 index_port, S3_U8 index)
{
    REGISTER_TEST_CONTEXT *context = (REGISTER_TEST_CONTEXT *)opaque;
    CHECK(index_port == 0x03D4 || index_port == 0x03B4);
    return context->crtc[index];
}

static void test_write_crtc(void *opaque, S3_U16 index_port, S3_U8 index, S3_U8 value)
{
    REGISTER_TEST_CONTEXT *context = (REGISTER_TEST_CONTEXT *)opaque;
    CHECK(index_port == 0x03D4 || index_port == 0x03B4);
    if (context->write_count < 16) {
        context->writes[context->write_count][0] = index;
        context->writes[context->write_count][1] = value;
        ++context->write_count;
    }
    if (index != 0x35 || context->crtc[0x38] == 0x48) {
        context->crtc[index] = value;
    }
}

static void test_write_dac(void *opaque, S3_U16 port, S3_U8 value)
{
    REGISTER_TEST_CONTEXT *context = (REGISTER_TEST_CONTEXT *)opaque;
    if (port == 0x03C8) {
        context->palette_index = value;
        context->palette_count = 0;
    } else if (port == 0x03C9 && context->palette_count < 768) {
        context->palette[context->palette_count++] = value;
    }
}

static void mode_log(MODE_TEST_CONTEXT *context, S3_U8 kind,
                     S3_U8 index, S3_U8 value)
{
    if (context->write_count < 512) {
        MODE_EVENT *event = &context->writes[context->write_count++];
        event->kind = kind;
        event->index = index;
        event->value = value;
    }
}

static S3_U8 mode_read_seq(void *opaque, S3_U8 index)
{
    MODE_TEST_CONTEXT *context = (MODE_TEST_CONTEXT *)opaque;
    return context->seq[index];
}

static void mode_write_seq(void *opaque, S3_U8 index, S3_U8 value)
{
    MODE_TEST_CONTEXT *context = (MODE_TEST_CONTEXT *)opaque;
    mode_log(context, 'S', index, value);
    if (index <= 0x08 || context->seq[0x08] == 0x06) {
        context->seq[index] = value;
    }
}

static S3_U8 mode_read_crtc(void *opaque, S3_U16 port, S3_U8 index)
{
    MODE_TEST_CONTEXT *context = (MODE_TEST_CONTEXT *)opaque;
    CHECK(port == 0x03B4 || port == 0x03D4);
    return context->crtc[index];
}

static void mode_write_crtc(void *opaque, S3_U16 port,
                            S3_U8 index, S3_U8 value)
{
    MODE_TEST_CONTEXT *context = (MODE_TEST_CONTEXT *)opaque;
    CHECK(port == 0x03B4 || port == 0x03D4);
    mode_log(context, 'C', index, value);
    if (context->drop_crtc_once && index == context->drop_crtc_index) {
        context->drop_crtc_once = 0;
        return;
    }
    if (index == 0x38) {
        context->crtc[index] = value;
    } else if (index == 0x35) {
        if (context->crtc[0x38] == 0x48) context->crtc[index] = value;
    } else if (index <= 0x05 && (context->crtc[0x35] & 0x20) != 0) {
        return;
    } else if ((index == 0x06 || index == 0x07 || index == 0x09 ||
                index == 0x10 || index == 0x11 || index == 0x15 ||
                index == 0x16) && (context->crtc[0x35] & 0x10) != 0) {
        return;
    } else if (index < 8 && (context->crtc[0x11] & 0x80) != 0) {
        return;
    } else if (index > 0x18 && context->crtc[0x38] != 0x48) {
        return;
    } else {
        context->crtc[index] = value;
    }
}

static S3_U8 mode_read_misc(void *opaque)
{
    return ((MODE_TEST_CONTEXT *)opaque)->misc;
}

static void mode_write_misc(void *opaque, S3_U8 value)
{
    MODE_TEST_CONTEXT *context = (MODE_TEST_CONTEXT *)opaque;
    mode_log(context, 'M', 0, value);
    context->misc = value;
}

static int mode_find_event(const MODE_TEST_CONTEXT *context,
                           S3_U8 kind, S3_U8 index, S3_U8 value,
                           unsigned int start)
{
    unsigned int i;
    for (i = start; i < context->write_count; ++i) {
        if (context->writes[i].kind == kind &&
            context->writes[i].index == index &&
            context->writes[i].value == value) {
            return (int)i;
        }
    }
    return -1;
}

static S3_U8 *test_map_bank(void *opaque, S3_U32 bank)
{
    BANK_TEST_CONTEXT *context = (BANK_TEST_CONTEXT *)opaque;
    ++context->calls;
    context->last_bank = bank;
    if (bank == context->fail_bank || bank * 64 >= sizeof(context->vram)) {
        return (S3_U8 *)0;
    }
    context->window = context->vram + bank * 64;
    return context->window;
}

static S3_U8 *test_map_large_bank(void *opaque, S3_U32 bank)
{
    LARGE_BANK_CONTEXT *context = (LARGE_BANK_CONTEXT *)opaque;
    S3_U32 offset = bank * context->bank_bytes;
    ++context->calls;
    if (offset >= context->framebuffer_bytes) return (S3_U8 *)0;
    return context->vram + offset;
}

static void test_setdibits_to_device_adapter(void)
{
    struct TEST_BITMAPINFO {
        S3_BITMAPINFOHEADER16 header;
        S3_RGBQUAD16 colors[256];
    } info;
    S3_PDEV pdev;
    S3_SURFACE surface;
    S3_RECT16 clip;
    BANK_TEST_CONTEXT context;
    S3_U8 initial_window[64];
    S3_U8 bits[12] = {10, 11, 12, 13, 20, 21, 22, 23, 30, 31, 32, 33};
    S3_I16 translate[256];
    int i;

    memset(&info, 0, sizeof(info));
    memset(&context, 0, sizeof(context));
    memset(context.vram, 0xCC, sizeof(context.vram));
    memset(initial_window, 0, sizeof(initial_window));
    context.fail_bank = 0xFFFFFFFFUL;
    for (i = 0; i < 256; ++i) translate[i] = (S3_I16)i;
    info.header.biSize = sizeof(S3_BITMAPINFOHEADER16);
    info.header.biWidth = 4;
    info.header.biHeight = 3;
    info.header.biPlanes = 1;
    info.header.biBitCount = 8;
    info.header.biCompression = 0;
    info.header.biClrUsed = 256;
    CHECK(S3_SurfaceInit(&surface, 6, 5, 8, 40, 64, 64,
                         initial_window, &context, test_map_bank));
    CHECK(S3_InitializePDEV(&pdev));
    pdev.surface_state = &surface;
    clip.left = 1;
    clip.top = 0;
    clip.right = 3;
    clip.bottom = 2;
    CHECK(SetDIBitsToDevice(&pdev, 0, 0, 0, 3, &clip, (S3_DRAWMODE16 *)0,
                            bits, (S3_BITMAPINFO16 *)&info, translate) == 2);
    CHECK(context.vram[8 + 1] == 21 && context.vram[8 + 2] == 22);
    CHECK(context.vram[1 * 8 + 1] == 21 && context.vram[1 * 8 + 2] == 22);
    CHECK(context.vram[0 * 8 + 1] == 31 && context.vram[0 * 8 + 2] == 32);

    CHECK(SetDIBitsToDevice(&pdev, 0, 0, 2, 2, &clip, (S3_DRAWMODE16 *)0,
                            bits, (S3_BITMAPINFO16 *)&info, translate) == 0);
    info.header.biBitCount = 4;
    CHECK(SetDIBitsToDevice(&pdev, 0, 0, 0, 1, &clip, (S3_DRAWMODE16 *)0,
                            bits, (S3_BITMAPINFO16 *)&info, translate) == 0);
    info.header.biBitCount = 8;
    CHECK(SetDIBitsToDevice(&pdev, 0, 0, 0, 1, &clip, (S3_DRAWMODE16 *)0,
                            bits, (S3_BITMAPINFO16 *)&info, (S3_I16 *)0) == 0);
    translate[21] = 256;
    CHECK(SetDIBitsToDevice(&pdev, 0, 0, 1, 1, &clip, (S3_DRAWMODE16 *)0,
                            bits, (S3_BITMAPINFO16 *)&info, translate) == 0);
    translate[21] = 21;
    CHECK(SetDIBitsToDevice(&pdev, 0, 0, 1, 1, &clip, (S3_DRAWMODE16 *)0,
                            bits, (S3_BITMAPINFO16 *)&info, translate) == 1);
}

static void test_banded_dib_upload_reference(void)
{
    static S3_U8 framebuffer[1280UL * 720UL];
    static S3_U8 dib_bits[1280UL * 720UL];
    static S3_U8 initial_window[65536];
    S3_SURFACE surface;
    LARGE_BANK_CONTEXT context;
    S3_I16 translate[256];
    S3_U16 scan, count, x;
    S3_U32 expected_bytes;

    context.vram = framebuffer;
    context.framebuffer_bytes = sizeof(framebuffer);
    context.bank_bytes = 65536UL;
    context.calls = 0;
    memset(framebuffer, 0xCC, sizeof(framebuffer));
    memset(initial_window, 0, sizeof(initial_window));
    for (scan = 0; scan < 720; ++scan) {
        for (x = 0; x < 1280; ++x) {
            dib_bits[(S3_U32)scan * 1280UL + x] = (S3_U8)(scan * 3U + x);
        }
    }
    for (x = 0; x < 256; ++x) translate[x] = (S3_I16)(x ^ 0xA5U);
    CHECK(S3_SurfaceInit(&surface, 1280, 720, 1280, sizeof(framebuffer),
                         65536UL, 65536UL, initial_window, &context,
                         test_map_large_bank));

    /* A 32-row band is 40,960 bytes: it fits exactly from offset 0x6000 to
       the end of a 64-KiB segment. The full image does not fit one segment. */
    expected_bytes = 1280UL * 32UL;
    CHECK(expected_bytes == 40960UL);
    CHECK(S3_FarSpanFitsSingleSegment(0x6000U, expected_bytes));
    CHECK(!S3_FarSpanFitsSingleSegment(0x6001U, expected_bytes));
    CHECK(!S3_FarSpanFitsSingleSegment(0, sizeof(dib_bits)));
    for (scan = 0; scan < 720; scan = (S3_U16)(scan + count)) {
        count = (S3_U16)((720U - scan) > 32U ? 32U : (720U - scan));
        CHECK(S3_SurfaceSetDIB8Band(&surface, 0, 0, 1280, 720, 1280,
                                    scan, count,
                                    &dib_bits[(S3_U32)scan * 1280UL],
                                    (S3_U32)count * 1280UL, 0x6000U,
                                    translate, 256, (S3_RECT16 *)0) == count);
    }
    CHECK(context.calls >= 14);
    for (scan = 0; scan < 720; ++scan) {
        S3_U32 dst_row = (S3_U32)(719U - scan) * 1280UL;
        for (x = 0; x < 1280; ++x) {
            S3_U8 source = (S3_U8)(scan * 3U + x);
            CHECK(framebuffer[dst_row + x] == (S3_U8)(source ^ 0xA5U));
        }
    }
    CHECK(S3_SurfaceSetDIB8Band(&surface, 0, 0, 1280, 720, 1280,
                                0, 32, dib_bits, 40960UL, 0x6001U,
                                translate, 256, (S3_RECT16 *)0) == 0);
    CHECK(S3_SurfaceSetDIB8Band(&surface, 0, 0, 1280, 720, 1280,
                                0, 1, dib_bits, 1279UL, 0,
                                translate, 256, (S3_RECT16 *)0) == 0);
    CHECK(S3_SurfaceSetDIB8Band(&surface, 0, 0, 1280, 720, 1280,
                                0, 1, dib_bits, 1280UL, 0,
                                translate, 255, (S3_RECT16 *)0) == 0);
}

static void test_gdi_contract(void)
{
    S3_GDIINFO info;
    S3_PDEV pdev;
    S3_GDIINFO output;
    S3_U8 *bytes = (S3_U8 *)&output;
    unsigned int i;

    CHECK(sizeof(info) == 110);
    CHECK(offsetof(S3_GDIINFO, dpMLoWin) == 48);
    CHECK(offsetof(S3_GDIINFO, dpSpotSizeX) == 96);
    CHECK(offsetof(S3_GDIINFO, dpPalColors) == 104);
    CHECK(sizeof(pdev) == 36);
    CHECK(S3_BuildGDIInfo(&info));
    CHECK(info.dpVersion == 0x030A);
    CHECK(info.dpTechnology == 1);
    CHECK(info.dpHorzRes == 1280 && info.dpVertRes == 720);
    CHECK(info.dpBitsPixel == 8 && info.dpPlanes == 1);
    CHECK(info.dpDEVICEsize == sizeof(S3_PDEV));
    CHECK(info.dpRaster == 0);
    CHECK(S3_InitializePDEV(&pdev));
    CHECK(pdev.pdType != 0);
    CHECK(pdev.width == 1280 && pdev.height == 720);
    CHECK(pdev.pitch == 1280 && pdev.bits_pixel == 8 && pdev.planes == 1);
    CHECK(pdev.framebuffer_bytes == 921600);
    CHECK(pdev.bank_count == 15);

    memset(&output, 0xA5, sizeof(output));
    CHECK(Enable(&output, S3_INQUIRE_INFO, (char *)0, (char *)0, (char *)0) == 110);
    CHECK(output.dpHorzRes == 1280 && output.dpVertRes == 720);
    CHECK(Enable((void *)0, S3_INQUIRE_INFO, (char *)0, (char *)0, (char *)0) == 0);
    CHECK(Enable(&pdev, S3_ENABLE_DEVICE, (char *)0, (char *)0, (char *)0) == 0);
    CHECK(Enable(&pdev, 2, (char *)0, (char *)0, (char *)0) == 0);
    for (i = 0; i < sizeof(output); ++i) {
        CHECK(bytes[i] != 0xA5);
    }
}

static void test_gdi_entrypoint_declines(void)
{
    S3_I16 (S3_FAR S3_PASCAL *bitblt_fn)(void S3_FAR *, S3_U16, S3_U16,
                                          void S3_FAR *, S3_U16, S3_U16,
                                          S3_U16, S3_U16, S3_I32,
                                          void S3_FAR *, void S3_FAR *) = BitBlt;
    S3_U16 (S3_FAR S3_PASCAL *output_fn)(void S3_FAR *, S3_U16, S3_U16,
                                          S3_POINT16 S3_FAR *, void S3_FAR *,
                                          void S3_FAR *, void S3_FAR *,
                                          S3_RECT16 S3_FAR *) = Output;
    S3_U32 (S3_FAR S3_PASCAL *pixel_fn)(void S3_FAR *, S3_U16, S3_U16,
                                         S3_U32, void S3_FAR *) = Pixel;

    CHECK(sizeof(S3_RECT16) == 8);
    CHECK(offsetof(S3_RECT16, right) == 4);
    CHECK(offsetof(S3_RECT16, bottom) == 6);
    /* Invalid sentinels prove these decline paths don't inspect DDI pointers. */
    CHECK(bitblt_fn((void *)0, 0, 0, (void *)1, 0, 0, 1, 1,
                    (S3_I32)S3_ROP3_SRCCOPY, (void *)0, (void *)0) == 0);
    CHECK(bitblt_fn((void *)0, 0, 0, (void *)0, 0, 0, 1, 1,
                    0, (void *)0, (void *)0) == 0);
    CHECK(output_fn((void *)1, 0, 1, (S3_POINT16 *)1, (void *)1,
                    (void *)1, (void *)1, (S3_RECT16 *)1) == S3_DDA_NOT_SUPPORTED);
    CHECK(pixel_fn((void *)0, 0, 0, 0xFF000000UL, (void *)1) == S3_PIXEL_ERROR);
    CHECK(pixel_fn((void *)0, 0, 0, 0, (void *)0) == S3_PIXEL_ERROR);
}

static void test_gdi_bitmap_and_pixel_path(void)
{
    S3_PDEV pdev;
    S3_SURFACE surface;
    S3_BITMAP16 bitmap;
    S3_DRAWMODE16 draw_mode;
    S3_DIB8_VIEW top_down_view;
    BANK_TEST_CONTEXT context;
    S3_U8 initial_window[64];
    S3_U8 dib_bits[12] = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12
    };
    S3_U8 too_large[1] = {0};
    S3_U8 top_down_bits[8] = {21, 22, 0, 0, 31, 32, 0, 0};
    int x, y;

    memset(&context, 0, sizeof(context));
    memset(context.vram, 0xCC, sizeof(context.vram));
    memset(initial_window, 0, sizeof(initial_window));
    context.fail_bank = 0xFFFFFFFFUL;
    CHECK(S3_SurfaceInit(&surface, 6, 5, 8, 40, 64, 64,
                         initial_window, &context, test_map_bank));
    CHECK(S3_InitializePDEV(&pdev));
    pdev.surface_state = &surface;

    bitmap.bmType = 0;
    bitmap.bmWidth = 4;
    bitmap.bmHeight = 3;
    bitmap.bmWidthBytes = 4;
    bitmap.bmPlanes = 1;
    bitmap.bmBitsPixel = 8;
    bitmap.bmBits = dib_bits;
    bitmap.bmWidthPlanes = 12;
    bitmap.bmlpPDevice = 0;
    bitmap.bmSegmentIndex = 0;
    bitmap.bmScanSegment = 0;
    bitmap.bmFillBytes = 0;
    bitmap.reserved1 = 0;
    bitmap.reserved2 = 0;
    CHECK(BitBlt(&pdev, 1, 1, &bitmap, 0, 0, 4, 3,
                 (S3_I32)S3_ROP3_SRCCOPY, (void *)0, (void *)0));
    for (y = 0; y < 5; ++y) {
        for (x = 0; x < 8; ++x) {
            S3_U8 expected = 0xCC;
            if (y == 1 && x >= 1 && x < 5) expected = (S3_U8)x;
            if (y == 2 && x >= 1 && x < 5) expected = (S3_U8)(x + 4);
            if (y == 3 && x >= 1 && x < 5) expected = (S3_U8)(x + 8);
            CHECK(context.vram[y * 8 + x] == expected);
        }
    }

    CHECK(Pixel(&pdev, 3, 2, 0, (void *)0) == 0xFF000007UL);
    memset(&draw_mode, 0, sizeof(draw_mode));
    draw_mode.Rop2 = S3_R2_COPYPEN;
    CHECK(Pixel(&pdev, 0, 0, 0xFF00002AUL, &draw_mode) == 1);
    CHECK(context.vram[0] == 0x2A);
    draw_mode.Rop2 = 7;
    CHECK(Pixel(&pdev, 0, 0, 0xFF000055UL, &draw_mode) == S3_PIXEL_ERROR);
    CHECK(context.vram[0] == 0x2A);
    draw_mode.Rop2 = S3_R2_COPYPEN;
    CHECK(Pixel(&pdev, 0, 0, 0x00000055UL, &draw_mode) == S3_PIXEL_ERROR);
    CHECK(Pixel(&pdev, 6, 0, 0, (void *)0) == S3_PIXEL_ERROR);

    bitmap.bmSegmentIndex = 1;
    CHECK(!BitBlt(&pdev, 0, 0, &bitmap, 0, 0, 1, 1,
                  (S3_I32)S3_ROP3_SRCCOPY, (void *)0, (void *)0));
    bitmap.bmSegmentIndex = 0;

    CHECK(!BitBlt(&pdev, 0, 0, &bitmap, 0, 0, 4, 3,
                  (S3_I32)0x00EE0086L, (void *)0, (void *)0));
    bitmap.bmBitsPixel = 4;
    CHECK(!BitBlt(&pdev, 0, 0, &bitmap, 0, 0, 4, 3,
                  (S3_I32)S3_ROP3_SRCCOPY, (void *)0, (void *)0));

    bitmap.bmBitsPixel = 8;
    bitmap.bmWidth = 1280;
    bitmap.bmHeight = 64;
    bitmap.bmWidthBytes = 1280;
    bitmap.bmBits = too_large;
    CHECK(!BitBlt(&pdev, 0, 0, &bitmap, 0, 0, 1, 1,
                  (S3_I32)S3_ROP3_SRCCOPY, (void *)0, (void *)0));
    pdev.surface_state = (void *)0;
    CHECK(Pixel(&pdev, 0, 0, 0, (void *)0) == S3_PIXEL_ERROR);

    top_down_view.width = 2;
    top_down_view.height = 2;
    top_down_view.stride = 4;
    top_down_view.data_bytes = sizeof(top_down_bits);
    top_down_view.bits = top_down_bits;
    top_down_view.top_down = 1;
    CHECK(S3_SurfaceBlitDIB8(&surface, 5, 4, &top_down_view,
                             0, 0, 2, 2, S3_ROP3_SRCCOPY));
    CHECK(context.vram[4 * 8 + 5] == 21);
    CHECK(S3_SurfaceBlitDIB8(&surface, 0, 0, &top_down_view,
                             -1, 0, 2, 2, S3_ROP3_SRCCOPY));
    CHECK(context.vram[1] == 21 && context.vram[1 * 8 + 1] == 31);
    top_down_view.data_bytes = 7;
    CHECK(!S3_SurfaceBlitDIB8(&surface, 0, 0, &top_down_view,
                              0, 0, 2, 2, S3_ROP3_SRCCOPY));
    top_down_view.data_bytes = sizeof(top_down_bits);
    top_down_view.top_down = 2;
    CHECK(!S3_SurfaceBlitDIB8(&surface, 0, 0, &top_down_view,
                              0, 0, 2, 2, S3_ROP3_SRCCOPY));
    top_down_view.top_down = 1;
    CHECK(!S3_SurfaceBlitDIB8(&surface, 0, 0, &top_down_view,
                              0, 0, 2, 2, 0));

    CHECK(S3_FarSpanFitsSingleSegment(0, 65536UL));
    CHECK(S3_FarSpanFitsSingleSegment(0x1000U, 0xF000UL));
    CHECK(!S3_FarSpanFitsSingleSegment(0x1001U, 0xF000UL));
    CHECK(!S3_FarSpanFitsSingleSegment(0xFFFFU, 2));
    CHECK(!S3_FarSpanFitsSingleSegment(1, 0xFFFFFFFFUL));
}

static void test_color_contract(void)
{
    S3_U32 physical = 0;
    S3_U32 rgb;
    S3_U8 index;

    rgb = S3_ColorInfoCore(0x000000FFUL, &physical);
    CHECK(physical == 0xFF0000E0UL);
    CHECK(rgb == 0x000000FFUL);
    CHECK(S3_ColorInfoCore(0xFF0000E0UL, (S3_U32 *)0) == 0x000000FFUL);

    rgb = S3_ColorInfoCore(0x0000FF00UL, &physical);
    CHECK(physical == 0xFF00001CUL);
    CHECK(rgb == 0x0000FF00UL);
    CHECK(S3_ColorInfoCore(0xFF00001CUL, (S3_U32 *)0) == 0x0000FF00UL);

    rgb = S3_ColorInfoCore(0x00FF0000UL, &physical);
    CHECK(physical == 0xFF000003UL);
    CHECK(rgb == 0x00FF0000UL);
    CHECK(S3_ColorInfoCore(0xFF000003UL, (S3_U32 *)0) == 0x00FF0000UL);

    CHECK(S3_ColorInfoCore(0xFF00002AUL, &physical) == 0xFF00002AUL);
    CHECK(physical == 0xFF00002AUL);
    CHECK(S3_RGBToColorIndex(0x00FFFFFFUL, &index) && index == 0xFF);
    CHECK(S3_ColorIndexToRGB(0xFF, &rgb) && rgb == 0x00FFFFFFUL);
    CHECK(!S3_RGBToColorIndex(0xFF000000UL, &index));
    CHECK(ColorInfo((S3_PDEV *)0, 0x000000FFUL, &physical) == 0x000000FFUL);
    CHECK(physical == 0xFF0000E0UL);
    {
        S3_U16 i;
        for (i = 0; i < 256; ++i) {
            CHECK(S3_ColorIndexToRGB((S3_U8)i, &rgb));
            CHECK(S3_RGBToColorIndex(rgb, &index));
            CHECK(index == (S3_U8)i);
        }
    }
    CHECK(GetDriverResourceID(3, "OEMBIN") == 3);
    CHECK(GetDriverResourceID(37, "CUSTOM") == 37);
    CHECK(GetDriverResourceID(3, (char *)0) == 3);
}

static void test_banked_surface(void)
{
    S3_SURFACE surface;
    BANK_TEST_CONTEXT context;
    S3_U8 initial_window[64];
    int i;
    memset(&context, 0, sizeof(context));
    memset(context.vram, 0xCC, sizeof(context.vram));
    context.fail_bank = 0xFFFFFFFFUL;
    memset(initial_window, 0, sizeof(initial_window));

    CHECK(S3_SurfaceInit(&surface, 10, 10, 12, 120, 64, 64,
                         initial_window, &context, test_map_bank));
    CHECK(S3_SurfaceFillRect(&surface, 4, 4, 8, 8, 0x5A));
    for (i = 0; i < 256; ++i) {
        int x = i % 12;
        int y = i / 12;
        int should_be_filled = y >= 4 && y < 8 && x >= 4 && x < 8;
        if (i < 120 && x < 10 && y < 10) {
            CHECK(context.vram[i] == (should_be_filled ? 0x5A : 0xCC));
        } else {
            CHECK(context.vram[i] == 0xCC);
        }
    }
    CHECK(context.calls == 2);
    CHECK(S3_SurfacePutPixel(&surface, 0, 6, 0xAA));
    CHECK(context.vram[72] == 0xAA);
    CHECK(!S3_SurfacePutPixel(&surface, -1, 0, 0));
    CHECK(!S3_SurfacePutPixel(&surface, 10, 0, 0));
    CHECK(!S3_SurfaceFillRect(&surface, 0, 0, 11, 1, 1));
    CHECK(!S3_SurfaceFillRect(&surface, 4, 5, 3, 6, 1));
    CHECK(S3_SurfaceFillRect(&surface, 10, 10, 10, 10, 1));
    CHECK(!S3_SurfaceInit(&surface, 10, 10, 9, 120, 64, 64,
                          initial_window, &context, test_map_bank));
    CHECK(!S3_SurfaceInit(&surface, 10, 10, 12, 119, 64, 64,
                          initial_window, &context, test_map_bank));

    context.fail_bank = 1;
    surface.current_bank = 0xFFFFFFFFUL;
    CHECK(!S3_SurfacePutPixel(&surface, 0, 6, 0xDD));
}

static void test_clipped_rectangles(void)
{
    S3_SURFACE surface;
    BANK_TEST_CONTEXT context;
    S3_U8 initial_window[64];
    int x, y;
    memset(&context, 0, sizeof(context));
    memset(context.vram, 0xCC, sizeof(context.vram));
    memset(initial_window, 0, sizeof(initial_window));
    context.fail_bank = 0xFFFFFFFFUL;
    CHECK(S3_SurfaceInit(&surface, 10, 10, 12, 120, 64, 64,
                         initial_window, &context, test_map_bank));
    CHECK(S3_SurfaceFillRectClipped(&surface, -2, -1, 4, 4, 0x77));
    CHECK(S3_SurfaceFillRectClipped(&surface, 8, 8, 13, 12, 0x88));
    CHECK(S3_SurfaceFillRectClipped(&surface, -8, 2, -1, 7, 0x99));
    CHECK(S3_SurfaceFillRectClipped(&surface, 0, 5, 4, 5, 0x99));
    CHECK(!S3_SurfaceFillRectClipped(&surface, 5, 5, 4, 8, 0x99));
    for (y = 0; y < 10; ++y) {
        for (x = 0; x < 10; ++x) {
            S3_U8 expected = 0xCC;
            if (x < 4 && y < 4) expected = 0x77;
            if (x >= 8 && y >= 8) expected = 0x88;
            CHECK(context.vram[y * 12 + x] == expected);
        }
    }
    for (x = 120; x < 256; ++x) {
        CHECK(context.vram[x] == 0xCC);
    }
}

static void test_line_raster(void)
{
    S3_SURFACE surface;
    BANK_TEST_CONTEXT context;
    S3_U8 initial_window[64];
    int x, y;
    memset(&context, 0, sizeof(context));
    memset(context.vram, 0xCC, sizeof(context.vram));
    memset(initial_window, 0, sizeof(initial_window));
    context.fail_bank = 0xFFFFFFFFUL;
    CHECK(S3_SurfaceInit(&surface, 10, 10, 12, 120, 64, 64,
                         initial_window, &context, test_map_bank));
    CHECK(S3_SurfaceDrawLine(&surface, 0, 5, 9, 5, 0x71));
    CHECK(context.vram[60] == 0x71 && context.vram[63] == 0x71);
    CHECK(context.vram[64] == 0x71 && context.vram[69] == 0x71);
    CHECK(context.calls == 2 && context.last_bank == 1);
    CHECK(S3_SurfaceDrawLine(&surface, 1, 1, 5, 4, 0x72));
    CHECK(context.vram[13] == 0x72 && context.vram[26] == 0x72);
    CHECK(context.vram[39] == 0x72 && context.vram[40] == 0x72);
    CHECK(context.vram[53] == 0x72);
    CHECK(S3_SurfaceDrawLine(&surface, 5, 4, 1, 1, 0x73));
    CHECK(context.vram[53] == 0x73 && context.vram[13] == 0x73);
    CHECK(S3_SurfaceDrawLine(&surface, 3, 3, 3, 3, 0x74));
    CHECK(context.vram[39] == 0x74);
    CHECK(!S3_SurfaceDrawLine(&surface, -1, 0, 2, 2, 0x75));
    CHECK(!S3_SurfaceDrawLine(&surface, 0, 0, 10, 0, 0x75));
    for (y = 0; y < 10; ++y) {
        for (x = 0; x < 12; ++x) {
            if ((x >= 10 || y >= 10) && y * 12 + x < 120) {
                CHECK(context.vram[y * 12 + x] == 0xCC);
            }
        }
    }
}

static void flat_line_reference(S3_U8 expected[256],
                                S3_I16 x0, S3_I16 y0,
                                S3_I16 x1, S3_I16 y1,
                                const S3_RECT16 *clip, S3_U8 color)
{
    S3_I32 x = x0, y = y0, end_x = x1, end_y = y1;
    S3_I32 dx = end_x >= x ? end_x - x : x - end_x;
    S3_I32 dy = end_y >= y ? y - end_y : end_y - y;
    S3_I32 sx = x < end_x ? 1 : -1;
    S3_I32 sy = y < end_y ? 1 : -1;
    S3_I32 error = dx + dy;
    S3_I32 left = clip->left > 0 ? clip->left : 0;
    S3_I32 top = clip->top > 0 ? clip->top : 0;
    S3_I32 right = clip->right < 10 ? clip->right : 10;
    S3_I32 bottom = clip->bottom < 10 ? clip->bottom : 10;
    if (clip->right <= clip->left || clip->bottom <= clip->top) return;
    for (;;) {
        S3_I32 twice_error;
        if (x == end_x && y == end_y) break;
        if (x >= left && x < right && y >= top && y < bottom) {
            expected[y * 12 + x] = color;
        }
        twice_error = error * 2;
        if (twice_error >= dy) {
            error += dy;
            x += sx;
        }
        if (twice_error <= dx) {
            error += dx;
            y += sy;
        }
    }
}

static void test_clipped_line_reference(void)
{
    BANK_TEST_CONTEXT context;
    S3_SURFACE surface;
    S3_U8 initial_window[64], expected[256];
    S3_U32 seed = 0x18B77E31UL;
    unsigned int iteration, i;
    for (iteration = 0; iteration < 256; ++iteration) {
        S3_I16 x0, y0, x1, y1;
        S3_RECT16 clip;
        S3_U8 color;
        int result;
        memset(&context, 0, sizeof(context));
        for (i = 0; i < 256; ++i) {
            context.vram[i] = (S3_U8)(i * 17U + iteration * 31U + 0x53U);
        }
        memcpy(expected, context.vram, sizeof(expected));
        context.fail_bank = 0xFFFFFFFFUL;
        memset(initial_window, 0, sizeof(initial_window));
        CHECK(S3_SurfaceInit(&surface, 10, 10, 12, 120, 64, 64,
                             initial_window, &context, test_map_bank));
        seed = seed * 1664525UL + 1013904223UL;
        x0 = (S3_I16)((seed >> 8) % 36U) - 13;
        seed = seed * 1664525UL + 1013904223UL;
        y0 = (S3_I16)((seed >> 8) % 36U) - 13;
        seed = seed * 1664525UL + 1013904223UL;
        x1 = (S3_I16)((seed >> 8) % 36U) - 13;
        seed = seed * 1664525UL + 1013904223UL;
        y1 = (S3_I16)((seed >> 8) % 36U) - 13;
        seed = seed * 1664525UL + 1013904223UL;
        clip.left = (S3_I16)((seed >> 8) % 20U) - 5;
        seed = seed * 1664525UL + 1013904223UL;
        clip.top = (S3_I16)((seed >> 8) % 20U) - 5;
        seed = seed * 1664525UL + 1013904223UL;
        clip.right = (S3_I16)((seed >> 8) % 20U) - 5;
        seed = seed * 1664525UL + 1013904223UL;
        clip.bottom = (S3_I16)((seed >> 8) % 20U) - 5;
        seed = seed * 1664525UL + 1013904223UL;
        color = (S3_U8)(seed >> 24);
        flat_line_reference(expected, x0, y0, x1, y1, &clip, color);
        result = S3_SurfaceDrawLineClipped(&surface, x0, y0, x1, y1,
                                           &clip, color);
        CHECK(result);
        CHECK(memcmp(context.vram, expected, sizeof(expected)) == 0);
    }
}

static void test_realized_pen_and_output_polyline(void)
{
    S3_PDEV pdev;
    S3_SURFACE surface;
    S3_RECT16 clip;
    S3_DRAWMODE16 mode;
    S3_LPEN16 logical_pen;
    S3_PPEN16 physical_pen;
    S3_POINT16 points[3];
    BANK_TEST_CONTEXT context;
    S3_U8 initial_window[64];

    memset(&context, 0, sizeof(context));
    memset(context.vram, 0xCC, sizeof(context.vram));
    memset(initial_window, 0, sizeof(initial_window));
    context.fail_bank = 0xFFFFFFFFUL;
    CHECK(S3_SurfaceInit(&surface, 10, 10, 12, 120, 64, 64,
                         initial_window, &context, test_map_bank));
    CHECK(S3_InitializePDEV(&pdev));
    pdev.surface_state = &surface;
    memset(&mode, 0, sizeof(mode));
    mode.Rop2 = S3_R2_COPYPEN;
    logical_pen.lopnStyle = S3_LS_SOLID;
    logical_pen.lopnWidth.x = 0;
    logical_pen.lopnWidth.y = 0;
    logical_pen.lopnColor = 0x000000FFUL;
    CHECK(RealizeObject(&pdev, S3_OBJ_PEN, &logical_pen, (void *)0, (void *)0) ==
          sizeof(S3_PPEN16));
    memset(&physical_pen, 0, sizeof(physical_pen));
    CHECK(RealizeObject(&pdev, S3_OBJ_PEN, &logical_pen, &physical_pen,
                        (void *)0) == sizeof(S3_PPEN16));
    CHECK(physical_pen.style == S3_LS_SOLID);
    CHECK(physical_pen.width.x == 1 && physical_pen.width.y == 0);
    CHECK(physical_pen.color == 0xFF0000E0UL);

    points[0].x = -2; points[0].y = 2;
    points[1].x = 4;  points[1].y = 2;
    points[2].x = 4;  points[2].y = 6;
    clip.left = 1; clip.top = 2; clip.right = 5; clip.bottom = 5;
    CHECK(Output(&pdev, S3_OS_POLYLINE, 3, points, &physical_pen,
                 (void *)0, &mode, &clip) == 1);
    CHECK(context.vram[2 * 12 + 0] == 0xE0);
    CHECK(context.vram[2 * 12 + 1] == 0xE0);
    CHECK(context.vram[2 * 12 + 2] == 0xE0);
    CHECK(context.vram[2 * 12 + 3] == 0xE0);
    CHECK(context.vram[2 * 12 + 4] == 0xE0);
    CHECK(context.vram[3 * 12 + 4] == 0xE0);
    CHECK(context.vram[4 * 12 + 4] == 0xE0);
    CHECK(context.vram[5 * 12 + 4] == 0xE0);
    CHECK(context.vram[6 * 12 + 4] == 0xCC);
    /* dpClip is zero; Output must ignore lpClipRect, including its contents. */
    memset(context.vram, 0xCC, sizeof(context.vram));
    CHECK(Output(&pdev, S3_OS_POLYLINE, 3, points, &physical_pen,
                 (void *)0, &mode, (S3_RECT16 *)1) == 1);
    CHECK(context.vram[2 * 12 + 0] == 0xE0);

    mode.Rop2 = 7;
    CHECK(Output(&pdev, S3_OS_POLYLINE, 3, points, &physical_pen,
                 (void *)0, &mode, &clip) == S3_DDA_NOT_SUPPORTED);
    mode.Rop2 = S3_R2_COPYPEN;
    CHECK(Output(&pdev, S3_OS_POLYLINE, 1, points, &physical_pen,
                 (void *)0, &mode, &clip) == 0);
    CHECK(Output(&pdev, S3_OS_POLYLINE, 0xFFFFU, points, &physical_pen,
                 (void *)0, &mode, &clip) == 0);
    CHECK(Output(&pdev, 6, 1, (S3_POINT16 *)1, (void *)1,
                 (void *)1, (void *)1, (S3_RECT16 *)1) ==
          S3_DDA_NOT_SUPPORTED);
    CHECK(RealizeObject(&pdev, 2, &logical_pen, &physical_pen, (void *)0) == 0);

    logical_pen.lopnWidth.x = -1;
    CHECK(RealizeObject(&pdev, S3_OBJ_PEN, &logical_pen, &physical_pen,
                        (void *)0) == 0);
    logical_pen.lopnWidth.x = 0;
    logical_pen.lopnStyle = S3_LS_NOLINE;
    CHECK(RealizeObject(&pdev, S3_OBJ_PEN, &logical_pen, &physical_pen,
                        (void *)0) == sizeof(S3_PPEN16));
    CHECK(physical_pen.style == S3_LS_NOLINE);
    memset(context.vram, 0xCC, sizeof(context.vram));
    CHECK(Output(&pdev, S3_OS_POLYLINE, 3, points, &physical_pen,
                 (void *)0, &mode, &clip) == 1);
    CHECK(context.vram[2 * 12 + 1] == 0xCC);
    CHECK(RealizeObject(&pdev, 0xFFFFU, &physical_pen, &physical_pen,
                        (void *)0) == 1);
    CHECK(RealizeObject(&pdev, 0xFFFFU, (void *)0, (void *)0, (void *)0) == 0);

    logical_pen.lopnWidth.x = 0;
    logical_pen.lopnStyle = S3_LS_SOLID;
    CHECK(RealizeObject(&pdev, S3_OBJ_PEN, &logical_pen, &physical_pen,
                        (void *)0) == sizeof(S3_PPEN16));
    points[0].x = 0; points[0].y = 5;
    points[1].x = 10; points[1].y = 5;
    clip.left = 0; clip.top = 0; clip.right = 10; clip.bottom = 10;
    context.fail_bank = 1;
    CHECK(Output(&pdev, S3_OS_POLYLINE, 2, points, &physical_pen,
                 (void *)0, &mode, &clip) == 0);
    CHECK(context.vram[60] == 0xE0 && context.vram[63] == 0xE0);
    CHECK(context.vram[64] == 0xCC);
}

static void flat_copy_reference(S3_U8 destination[256],
                                const S3_U8 source[256],
                                S3_I16 destination_x, S3_I16 destination_y,
                                S3_I16 source_x, S3_I16 source_y,
                                S3_U16 width, S3_U16 height)
{
    S3_U16 x, y;
    for (y = 0; y < height; ++y) {
        for (x = 0; x < width; ++x) {
            S3_I32 dx = (S3_I32)destination_x + x;
            S3_I32 dy = (S3_I32)destination_y + y;
            S3_I32 sx = (S3_I32)source_x + x;
            S3_I32 sy = (S3_I32)source_y + y;
            if (dx >= 0 && dx < 10 && dy >= 0 && dy < 10 &&
                sx >= 0 && sx < 10 && sy >= 0 && sy < 10) {
                destination[dy * 12 + dx] = source[sy * 12 + sx];
            }
        }
    }
}

static void test_source_copy_reference(void)
{
    BANK_TEST_CONTEXT source_context, destination_context;
    S3_SURFACE source_surface, destination_surface;
    S3_U8 source_initial[256], destination_initial[256], expected[256];
    S3_U8 source_window[64], destination_window[64];
    S3_U32 seed = 0x51A3C0DEUL;
    unsigned int iteration, i;

    for (iteration = 0; iteration < 64; ++iteration) {
        S3_I16 dx, dy, sx, sy;
        S3_U16 width, height;
        int same_surface = (iteration & 1) == 0;
        int result;
        memset(&source_context, 0, sizeof(source_context));
        memset(&destination_context, 0, sizeof(destination_context));
        for (i = 0; i < 256; ++i) {
            source_context.vram[i] = (S3_U8)(i * 13U + iteration * 7U + 3U);
            destination_context.vram[i] =
                (S3_U8)(i * 29U + iteration * 11U + 0x40U);
        }
        source_context.fail_bank = 0xFFFFFFFFUL;
        destination_context.fail_bank = 0xFFFFFFFFUL;
        memcpy(source_initial, source_context.vram, sizeof(source_initial));
        memcpy(destination_initial, destination_context.vram,
               sizeof(destination_initial));

        seed = seed * 1664525UL + 1013904223UL;
        dx = (S3_I16)((seed >> 8) % 20) - 5;
        seed = seed * 1664525UL + 1013904223UL;
        dy = (S3_I16)((seed >> 8) % 20) - 5;
        seed = seed * 1664525UL + 1013904223UL;
        sx = (S3_I16)((seed >> 8) % 20) - 5;
        seed = seed * 1664525UL + 1013904223UL;
        sy = (S3_I16)((seed >> 8) % 20) - 5;
        seed = seed * 1664525UL + 1013904223UL;
        width = (S3_U16)((seed >> 8) % 16);
        seed = seed * 1664525UL + 1013904223UL;
        height = (S3_U16)((seed >> 8) % 16);

        if (same_surface) {
            memcpy(destination_context.vram, source_context.vram,
                   sizeof(destination_context.vram));
            memcpy(destination_initial, destination_context.vram,
                   sizeof(destination_initial));
            memcpy(source_initial, destination_context.vram,
                   sizeof(source_initial));
        }
        memcpy(expected, destination_initial, sizeof(expected));
        flat_copy_reference(expected, source_initial, dx, dy, sx, sy,
                            width, height);
        CHECK(S3_SurfaceInit(&source_surface, 10, 10, 12, 120, 64, 64,
                             source_window, &source_context, test_map_bank));
        CHECK(S3_SurfaceInit(&destination_surface, 10, 10, 12, 120, 64, 64,
                             destination_window, &destination_context,
                             test_map_bank));
        if (same_surface) {
            result = S3_SurfaceCopyRect(&destination_surface, dx, dy,
                                        &destination_surface, sx, sy,
                                        width, height);
        } else {
            result = S3_SurfaceCopyRect(&destination_surface, dx, dy,
                                        &source_surface, sx, sy,
                                        width, height);
        }
        CHECK(result);
        CHECK(memcmp(destination_context.vram, expected, sizeof(expected)) == 0);
    }

    memset(&source_context, 0, sizeof(source_context));
    memset(&destination_context, 0, sizeof(destination_context));
    for (i = 0; i < 256; ++i) {
        source_context.vram[i] = (S3_U8)(i ^ 0x5A);
        destination_context.vram[i] = (S3_U8)(i ^ 0xA5);
    }
    source_context.fail_bank = 0xFFFFFFFFUL;
    destination_context.fail_bank = 0xFFFFFFFFUL;
    CHECK(S3_SurfaceInit(&source_surface, 10, 10, 12, 120, 64, 64,
                         source_window, &source_context, test_map_bank));
    CHECK(S3_SurfaceInit(&destination_surface, 10, 10, 12, 120, 64, 64,
                         destination_window, &destination_context,
                         test_map_bank));
    memcpy(expected, destination_context.vram, sizeof(expected));
    flat_copy_reference(expected, source_context.vram, 0, 5, 0, 5, 10, 1);
    CHECK(S3_SurfaceCopyRect(&destination_surface, 0, 5, &source_surface,
                             0, 5, 10, 1));
    CHECK(memcmp(destination_context.vram, expected, sizeof(expected)) == 0);
    CHECK(source_context.calls >= 2 && destination_context.calls >= 2);

    CHECK(S3_SurfaceInit(&destination_surface, 10, 10, 12, 120, 64, 64,
                         destination_window, &destination_context,
                         test_map_bank));
    memcpy(expected, destination_context.vram, sizeof(expected));
    flat_copy_reference(expected, destination_context.vram, 2, 5, 0, 5, 8, 1);
    CHECK(S3_SurfaceCopyRect(&destination_surface, 2, 5, &destination_surface,
                             0, 5, 8, 1));
    CHECK(memcmp(destination_context.vram, expected, sizeof(expected)) == 0);
    memcpy(expected, destination_context.vram, sizeof(expected));
    flat_copy_reference(expected, destination_context.vram, 0, 4, 0, 2, 10, 5);
    CHECK(S3_SurfaceCopyRect(&destination_surface, 0, 4, &destination_surface,
                             0, 2, 10, 5));
    CHECK(memcmp(destination_context.vram, expected, sizeof(expected)) == 0);
}

static void test_s3_register_contract(void)
{
    REGISTER_TEST_CONTEXT context;
    S3_REGISTER_IO io;
    unsigned int i;
    memset(&context, 0, sizeof(context));
    context.crtc[0x38] = 0x00;
    context.crtc[0x35] = 0xA0;
    context.crtc[0x51] = 0xA3;
    io.crtc_index_port = 0x03D4;
    io.context = &context;
    io.read_crtc = test_read_crtc;
    io.write_crtc = test_write_crtc;
    io.write_dac = test_write_dac;

    CHECK(S3_Select64KBank(&io, 14));
    CHECK(context.crtc[0x35] == 0xAE);
    CHECK(context.crtc[0x51] == 0xA3);
    CHECK(((context.crtc[0x35] & 0x0F) | ((context.crtc[0x51] & 0x0C) << 2)) == 14);
    CHECK(context.crtc[0x38] == 0x00);
    CHECK(context.write_count == 4);
    CHECK(context.writes[0][0] == 0x38 && context.writes[0][1] == 0x48);
    CHECK(context.writes[1][0] == 0x35 && context.writes[1][1] == 0xAE);
    CHECK(context.writes[2][0] == 0x51 && context.writes[2][1] == 0xA3);
    CHECK(context.writes[3][0] == 0x38 && context.writes[3][1] == 0x00);

    CHECK(S3_Select64KBank(&io, 63));
    CHECK(context.crtc[0x35] == 0xAF);
    CHECK(context.crtc[0x51] == 0xAF);
    CHECK(((context.crtc[0x35] & 0x0F) | ((context.crtc[0x51] & 0x0C) << 2)) == 63);
    CHECK(!S3_Select64KBank(&io, 64));
    io.crtc_index_port = 0x03C4;
    CHECK(!S3_Select64KBank(&io, 0));
    io.crtc_index_port = 0x03D4;

    CHECK(S3_ProgramRGB332Palette6(&io));
    CHECK(context.palette_index == 0);
    CHECK(context.palette_count == 768);
    CHECK(context.palette[0] == 0 && context.palette[1] == 0 && context.palette[2] == 0);
    CHECK(context.palette[3] == 0 && context.palette[4] == 0 && context.palette[5] == 21);
    CHECK(context.palette[765] == 63 && context.palette[766] == 63 && context.palette[767] == 63);
    for (i = 0; i < 256; ++i) {
        S3_U32 rgb;
        S3_U8 expected_red, expected_green, expected_blue;
        CHECK(S3_ColorIndexToRGB((S3_U8)i, &rgb));
        expected_red = (S3_U8)((((rgb & 0xFF) * 63) + 127) / 255);
        expected_green = (S3_U8)(((((rgb >> 8) & 0xFF) * 63) + 127) / 255);
        expected_blue = (S3_U8)(((((rgb >> 16) & 0xFF) * 63) + 127) / 255);
        CHECK(context.palette[i * 3] == expected_red);
        CHECK(context.palette[i * 3 + 1] == expected_green);
        CHECK(context.palette[i * 3 + 2] == expected_blue);
    }
    io.write_dac = (S3_WRITE_DAC)0;
    CHECK(!S3_ProgramRGB332Palette6(&io));
}

static void test_full_vga_bridge_snapshot(void)
{
    FULL_VGA_TEST_CONTEXT c;
    S3_VGA_STATE_IO io;
    S3_VGA_STATE original, observed;
    S3_U8 opaque[S3_BRIDGE_MODE_STATE_BYTES];
    unsigned int i;
    memset(&c,0,sizeof(c));
    for(i=0;i<256;i++){c.seq[i]=(S3_U8)(i^0x31);c.crtc[i]=(S3_U8)(i^0x72);}
    c.crtc[0x11] |= 0x80;
    for(i=0;i<16;i++)c.gc[i]=(S3_U8)(i*3);
    for(i=0;i<32;i++)c.ac[i]=(S3_U8)(i*5);
    for(i=0;i<768;i++)c.dac[i]=(S3_U8)(i%64);
    c.misc=0xA5;c.dac_mask=0xFF;
    memset(&io,0,sizeof(io));io.context=&c;io.crtc_port=0x03D4;
    io.read_seq=full_read_seq;io.write_seq=full_write_seq;
    io.read_crtc=full_read_crtc;io.write_crtc=full_write_crtc;
    io.read_gc=full_read_gc;io.write_gc=full_write_gc;
    io.read_ac=full_read_ac;io.write_ac=full_write_ac;
    io.read_misc=full_read_misc;io.write_misc=full_write_misc;
    io.read_dac=full_read_dac;io.read_dac_mask=full_read_dac_mask;
    io.write_dac=full_write_dac;io.enable_ac_display=full_enable_ac;
    CHECK(S3_VgaCaptureState(&io,&original));
    CHECK(original.valid==1 && sizeof(original)<=sizeof(opaque));
    CHECK(S3_VgaBridgeCapture(&io,opaque,sizeof(opaque)));
    memset(c.seq,0x11,sizeof(c.seq));memset(c.crtc,0x22,sizeof(c.crtc));
    memset(c.gc,0x33,sizeof(c.gc));memset(c.ac,0x44,sizeof(c.ac));
    memset(c.dac,0x55,sizeof(c.dac));c.misc=0;c.dac_mask=0;
    c.drop_dac_once=1;
    CHECK(!S3_VgaBridgeRestore(&io,opaque,sizeof(opaque)));
    CHECK(S3_VgaBridgeRestore(&io,opaque,sizeof(opaque)));
    CHECK(S3_VgaCaptureState(&io,&observed));
    CHECK(memcmp(&original,&observed,sizeof(original))==0);
    CHECK(!S3_VgaBridgeRestore(&io,opaque,(S3_U16)(sizeof(original)-1)));
}

static void test_s3_mode_snapshot_and_candidate_sequence(void)
{
    static const S3_U8 ext_regs[S3_MODE_EXT_REG_COUNT] = {
        0x42, 0x43, 0x50, 0x51, 0x5D, 0x5E, 0x67, 0x31, 0x58, 0x35
    };
    MODE_TEST_CONTEXT context;
    S3_MODE_IO io;
    S3_MODE_SNAPSHOT snapshot;
    S3_U8 original_crtc[S3_MODE_CRTC_REG_COUNT];
    S3_U8 original_extended[S3_MODE_EXT_REG_COUNT];
    S3_U8 original_crtc3a;
    S3_U8 original_seq[5];
    S3_U8 original_misc;
    unsigned int i;
    int blank_at, unlock_seq_at, unlock_crtc_at, unlock_timing_at;
    int first_timing_at, pulse_at;

    memset(&context, 0, sizeof(context));
    for (i = 0; i < 256; ++i) {
        context.crtc[i] = (S3_U8)(i ^ 0xA5);
        context.seq[i] = (S3_U8)(i ^ 0x5A);
    }
    context.crtc[0x11] = 0x83;
    context.crtc[0x38] = 0x17;
    context.crtc[0x42] = 0xE3;
    context.crtc[0x43] = 0xA5;
    context.crtc[0x50] = 0xF5;
    context.crtc[0x51] = 0x8D;
    context.crtc[0x5D] = 0xF7;
    context.crtc[0x5E] = 0xFF;
    context.crtc[0x67] = 0xF7;
    context.crtc[0x31] = 0x62;
    context.crtc[0x58] = 0xB5;
    context.crtc[0x35] = 0x30;
    context.crtc[0x3A] = 0x05;
    context.seq[0x01] = 0x01;
    context.seq[0x08] = 0xA5;
    context.seq[0x12] = 0xD1;
    context.seq[0x13] = 0x67;
    context.seq[0x15] = 0x65;
    context.misc = 0x63;
    io.context = &context;
    io.crtc_index_port = 0x03D4;
    io.read_seq = mode_read_seq;
    io.write_seq = mode_write_seq;
    io.read_crtc = mode_read_crtc;
    io.write_crtc = mode_write_crtc;
    io.read_misc = mode_read_misc;
    io.write_misc = mode_write_misc;
    memset(&snapshot, 0, sizeof(snapshot));

    CHECK(S3_CaptureModeSnapshot(&io, &snapshot));
    CHECK(snapshot.valid == 1);
    CHECK(context.seq[0x08] == 0xA5 && context.crtc[0x38] == 0x17);
    for (i = 0; i < S3_MODE_CRTC_REG_COUNT; ++i) {
        original_crtc[i] = snapshot.crtc[i];
    }
    for (i = 0; i < S3_MODE_EXT_REG_COUNT; ++i) {
        original_extended[i] = snapshot.extended[i];
    }
    original_crtc3a = snapshot.crtc3a;
    original_seq[0] = snapshot.seq01;
    original_seq[1] = snapshot.seq08;
    original_seq[2] = snapshot.seq12;
    original_seq[3] = snapshot.seq13;
    original_seq[4] = snapshot.seq15;
    original_misc = snapshot.misc_output;

    context.write_count = 0;
    CHECK(S3_Program720p8bppCandidate(&io, &snapshot));
    CHECK(snapshot.valid == 1);
    CHECK(context.misc == 0xEF);
    CHECK(context.seq[0x12] == 0x23 && context.seq[0x13] == 0x32);
    CHECK(context.seq[0x08] == 0xA5 && context.seq[0x01] == 0x01);
    CHECK(context.crtc[0x00] == 0xCB && context.crtc[0x01] == 0x9F);
    CHECK(context.crtc[0x02] == 0xA0 && context.crtc[0x04] == 0xA7);
    CHECK(context.crtc[0x06] == 0xE8 && context.crtc[0x07] == 0xF0);
    CHECK(context.crtc[0x09] == 0x60 && context.crtc[0x10] == 0xD1);
    CHECK(context.crtc[0x11] == 0x84 && context.crtc[0x12] == 0xCF);
    CHECK(context.crtc[0x0C] == 0x00 && context.crtc[0x0D] == 0x00);
    CHECK(context.crtc[0x13] == 0xA0 && context.crtc[0x15] == 0xD0);
    CHECK(context.crtc[0x16] == 0xFF && context.crtc[0x17] == 0xA3);
    CHECK(context.crtc[0x3A] == 0x15);
    CHECK(context.crtc[0x18] == 0xFF);
    CHECK(context.crtc[0x42] == 0x00 && context.crtc[0x43] == 0x00);
    CHECK(context.crtc[0x50] == 0xC4 && context.crtc[0x51] == 0x8C);
    CHECK(context.crtc[0x5D] == 0xE0 && context.crtc[0x5E] == 0xA8);
    CHECK(context.crtc[0x67] == 0x00 && context.crtc[0x31] == 0x4B);
    CHECK(context.crtc[0x58] == 0xA4 && context.crtc[0x38] == 0x17);
    CHECK(context.crtc[0x35] == 0x30);

    blank_at = mode_find_event(&context, 'S', 0x01, 0x21, 0);
    unlock_seq_at = mode_find_event(&context, 'S', 0x08, 0x06,
                                    (unsigned int)(blank_at + 1));
    unlock_crtc_at = mode_find_event(&context, 'C', 0x38, 0x48,
                                     (unsigned int)(blank_at + 1));
    unlock_timing_at = mode_find_event(&context, 'C', 0x35, 0x00,
                                       (unsigned int)(unlock_crtc_at + 1));
    first_timing_at = mode_find_event(&context, 'C', 0x00, 0xCB,
                                      (unsigned int)(unlock_timing_at + 1));
    pulse_at = mode_find_event(&context, 'S', 0x15, 0x45, 0);
    CHECK(blank_at >= 0 && unlock_seq_at > blank_at);
    CHECK(unlock_crtc_at > blank_at && pulse_at >= 0);
    CHECK(unlock_timing_at > unlock_crtc_at && first_timing_at > unlock_timing_at);
    if (pulse_at >= 0 && (unsigned int)(pulse_at + 3) < context.write_count) {
        CHECK(context.writes[pulse_at + 1].kind == 'S' &&
              context.writes[pulse_at + 1].index == 0x15 &&
              context.writes[pulse_at + 1].value == 0x65);
        CHECK(context.writes[pulse_at + 2].kind == 'S' &&
              context.writes[pulse_at + 2].index == 0x15 &&
              context.writes[pulse_at + 2].value == 0x45);
        CHECK(context.writes[pulse_at + 3].kind == 'S' &&
              context.writes[pulse_at + 3].index == 0x15 &&
              context.writes[pulse_at + 3].value == 0x65);
    } else {
        CHECK(0);
    }

    for (i = 0; i < S3_MODE_CRTC_REG_COUNT; ++i) {
        context.crtc[i] = (S3_U8)~original_crtc[i];
    }
    for (i = 0; i < S3_MODE_EXT_REG_COUNT; ++i) {
        context.crtc[ext_regs[i]] = (S3_U8)~original_extended[i];
    }
    context.crtc[0x38] = 0x48;
    context.seq[0x01] = 0x21;
    context.seq[0x08] = 0x06;
    context.seq[0x12] = 0x00;
    context.seq[0x13] = 0x00;
    context.seq[0x15] = 0x45;
    context.misc = 0x0C;
    context.write_count = 0;
    CHECK(S3_RestoreModeSnapshot(&io, &snapshot));
    CHECK(context.write_count > 0);
    CHECK(context.writes[0].kind == 'S' && context.writes[0].index == 0x01 &&
          context.writes[0].value == 0x21);
    CHECK(context.writes[1].kind == 'S' && context.writes[1].index == 0x08 &&
          context.writes[1].value == 0x06);
    CHECK(context.writes[2].kind == 'C' && context.writes[2].index == 0x38 &&
          context.writes[2].value == 0x48);
    CHECK(context.misc == original_misc);
    CHECK(context.seq[0x01] == original_seq[0]);
    CHECK(context.seq[0x08] == original_seq[1]);
    CHECK(context.seq[0x12] == original_seq[2]);
    CHECK(context.seq[0x13] == original_seq[3]);
    CHECK(context.seq[0x15] == original_seq[4]);
    for (i = 0; i < S3_MODE_CRTC_REG_COUNT; ++i) {
        CHECK(context.crtc[i] == original_crtc[i]);
    }
    CHECK(context.crtc[0x38] == snapshot.crtc38);
    CHECK(context.crtc[0x3A] == original_crtc3a);
    CHECK(context.crtc[0x35] == snapshot.extended[9]);
    for (i = 0; i < S3_MODE_EXT_REG_COUNT; ++i) {
        CHECK(context.crtc[ext_regs[i]] == original_extended[i]);
    }
    CHECK(context.writes[context.write_count - 2].kind == 'C' &&
          context.writes[context.write_count - 2].index == 0x0E &&
          context.writes[context.write_count - 2].value == original_crtc[0x0E]);
    CHECK(context.writes[context.write_count - 1].kind == 'C' &&
          context.writes[context.write_count - 1].index == 0x0F &&
          context.writes[context.write_count - 1].value == original_crtc[0x0F]);
    snapshot.valid = 0;
    context.write_count = 0;
    CHECK(!S3_RestoreModeSnapshot(&io, &snapshot));
    CHECK(context.write_count == 0);

    context.drop_crtc_index = 0x50;
    context.drop_crtc_once = 1;
    CHECK(!S3_Program720p8bppCandidate(&io, &snapshot));
    CHECK(context.drop_crtc_once == 0 && snapshot.valid == 1);
    CHECK(context.misc == original_misc);
    CHECK(context.seq[0x01] == original_seq[0]);
    CHECK(context.seq[0x08] == original_seq[1]);
    CHECK(context.seq[0x12] == original_seq[2]);
    CHECK(context.seq[0x13] == original_seq[3]);
    CHECK(context.seq[0x15] == original_seq[4]);
    for (i = 0; i < S3_MODE_CRTC_REG_COUNT; ++i) {
        CHECK(context.crtc[i] == original_crtc[i]);
    }
    CHECK(context.crtc[0x38] == 0x17);
    CHECK(context.crtc[0x3A] == original_crtc3a);
    CHECK(context.crtc[0x35] == original_extended[9]);
    for (i = 0; i < S3_MODE_EXT_REG_COUNT; ++i) {
        CHECK(context.crtc[ext_regs[i]] == original_extended[i]);
    }
}

static void test_s3_mode_candidate_fields(void)
{
    S3_U8 sr12 = 0, sr13 = 0, m, n, r, crtc_offset;
    S3_U16 pitch = 0;
    S3_U32 clock_khz;
    S3_U32 frame_pixels = 1664UL * 746UL;
    S3_U32 target_clock_hz = 60UL * frame_pixels;
    S3_U32 actual_clock_hz;
    S3_U32 delta;

    /* Candidate from VESA GTF and the pinned X.Org Trio PLL search
       constraints: M=50, N1=3, N2=1 -> SR12=23h, SR13=32h. */
    CHECK(S3_EncodeVideoPLL(50, 3, 1, &sr12, &sr13));
    CHECK(sr12 == 0x23 && sr13 == 0x32);
    CHECK(S3_DecodeVideoPLL(sr12, sr13, &m, &n, &r));
    CHECK(m == 50 && n == 3 && r == 1);
    clock_khz = S3_VideoPLLClockKHz(m, n, r);
    CHECK(clock_khz == 74453);
    actual_clock_hz = clock_khz * 1000;
    delta = (actual_clock_hz > target_clock_hz) ?
            actual_clock_hz - target_clock_hz : target_clock_hz - actual_clock_hz;
    CHECK(delta * 1000 < target_clock_hz); /* less than 0.1% refresh error */
    CHECK(!S3_EncodeVideoPLL(128, 3, 2, &sr12, &sr13));
    CHECK(!S3_EncodeVideoPLL(50, 3, 4, &sr12, &sr13));
    {
        unsigned int test_m, test_n, test_r;
        for (test_r = 0; test_r <= 3; ++test_r) {
            for (test_n = 0; test_n <= 31; ++test_n) {
                for (test_m = 0; test_m <= 127; ++test_m) {
                    S3_U8 test_sr12, test_sr13, decoded_m, decoded_n, decoded_r;
                    CHECK(S3_EncodeVideoPLL((S3_U8)test_m, (S3_U8)test_n,
                                            (S3_U8)test_r, &test_sr12, &test_sr13));
                    CHECK(S3_DecodeVideoPLL(test_sr12, test_sr13,
                                            &decoded_m, &decoded_n, &decoded_r));
                    CHECK(decoded_m == test_m && decoded_n == test_n && decoded_r == test_r);
                }
            }
        }
    }

    CHECK(S3_Encode8BppPitch(1280, &crtc_offset));
    CHECK(crtc_offset == 160);
    CHECK(S3_Decode8BppPitch(crtc_offset, &pitch));
    CHECK(pitch == 1280);
    CHECK(!S3_Encode8BppPitch(1281, &crtc_offset));
    CHECK(!S3_Encode8BppPitch(2048, &crtc_offset));

    CHECK((719UL * pitch + 1279UL) == 921599UL);
    CHECK((719UL * pitch + 1279UL) < 921600UL);
    CHECK((921599UL / S3_BANK_BYTES) == 14);
}

typedef struct BRIDGE_TEST_CONTEXT {
    int fail_alloc;
    int fail_convert;
    int fail_base;
    int fail_limit;
    int fail_get_base;
    int fail_get_limit;
    int fail_window;
    int fail_capture;
    int fail_program;
    int fail_restore;
    int fail_free;
    int fail_bank;
    unsigned int free_count;
    unsigned int restore_count;
    unsigned int bank_count;
    S3_U32 base;
    S3_U32 limit;
    S3_U32 original_limit;
    S3_U32 free_original_limit;
    S3_U32 last_bank;
    S3_U8 mode_value;
    S3_U8 mode_snapshot;
    S3_U8 window_byte;
} BRIDGE_TEST_CONTEXT;

static int S3_FAR S3_PASCAL bridge_test_current_cs(void S3_FAR *context,
                                                   S3_U16 S3_FAR *selector)
{
    (void)context;
    *selector = 0x1234;
    return 1;
}

static int S3_FAR S3_PASCAL bridge_test_alloc(void S3_FAR *opaque,
                                             S3_U16 source,
                                             S3_U16 S3_FAR *selector,
                                             S3_U32 S3_FAR *original_limit)
{
    BRIDGE_TEST_CONTEXT *context = (BRIDGE_TEST_CONTEXT *)opaque;
    CHECK(source == 0x1234);
    if (context->fail_alloc) return 0;
    *selector = 0x2000;
    *original_limit = context->original_limit != 0
        ? context->original_limit : 0x0200UL;
    return 1;
}

static int S3_FAR S3_PASCAL bridge_test_convert(void S3_FAR *opaque,
                                               S3_U16 source,
                                               S3_U16 selector)
{
    BRIDGE_TEST_CONTEXT *context = (BRIDGE_TEST_CONTEXT *)opaque;
    CHECK(source == 0x1234 && selector == 0x2000);
    return !context->fail_convert;
}

static int S3_FAR S3_PASCAL bridge_test_base(void S3_FAR *opaque,
                                            S3_U16 selector,
                                            S3_U32 base)
{
    BRIDGE_TEST_CONTEXT *context = (BRIDGE_TEST_CONTEXT *)opaque;
    CHECK(selector == 0x2000);
    if (context->fail_base) return 0;
    context->base = base;
    return 1;
}

static int S3_FAR S3_PASCAL bridge_test_limit(void S3_FAR *opaque,
                                             S3_U16 selector,
                                             S3_U32 limit)
{
    BRIDGE_TEST_CONTEXT *context = (BRIDGE_TEST_CONTEXT *)opaque;
    CHECK(selector == 0x2000);
    if (context->fail_limit) return 0;
    context->limit = limit;
    return 1;
}

static int S3_FAR S3_PASCAL bridge_test_get_base(void S3_FAR *opaque,
                                                S3_U16 selector,
                                                S3_U32 S3_FAR *base)
{
    BRIDGE_TEST_CONTEXT *context = (BRIDGE_TEST_CONTEXT *)opaque;
    CHECK(selector == 0x2000);
    if (context->fail_get_base) return 0;
    *base = context->base;
    return 1;
}

static int S3_FAR S3_PASCAL bridge_test_get_limit(void S3_FAR *opaque,
                                                 S3_U16 selector,
                                                 S3_U32 S3_FAR *limit)
{
    BRIDGE_TEST_CONTEXT *context = (BRIDGE_TEST_CONTEXT *)opaque;
    CHECK(selector == 0x2000);
    if (context->fail_get_limit) return 0;
    *limit = context->limit;
    return 1;
}

static int S3_FAR S3_PASCAL bridge_test_window(void S3_FAR *opaque,
                                               S3_U16 selector,
                                               void S3_FAR * S3_FAR *window)
{
    BRIDGE_TEST_CONTEXT *context = (BRIDGE_TEST_CONTEXT *)opaque;
    CHECK(selector == 0x2000);
    if (context->fail_window) {
        *window = &context->window_byte;
        return 0;
    }
    *window = &context->window_byte;
    return 1;
}

static int S3_FAR S3_PASCAL bridge_test_free(void S3_FAR *opaque,
                                             S3_U16 selector,
                                             S3_U32 original_limit)
{
    BRIDGE_TEST_CONTEXT *context = (BRIDGE_TEST_CONTEXT *)opaque;
    CHECK(selector == 0x2000);
    if (context->fail_free) return 0;
    context->free_original_limit = original_limit;
    context->limit = original_limit;
    ++context->free_count;
    return 1;
}

static int S3_FAR S3_PASCAL bridge_test_capture(void S3_FAR *opaque,
                                               S3_U8 S3_FAR *snapshot,
                                               S3_U16 bytes)
{
    BRIDGE_TEST_CONTEXT *context = (BRIDGE_TEST_CONTEXT *)opaque;
    CHECK(bytes == S3_BRIDGE_MODE_STATE_BYTES);
    if (context->fail_capture) return 0;
    memset(snapshot, 0, bytes);
    snapshot[0] = context->mode_value;
    context->mode_snapshot = context->mode_value;
    return 1;
}

static int S3_FAR S3_PASCAL bridge_test_program(void S3_FAR *opaque)
{
    BRIDGE_TEST_CONTEXT *context = (BRIDGE_TEST_CONTEXT *)opaque;
    context->mode_value = 0xA5;
    return !context->fail_program;
}

static int S3_FAR S3_PASCAL bridge_test_restore(void S3_FAR *opaque,
                                                const S3_U8 S3_FAR *snapshot,
                                                S3_U16 bytes)
{
    BRIDGE_TEST_CONTEXT *context = (BRIDGE_TEST_CONTEXT *)opaque;
    CHECK(bytes == S3_BRIDGE_MODE_STATE_BYTES);
    ++context->restore_count;
    if (context->fail_restore) return 0;
    context->mode_value = snapshot[0];
    return 1;
}

static int S3_FAR S3_PASCAL bridge_test_bank(void S3_FAR *opaque,
                                            S3_U32 bank)
{
    BRIDGE_TEST_CONTEXT *context = (BRIDGE_TEST_CONTEXT *)opaque;
    if (context->fail_bank) return 0;
    context->last_bank = bank;
    ++context->bank_count;
    return 1;
}

static void bridge_test_ops(S3_BRIDGE_OPS *ops,
                            BRIDGE_TEST_CONTEXT *context)
{
    memset(ops, 0, sizeof(*ops));
    ops->context = context;
    ops->current_code_selector = bridge_test_current_cs;
    ops->alloc_selector = bridge_test_alloc;
    ops->make_data_selector = bridge_test_convert;
    ops->set_selector_base = bridge_test_base;
    ops->set_selector_limit = bridge_test_limit;
    ops->get_selector_base = bridge_test_get_base;
    ops->get_selector_limit = bridge_test_get_limit;
    ops->selector_window = bridge_test_window;
    ops->free_selector = bridge_test_free;
    ops->capture_mode = bridge_test_capture;
    ops->program_mode = bridge_test_program;
    ops->restore_mode = bridge_test_restore;
    ops->set_bank = bridge_test_bank;
}

typedef struct FULL_BRIDGE_CONTEXT {
    BRIDGE_TEST_CONTEXT base;
    S3_VGA_STATE_IO io;
} FULL_BRIDGE_CONTEXT;

static int S3_FAR S3_PASCAL full_bridge_capture(void S3_FAR *p,S3_U8 S3_FAR *b,S3_U16 n)
{ FULL_BRIDGE_CONTEXT*c=(FULL_BRIDGE_CONTEXT*)p;return S3_VgaBridgeCapture(&c->io,b,n); }
static int S3_FAR S3_PASCAL full_bridge_restore(void S3_FAR *p,const S3_U8 S3_FAR *b,S3_U16 n)
{ FULL_BRIDGE_CONTEXT*c=(FULL_BRIDGE_CONTEXT*)p;return S3_VgaBridgeRestore(&c->io,b,n); }

static void test_full_vga_snapshot_in_bridge_close(void)
{
    FULL_BRIDGE_CONTEXT context;
    FULL_VGA_TEST_CONTEXT vga;
    S3_BRIDGE_OPS ops;
    S3_WIN16_BRIDGE bridge;
    S3_VGA_STATE expected, observed;
    unsigned int i;
    memset(&context,0,sizeof(context));memset(&vga,0,sizeof(vga));
    for(i=0;i<256;i++){vga.seq[i]=(S3_U8)(i^0x17);vga.crtc[i]=(S3_U8)(i^0x91);}
    vga.crtc[0x11] |= 0x80;
    for(i=0;i<16;i++)vga.gc[i]=(S3_U8)(i*7);
    for(i=0;i<32;i++)vga.ac[i]=(S3_U8)(i*9);
    for(i=0;i<768;i++)vga.dac[i]=(S3_U8)(i%64);
    vga.misc=0xA5;vga.dac_mask=0xFF;
    context.io.context=&vga;context.io.crtc_port=0x03D4;
    context.io.read_seq=full_read_seq;context.io.write_seq=full_write_seq;
    context.io.read_crtc=full_read_crtc;context.io.write_crtc=full_write_crtc;
    context.io.read_gc=full_read_gc;context.io.write_gc=full_write_gc;
    context.io.read_ac=full_read_ac;context.io.write_ac=full_write_ac;
    context.io.read_misc=full_read_misc;context.io.write_misc=full_write_misc;
    context.io.read_dac=full_read_dac;context.io.read_dac_mask=full_read_dac_mask;
    context.io.write_dac=full_write_dac;context.io.enable_ac_display=full_enable_ac;
    CHECK(S3_VgaCaptureState(&context.io,&expected));
    bridge_test_ops(&ops,&context.base);ops.context=&context;
    ops.capture_mode=full_bridge_capture;ops.restore_mode=full_bridge_restore;
    S3_Win16BridgeInit(&bridge);
    CHECK(S3_Win16BridgeOpen(&bridge,&ops,0xA0000UL,S3_BRIDGE_APERTURE_BYTES));
    memset(vga.seq,0x11,sizeof(vga.seq));memset(vga.crtc,0x22,sizeof(vga.crtc));
    memset(vga.gc,0x33,sizeof(vga.gc));memset(vga.ac,0x44,sizeof(vga.ac));
    memset(vga.dac,0x55,sizeof(vga.dac));vga.misc=0;vga.dac_mask=0;
    vga.drop_dac_once=1;
    CHECK(!S3_Win16BridgeClose(&bridge));
    CHECK(bridge.selector_owned && bridge.mode_snapshot_valid);
    CHECK(S3_Win16BridgeClose(&bridge));
    CHECK(!bridge.selector_owned && !bridge.mode_snapshot_valid);
    CHECK(S3_VgaCaptureState(&context.io,&observed));
    CHECK(memcmp(&expected,&observed,sizeof(expected))==0);
}

static void test_win16_bridge_lifecycle(void)
{
    S3_WIN16_BRIDGE bridge;
    S3_BRIDGE_OPS ops;
    BRIDGE_TEST_CONTEXT context;
    memset(&context, 0, sizeof(context));
    context.mode_value = 0x3C;
    bridge_test_ops(&ops, &context);
    S3_Win16BridgeInit(&bridge);
    CHECK(S3_Win16BridgeOpen(&bridge, &ops, 0x000A0000UL,
                             S3_BRIDGE_APERTURE_BYTES));
    CHECK(bridge.selector_owned && bridge.mode_active);
    CHECK(context.base == 0x000A0000UL && context.limit == 0xFFFFUL);
    CHECK(context.mode_value == 0xA5 && context.bank_count == 1 &&
          context.last_bank == 0);
    CHECK(S3_Win16BridgeSetBank(&bridge, 14));
    CHECK(context.last_bank == 14);
    CHECK(!S3_Win16BridgeSetBank(&bridge, 15));
    context.fail_bank = 1;
    CHECK(!S3_Win16BridgeSetBank(&bridge, 2));
    context.fail_bank = 0;
    CHECK(S3_Win16BridgeClose(&bridge));
    CHECK(context.mode_value == 0x3C && !bridge.selector_owned &&
          context.free_count == 1 && context.free_original_limit == 0x0200UL &&
          context.limit == 0x0200UL);

    memset(&context, 0, sizeof(context));
    context.fail_convert = 1;
    bridge_test_ops(&ops, &context);
    S3_Win16BridgeInit(&bridge);
    CHECK(!S3_Win16BridgeOpen(&bridge, &ops, 0x000A0000UL,
                              S3_BRIDGE_APERTURE_BYTES));
    CHECK(!bridge.selector_owned && context.free_count == 1 &&
          bridge.window == 0 && bridge.linear_base == 0);

    memset(&context, 0, sizeof(context));
    context.fail_alloc = 1;
    bridge_test_ops(&ops, &context);
    S3_Win16BridgeInit(&bridge);
    CHECK(!S3_Win16BridgeOpen(&bridge, &ops, 0x000A0000UL,
                              S3_BRIDGE_APERTURE_BYTES));
    CHECK(!bridge.selector_owned && context.free_count == 0);

    memset(&context, 0, sizeof(context));
    context.fail_base = 1;
    context.fail_free = 1;
    bridge_test_ops(&ops, &context);
    S3_Win16BridgeInit(&bridge);
    CHECK(!S3_Win16BridgeOpen(&bridge, &ops, 0x000A0000UL,
                              S3_BRIDGE_APERTURE_BYTES));
    CHECK(bridge.selector_owned && !bridge.mode_snapshot_valid &&
          bridge.selector_original_limit == 0x0200UL);
    CHECK(bridge.linear_base == 0 && bridge.window == 0);
    context.fail_free = 0;
    CHECK(S3_Win16BridgeClose(&bridge));

    memset(&context, 0, sizeof(context));
    context.fail_limit = 1;
    bridge_test_ops(&ops, &context);
    S3_Win16BridgeInit(&bridge);
    CHECK(!S3_Win16BridgeOpen(&bridge, &ops, 0x000A0000UL,
                              S3_BRIDGE_APERTURE_BYTES));
    CHECK(!bridge.selector_owned && context.free_count == 1 &&
          bridge.window == 0 && bridge.linear_base == 0);

    memset(&context, 0, sizeof(context));
    context.original_limit = 0x10000UL;
    bridge_test_ops(&ops, &context);
    S3_Win16BridgeInit(&bridge);
    CHECK(!S3_Win16BridgeOpen(&bridge, &ops, 0x000A0000UL,
                              S3_BRIDGE_APERTURE_BYTES));
    CHECK(!bridge.selector_owned && context.free_count == 1 &&
          context.free_original_limit == 0x10000UL);

    memset(&context, 0, sizeof(context));
    context.fail_get_base = 1;
    bridge_test_ops(&ops, &context);
    S3_Win16BridgeInit(&bridge);
    CHECK(!S3_Win16BridgeOpen(&bridge, &ops, 0x000A0000UL,
                              S3_BRIDGE_APERTURE_BYTES));
    CHECK(!bridge.selector_owned && context.free_count == 1);

    memset(&context, 0, sizeof(context));
    context.fail_get_limit = 1;
    bridge_test_ops(&ops, &context);
    S3_Win16BridgeInit(&bridge);
    CHECK(!S3_Win16BridgeOpen(&bridge, &ops, 0x000A0000UL,
                              S3_BRIDGE_APERTURE_BYTES));
    CHECK(!bridge.selector_owned && context.free_count == 1);

    memset(&context, 0, sizeof(context));
    context.fail_window = 1;
    bridge_test_ops(&ops, &context);
    S3_Win16BridgeInit(&bridge);
    CHECK(!S3_Win16BridgeOpen(&bridge, &ops, 0x000A0000UL,
                              S3_BRIDGE_APERTURE_BYTES));
    CHECK(!bridge.selector_owned && context.free_count == 1 &&
          bridge.window == 0 && bridge.linear_base == 0);

    memset(&context, 0, sizeof(context));
    context.fail_capture = 1;
    bridge_test_ops(&ops, &context);
    S3_Win16BridgeInit(&bridge);
    CHECK(!S3_Win16BridgeOpen(&bridge, &ops, 0x000A0000UL,
                              S3_BRIDGE_APERTURE_BYTES));
    CHECK(!bridge.selector_owned && !bridge.mode_snapshot_valid &&
          context.free_count == 1);

    memset(&context, 0, sizeof(context));
    context.mode_value = 0x55;
    context.fail_program = 1;
    context.fail_restore = 1;
    bridge_test_ops(&ops, &context);
    S3_Win16BridgeInit(&bridge);
    CHECK(!S3_Win16BridgeOpen(&bridge, &ops, 0x000A0000UL,
                              S3_BRIDGE_APERTURE_BYTES));
    CHECK(bridge.selector_owned && bridge.mode_snapshot_valid &&
          context.free_count == 0 && context.mode_value == 0xA5);
    context.fail_restore = 0;
    CHECK(S3_Win16BridgeClose(&bridge));
    CHECK(context.mode_value == 0x55 && context.free_count == 1);

    memset(&context, 0, sizeof(context));
    context.mode_value = 0x44;
    context.fail_bank = 1;
    bridge_test_ops(&ops, &context);
    S3_Win16BridgeInit(&bridge);
    CHECK(!S3_Win16BridgeOpen(&bridge, &ops, 0x000A0000UL,
                              S3_BRIDGE_APERTURE_BYTES));
    CHECK(context.mode_value == 0x44 && context.free_count == 1 &&
          !bridge.selector_owned && !bridge.mode_snapshot_valid);

    memset(&context, 0, sizeof(context));
    context.mode_value = 0x66;
    bridge_test_ops(&ops, &context);
    S3_Win16BridgeInit(&bridge);
    CHECK(S3_Win16BridgeOpen(&bridge, &ops, 0x000A0000UL,
                             S3_BRIDGE_APERTURE_BYTES));
    context.fail_restore = 1;
    CHECK(!S3_Win16BridgeClose(&bridge));
    CHECK(bridge.selector_owned && bridge.mode_snapshot_valid &&
          context.free_count == 0);
    context.fail_restore = 0;
    context.fail_free = 1;
    CHECK(!S3_Win16BridgeClose(&bridge));
    CHECK(bridge.selector_owned && !bridge.mode_snapshot_valid &&
          bridge.selector_original_limit == 0x0200UL &&
          context.mode_value == 0x66);
    context.fail_free = 0;
    CHECK(S3_Win16BridgeClose(&bridge));
    CHECK(!bridge.selector_owned && context.free_count == 1);
}

static void test_device_lifecycle_surface_and_retry(void)
{
    S3_DEVICE_RUNTIME runtime;
    S3_PDEV device;
    S3_BRIDGE_OPS ops;
    BRIDGE_TEST_CONTEXT context;
    S3_DRAWMODE16 draw_mode;

    memset(&context, 0, sizeof(context));
    context.mode_value = 0x3C;
    memset(&device, 0, sizeof(device));
    bridge_test_ops(&ops, &context);
    S3_DeviceRuntimeInit(&runtime);
    CHECK(S3_DeviceEnableWithOps(&device, &runtime, &ops));
    S3_DeviceRuntimeInit(&runtime);
    CHECK(runtime.enabled && !runtime.cleanup_pending &&
          device.surface_state == &runtime.surface);
    CHECK(device.pdType == S3_PDEV_DISPLAY && device.width == S3_WIDTH &&
          device.height == S3_HEIGHT && device.bits_pixel == S3_BITS_PIXEL &&
          runtime.surface.window == &context.window_byte &&
          runtime.surface.current_bank == 0);
    runtime.surface.current_bank = 0xFFFFFFFFUL;
    memset(&draw_mode, 0, sizeof(draw_mode));
    draw_mode.Rop2 = S3_R2_COPYPEN;
    CHECK(Pixel(&device, 0, 0, 0xFF00005AUL, &draw_mode) == 1);
    CHECK(context.bank_count == 2 && context.last_bank == 0 &&
          context.window_byte == 0x5A);
    CHECK(S3_DeviceDisableWithOps(&device, &runtime));
    CHECK(!runtime.enabled && !runtime.cleanup_pending &&
          device.surface_state == 0 &&
          !runtime.bridge.selector_owned &&
          context.mode_value == 0x3C && context.free_count == 1);

    /* Failed programming plus failed rollback keeps bridge ownership alive;
       Disable retries restore before freeing and clears no live recovery data. */
    memset(&context, 0, sizeof(context));
    context.mode_value = 0x44;
    context.fail_program = 1;
    context.fail_restore = 1;
    memset(&device, 0, sizeof(device));
    bridge_test_ops(&ops, &context);
    S3_DeviceRuntimeInit(&runtime);
    CHECK(!S3_DeviceEnableWithOps(&device, &runtime, &ops));
    CHECK(runtime.cleanup_pending && runtime.device == &device &&
          runtime.bridge.selector_owned && runtime.bridge.mode_snapshot_valid &&
          device.surface_state == 0 && context.free_count == 0);
    context.fail_restore = 0;
    CHECK(S3_DeviceDisableWithOps(&device, &runtime));
    CHECK(!runtime.cleanup_pending && runtime.device == 0 &&
          !runtime.bridge.selector_owned && context.mode_value == 0x44 &&
          context.free_count == 1);

    /* A release failure after successful restore detaches the PDEV and keeps
       the surface inert until a retry completes cleanup. */
    memset(&context, 0, sizeof(context));
    context.mode_value = 0x66;
    memset(&device, 0, sizeof(device));
    bridge_test_ops(&ops, &context);
    S3_DeviceRuntimeInit(&runtime);
    CHECK(S3_DeviceEnableWithOps(&device, &runtime, &ops));
    context.fail_free = 1;
    CHECK(!S3_DeviceDisableWithOps(&device, &runtime));
    CHECK(runtime.cleanup_pending && device.surface_state == 0 &&
          runtime.surface.window == 0 && !runtime.bridge.mode_snapshot_valid &&
          runtime.bridge.selector_owned && context.mode_value == 0x66);
    S3_DeviceRuntimeInit(&runtime);
    CHECK(runtime.cleanup_pending && runtime.bridge.selector_owned &&
          runtime.surface.window == 0);
    context.fail_free = 0;
    /* Retry cleanup without relying on the old PDEV remaining allocated. */
    CHECK(S3_DeviceDisableWithOps(0, &runtime));
    CHECK(device.surface_state == 0 &&
          !runtime.bridge.selector_owned &&
          context.free_count == 1);
    CHECK(Pixel(&device, 0, 0, 0xFF00005AUL, &draw_mode) == S3_PIXEL_ERROR);
}

int main(void)
{
    test_full_vga_bridge_snapshot();
    test_full_vga_snapshot_in_bridge_close();
    test_gdi_contract();
    test_gdi_entrypoint_declines();
    test_realized_pen_and_output_polyline();
    test_gdi_bitmap_and_pixel_path();
    test_setdibits_to_device_adapter();
    test_banded_dib_upload_reference();
    test_color_contract();
    test_banked_surface();
    test_clipped_rectangles();
    test_line_raster();
    test_clipped_line_reference();
    test_source_copy_reference();
    test_s3_register_contract();
    test_s3_mode_snapshot_and_candidate_sequence();
    test_s3_mode_candidate_fields();
    test_win16_bridge_lifecycle();
    test_device_lifecycle_surface_and_retry();
    if (failures != 0) {
        fprintf(stderr, "%d checks failed\n", failures);
        return 1;
    }
    puts("host checks passed: GDIINFO/PDEV, DIB and pen/polyline paths, selector/bank/mode and PDEV/surface lifecycle rollback, S3 bank/DAC and mode save-restore");
    return 0;
}
