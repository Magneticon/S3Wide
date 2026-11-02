#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include "s3_abi.h"
#include "s3_hw.h"
#include "s3_vga_state.h"
#include "s3_win16_bridge.h"
#include "s3_surface.h"
#include "s3_device.h"
#include "s3_brush.h"
#include "s3_raster.h"
#include "s3_vbe.h"
#include "s3_dib.h"
#include "s3_text.h"
#include "s3_output.h"
#include "s3_cursor.h"
#include "s3_mode.h"
#include "s3_pixel.h"
#include "s3_swcursor.h"

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
    /* Without a translate table the RGB colour table is used (non-palette
       device), through the generic DIB path. */
    CHECK(SetDIBitsToDevice(&pdev, 0, 0, 0, 1, &clip, (S3_DRAWMODE16 *)0,
                            bits, (S3_BITMAPINFO16 *)&info, (S3_I16 *)0) == 1);
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
    CHECK(info.dpRaster == (S3_RC_BITBLT | S3_RC_BITMAP64 | S3_RC_GDI20_OUTPUT |
                            S3_RC_DI_BITMAP | S3_RC_DIBTODEV));
    CHECK(info.dpText == (S3_TC_CP_STROKE | S3_TC_RA_ABLE));
    CHECK(info.dpPolygonals == S3_PC_SCANLINE && info.dpLines == S3_LC_POLYLINE);
    CHECK(info.dpClip == S3_CP_RECTANGLE);
    CHECK(info.dpMLoWin.x > 0 && info.dpMLoWin.y > 0);
    CHECK(info.dpMHiWin.x > 0 && info.dpMHiWin.y > 0);
    CHECK(info.dpELoWin.x > 0 && info.dpELoWin.y > 0);
    CHECK(info.dpEHiWin.x > 0 && info.dpEHiWin.y > 0);
    CHECK(info.dpTwpWin.x > 0 && info.dpTwpWin.y > 0);
    CHECK((long)info.dpMHiWin.x * info.dpMLoVpt.x ==
          10L * info.dpMLoWin.x * info.dpMHiVpt.x);
    CHECK((long)info.dpMHiWin.y * info.dpMLoVpt.y ==
          10L * info.dpMLoWin.y * info.dpMHiVpt.y);
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
    draw_mode.Rop2 = 7; /* R2_XORPEN */
    CHECK(Pixel(&pdev, 0, 0, 0xFF000055UL, &draw_mode) == 1);
    CHECK(context.vram[0] == (0x2A ^ 0x55));
    draw_mode.Rop2 = 0;
    CHECK(Pixel(&pdev, 0, 0, 0xFF000055UL, &draw_mode) == S3_PIXEL_ERROR);
    draw_mode.Rop2 = S3_R2_COPYPEN;
    CHECK(Pixel(&pdev, 6, 0, 0, (void *)0) == S3_PIXEL_ERROR);

    bitmap.bmSegmentIndex = 1;
    CHECK(!BitBlt(&pdev, 0, 0, &bitmap, 0, 0, 1, 1,
                  (S3_I32)S3_ROP3_SRCCOPY, (void *)0, (void *)0));
    bitmap.bmSegmentIndex = 0;

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
    S3_RECT16 clip, clip_saved;
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
    /* Clipped to x 1..4 and y 2..4. */
    CHECK(context.vram[2 * 12 + 0] == 0xCC);
    CHECK(context.vram[2 * 12 + 1] == 0xE0);
    CHECK(context.vram[2 * 12 + 2] == 0xE0);
    CHECK(context.vram[2 * 12 + 3] == 0xE0);
    CHECK(context.vram[2 * 12 + 4] == 0xE0);
    CHECK(context.vram[3 * 12 + 4] == 0xE0);
    CHECK(context.vram[4 * 12 + 4] == 0xE0);
    CHECK(context.vram[5 * 12 + 4] == 0xCC);
    CHECK(context.vram[6 * 12 + 4] == 0xCC);
    /* GDIINFO advertises CP_RECTANGLE: Output honours lpClipRect. */
    memset(context.vram, 0xCC, sizeof(context.vram));
    clip_saved = clip;
    clip.left = 1;
    CHECK(Output(&pdev, S3_OS_POLYLINE, 3, points, &physical_pen,
                 (void *)0, &mode, &clip) == 1);
    CHECK(context.vram[2 * 12 + 0] == 0xCC);
    clip = clip_saved;

    /* R2_XORPEN through the generic path: drawing twice restores the pixels. */
    memset(context.vram, 0xCC, sizeof(context.vram));
    mode.Rop2 = 7;
    CHECK(Output(&pdev, S3_OS_POLYLINE, 3, points, &physical_pen,
                 (void *)0, &mode, &clip) == 1);
    CHECK(context.vram[2 * 12 + 3] == (S3_U8)(0xCC ^ 0xE0));
    CHECK(Output(&pdev, S3_OS_POLYLINE, 3, points, &physical_pen,
                 (void *)0, &mode, &clip) == 1);
    CHECK(context.vram[2 * 12 + 3] == 0xCC);
    mode.Rop2 = S3_R2_COPYPEN;
    CHECK(Output(&pdev, S3_OS_POLYLINE, 1, points, &physical_pen,
                 (void *)0, &mode, &clip) == 0);
    CHECK(Output(&pdev, S3_OS_POLYLINE, 0xFFFFU, points, &physical_pen,
                 (void *)0, &mode, &clip) == 0);
    CHECK(Output(&pdev, 6, 1, (S3_POINT16 *)1, (void *)1,
                 (void *)1, (void *)1, (S3_RECT16 *)1) ==
          S3_DDA_NOT_SUPPORTED);

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
    CHECK(context.crtc[0x16] == 0xE7 && context.crtc[0x17] == 0xA3);
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

static void test_realized_brushes(void)
{
    S3_PDEV pdev;
    S3_LBRUSH16 logical;
    S3_PBRUSH16 physical;
    S3_BITMAP16 bitmap;
    S3_U8 mono_bits[8 * 2];
    S3_U8 color_bits[8 * 8];
    unsigned int i;

    memset(&pdev, 0, sizeof(pdev));
    memset(&logical, 0, sizeof(logical));
    logical.lbStyle = S3_BS_SOLID;
    logical.lbColor = 0x000000FFUL; /* RGB red */
    CHECK(RealizeObject(&pdev, S3_OBJ_BRUSH, &logical, (void *)0, (void *)0) ==
          sizeof(S3_PBRUSH16));
    memset(&physical, 0xA5, sizeof(physical));
    CHECK(RealizeObject(&pdev, S3_OBJ_BRUSH, &logical, &physical, (void *)0) ==
          sizeof(S3_PBRUSH16));
    CHECK(physical.style == S3_BS_SOLID && physical.has_mask == 0);
    for (i = 0; i < 64; ++i) {
        CHECK(physical.color[i] == 0xE0); /* 3:3:2 red */
    }
    CHECK(RealizeObject(&pdev, (S3_U16)(0U - S3_OBJ_BRUSH), &logical, &physical,
                        (void *)0) == 1);

    logical.lbStyle = S3_BS_HATCHED;
    logical.lbHatch = 0; /* HS_HORIZONTAL */
    logical.lbColor = 0x00000000UL;
    logical.lbBkColor = 0x00FFFFFFUL;
    CHECK(RealizeObject(&pdev, S3_OBJ_BRUSH, &logical, &physical, (void *)0) ==
          sizeof(S3_PBRUSH16));
    CHECK(physical.has_mask == 1 && physical.mask[4] == 0xFF && physical.mask[0] == 0);
    CHECK(physical.color[4 * 8 + 3] == 0x00 && physical.color[0] == 0xFF);
    logical.lbHatch = 6;
    CHECK(RealizeObject(&pdev, S3_OBJ_BRUSH, &logical, &physical, (void *)0) == 0);

    logical.lbStyle = S3_BS_HOLLOW;
    CHECK(S3_RealizeBrush(&logical, &physical) && physical.style == S3_BS_HOLLOW);
    logical.lbStyle = 7;
    CHECK(!S3_RealizeBrush(&logical, &physical));

    /* Monochrome 8x8 pattern: 1 bits become white, 0 bits black. */
    memset(&bitmap, 0, sizeof(bitmap));
    memset(mono_bits, 0, sizeof(mono_bits));
    mono_bits[0] = 0x80;
    mono_bits[2 * 7] = 0x01;
    bitmap.bmWidth = 8;
    bitmap.bmHeight = 8;
    bitmap.bmWidthBytes = 2;
    bitmap.bmPlanes = 1;
    bitmap.bmBitsPixel = 1;
    bitmap.bmBits = mono_bits;
    CHECK(S3_RealizePatternBrush(&bitmap, &physical));
    CHECK(physical.color[0] == 0xFF && physical.color[1] == 0x00);
    CHECK(physical.color[7 * 8 + 7] == 0xFF && physical.color[7 * 8 + 6] == 0x00);

    /* 8-bpp pattern indices are copied unchanged. */
    for (i = 0; i < sizeof(color_bits); ++i) color_bits[i] = (S3_U8)i;
    bitmap.bmWidthBytes = 8;
    bitmap.bmBitsPixel = 8;
    bitmap.bmBits = color_bits;
    CHECK(S3_RealizePatternBrush(&bitmap, &physical));
    CHECK(physical.color[9] == 9 && physical.color[63] == 63);
    bitmap.bmWidth = 4;
    CHECK(!S3_RealizePatternBrush(&bitmap, &physical));
}

static void test_raster_rop3_and_targets(void)
{
    BANK_TEST_CONTEXT context;
    S3_SURFACE surface;
    S3_PDEV pdev;
    S3_U8 initial_window[64];
    S3_BITMAP16 mono, color;
    S3_U8 mono_bits[2 * 4];
    S3_U8 color_bits[8 * 4];
    S3_LBRUSH16 logical;
    S3_PBRUSH16 brush;
    S3_DRAWMODE16 mode;
    unsigned int i;

    /* Truth-table identities of the ROP3 evaluator. */
    for (i = 0; i < 256; ++i) {
        CHECK(S3_Rop3(0xF0, (S3_U8)i, 0x5A, 0x33) == (S3_U8)i);  /* PATCOPY */
        CHECK(S3_Rop3(0xCC, 0x5A, (S3_U8)i, 0x33) == (S3_U8)i);  /* SRCCOPY */
        CHECK(S3_Rop3(0x55, 0x5A, 0x33, (S3_U8)i) == (S3_U8)~i); /* DSTINVERT */
        CHECK(S3_Rop3(0x66, 0x00, 0x3C, (S3_U8)i) == (S3_U8)(i ^ 0x3C)); /* SRCINVERT */
        CHECK(S3_Rop3(0xEE, 0x00, 0x0F, (S3_U8)i) == (S3_U8)(i | 0x0F)); /* SRCPAINT */
        CHECK(S3_Rop3(0x5A, 0x0F, 0x00, (S3_U8)i) == (S3_U8)(i ^ 0x0F)); /* PATINVERT */
    }

    memset(&context, 0, sizeof(context));
    memset(context.vram, 0x11, sizeof(context.vram));
    memset(initial_window, 0, sizeof(initial_window));
    context.fail_bank = 0xFFFFFFFFUL;
    /* 6x5 screen with pitch 8 in the test's 64-byte bank model. */
    CHECK(S3_SurfaceInit(&surface, 6, 5, 8, 40, 64, 64,
                         initial_window, &context, test_map_bank));
    CHECK(S3_InitializePDEV(&pdev));
    pdev.surface_state = &surface;

    memset(&logical, 0, sizeof(logical));
    logical.lbStyle = S3_BS_SOLID;
    logical.lbColor = 0x00FFFFFFUL;
    CHECK(S3_RealizeBrush(&logical, &brush));
    /* Solid PATCOPY, clipped to the 6x5 screen. */
    CHECK(BitBlt(&pdev, 4, 3, (void *)0, 0, 0, 5, 5, 0x00F00021L, &brush, (void *)0));
    CHECK(context.vram[3 * 8 + 4] == 0xFF && context.vram[4 * 8 + 5] == 0xFF);
    CHECK(context.vram[3 * 8 + 6] == 0x11 && context.vram[2 * 8 + 4] == 0x11);
    /* DSTINVERT through the per-pixel path. */
    CHECK(BitBlt(&pdev, 0, 0, (void *)0, 0, 0, 2, 1, 0x00550009L, (void *)0, (void *)0));
    CHECK(context.vram[0] == 0xEE && context.vram[1] == 0xEE && context.vram[2] == 0x11);

    /* Mono source to colour screen: 1 = background, 0 = text colour. */
    memset(&mono, 0, sizeof(mono));
    memset(mono_bits, 0, sizeof(mono_bits));
    mono_bits[0] = 0xA0; /* row 0: 1 0 1 0 */
    mono.bmWidth = 4;
    mono.bmHeight = 4;
    mono.bmWidthBytes = 2;
    mono.bmPlanes = 1;
    mono.bmBitsPixel = 1;
    mono.bmBits = mono_bits;
    memset(&mode, 0, sizeof(mode));
    mode.bkColor = 0xFF0000E0UL;
    mode.TextColor = 0xFF000003UL;
    CHECK(BitBlt(&pdev, 0, 1, &mono, 0, 0, 4, 1, (S3_I32)S3_ROP3_SRCCOPY,
                 (void *)0, &mode));
    CHECK(context.vram[1 * 8 + 0] == 0xE0 && context.vram[1 * 8 + 1] == 0x03);
    CHECK(context.vram[1 * 8 + 2] == 0xE0 && context.vram[1 * 8 + 3] == 0x03);

    /* Colour screen to mono bitmap: background colour becomes 1. */
    memset(mono_bits, 0x00, sizeof(mono_bits));
    CHECK(BitBlt(&mono, 0, 0, &pdev, 0, 1, 4, 1, (S3_I32)S3_ROP3_SRCCOPY,
                 (void *)0, &mode));
    CHECK(mono_bits[0] == 0xA0);
    /* Solid white brush on mono: PATCOPY sets bits. */
    CHECK(BitBlt(&mono, 0, 2, (void *)0, 0, 0, 4, 1, 0x00F00021L, &brush, (void *)0));
    CHECK((mono_bits[4] & 0xF0) == 0xF0);

    /* 8-bpp bitmap SRCPAINT onto the screen. */
    memset(&color, 0, sizeof(color));
    for (i = 0; i < sizeof(color_bits); ++i) color_bits[i] = 0x0F;
    color.bmWidth = 8;
    color.bmHeight = 4;
    color.bmWidthBytes = 8;
    color.bmPlanes = 1;
    color.bmBitsPixel = 8;
    color.bmBits = color_bits;
    CHECK(BitBlt(&pdev, 0, 4, &color, 0, 0, 2, 1, 0x00EE0086L, (void *)0, (void *)0));
    CHECK(context.vram[4 * 8 + 0] == 0x1F && context.vram[4 * 8 + 2] == 0x11);

    /* Overlapping screen copy downwards keeps the source rows intact. */
    context.vram[0 * 8 + 5] = 0x77;
    context.vram[1 * 8 + 5] = 0x88;
    CHECK(BitBlt(&pdev, 5, 1, &pdev, 5, 0, 1, 2, (S3_I32)S3_ROP3_SRCCOPY,
                 (void *)0, (void *)0));
    CHECK(context.vram[1 * 8 + 5] == 0x77 && context.vram[2 * 8 + 5] == 0x88);
}

static void test_dib_conversions(void)
{
    struct {
        S3_BITMAPINFOHEADER16 header;
        S3_RGBQUAD16 colors[256];
    } info;
    S3_BITMAP16 bitmap;
    S3_U8 device_bits[4 * 2];
    S3_U8 mono_bits[2 * 2];
    S3_U8 dib24[2 * 12];
    S3_U8 dib1[2 * 4];
    S3_U8 dib8[2 * 4];
    S3_DIB_FORMAT format;

    memset(&info, 0, sizeof(info));
    info.header.biSize = sizeof(S3_BITMAPINFOHEADER16);
    info.header.biWidth = 3;
    info.header.biHeight = 2;
    info.header.biPlanes = 1;
    info.header.biBitCount = 24;
    CHECK(S3_DibFormat((S3_BITMAPINFO16 *)&info, &format));
    CHECK(format.stride == 12 && format.colors == 0);

    memset(&bitmap, 0, sizeof(bitmap));
    memset(device_bits, 0x55, sizeof(device_bits));
    bitmap.bmWidth = 3;
    bitmap.bmHeight = 2;
    bitmap.bmWidthBytes = 4;
    bitmap.bmPlanes = 1;
    bitmap.bmBitsPixel = 8;
    bitmap.bmBits = device_bits;

    /* 24-bpp DIB, bottom-up: scan 0 = bottom row. Pixels are B, G, R. */
    memset(dib24, 0, sizeof(dib24));
    dib24[2] = 0xFF;                 /* bottom-left red */
    dib24[12 + 3] = 0xFF;            /* top-middle blue */
    dib24[12 + 6] = dib24[12 + 7] = dib24[12 + 8] = 0xFF; /* top-right white */
    CHECK(S3_DibBitmapBits(&bitmap, 0, 0, 2, dib24, (S3_BITMAPINFO16 *)&info) == 2);
    CHECK(device_bits[4 + 0] == 0xE0);   /* bottom row (y = 1) red */
    CHECK(device_bits[1] == 0x03);       /* top row blue */
    CHECK(device_bits[2] == 0xFF);       /* top row white */
    CHECK(device_bits[3] == 0x55);       /* padding untouched */

    /* Read back as 24 bpp: red and white survive exactly. */
    memset(dib24, 0xAA, sizeof(dib24));
    CHECK(S3_DibBitmapBits(&bitmap, 1, 0, 2, dib24, (S3_BITMAPINFO16 *)&info) == 2);
    CHECK(dib24[0] == 0 && dib24[1] == 0 && dib24[2] == 0xFF);
    CHECK(dib24[12 + 6] == 0xFF && dib24[12 + 7] == 0xFF && dib24[12 + 8] == 0xFF);

    /* 8-bpp DIB with a colour table; GetDIBits fills the device table. */
    info.header.biBitCount = 8;
    info.header.biClrUsed = 2;
    info.colors[0].rgbRed = 0xFF;          /* index 0: red */
    info.colors[1].rgbGreen = 0xFF;        /* index 1: green */
    memset(dib8, 0, sizeof(dib8));
    dib8[4 + 1] = 1;                        /* top row, middle pixel green */
    CHECK(S3_DibBitmapBits(&bitmap, 0, 0, 2, dib8, (S3_BITMAPINFO16 *)&info) == 2);
    CHECK(device_bits[0] == 0xE0 && device_bits[1] == 0x1C && device_bits[4] == 0xE0);
    info.header.biClrUsed = 0;
    CHECK(S3_DibBitmapBits(&bitmap, 1, 0, 1, dib8, (S3_BITMAPINFO16 *)&info) == 1);
    CHECK(info.colors[0xE0].rgbRed == 0xFF && info.colors[0xE0].rgbBlue == 0);
    CHECK(dib8[0] == 0xE0);

    /* 1-bpp DIB into a monochrome bitmap. */
    memset(&info.colors, 0, sizeof(info.colors));
    info.header.biBitCount = 1;
    info.colors[1].rgbRed = info.colors[1].rgbGreen = info.colors[1].rgbBlue = 0xFF;
    memset(dib1, 0, sizeof(dib1));
    dib1[0] = 0x80;          /* bottom row: white, black, black */
    dib1[4] = 0x60;          /* top row: black, white, white */
    memset(mono_bits, 0, sizeof(mono_bits));
    bitmap.bmWidthBytes = 2;
    bitmap.bmBitsPixel = 1;
    bitmap.bmBits = mono_bits;
    CHECK(S3_DibBitmapBits(&bitmap, 0, 0, 2, dib1, (S3_BITMAPINFO16 *)&info) == 2);
    CHECK((mono_bits[0] & 0xE0) == 0x60 && (mono_bits[2] & 0xE0) == 0x80);

    info.header.biCompression = 1; /* BI_RLE8 is declined */
    CHECK(S3_DibBitmapBits(&bitmap, 0, 0, 2, dib1, (S3_BITMAPINFO16 *)&info) == 0);
}

static void test_raster_text(void)
{
    /* Version 2.00 font segment: chars 'A'..'B', 4 wide x 3 high, default
       and break characters biased by the first character. */
    S3_U8 font[160];
    S3_U8 bits[8];
    S3_BITMAP16 bitmap;
    S3_DRAWMODE16 mode;
    S3_RECT16 clip;
    S3_I16 widths[2];
    S3_U32 extent;

    memset(font, 0, sizeof(font));
    font[0] = 0x00; font[1] = 0x02;            /* fsVersion 0x0200 */
    font[86] = 4;                              /* fsPixWidth */
    font[88] = 3;                              /* fsPixHeight */
    font[95] = 'A'; font[96] = 'B';            /* first, last */
    font[97] = 0; font[98] = 1;                /* default 'A', break 'B' */
    font[118] = 4; font[120] = 130;            /* 'A': width 4, bits at 130 */
    font[122] = 3; font[124] = 133;            /* 'B': width 3, bits at 133 */
    font[130] = 0xF0; font[131] = 0x90; font[132] = 0xF0; /* 'A' rows */
    font[133] = 0x80; font[134] = 0x80; font[135] = 0x80; /* 'B': left bar */

    memset(&mode, 0, sizeof(mode));
    mode.bkMode = S3_BK_TRANSPARENT;
    mode.TextColor = 0xFF000000UL; /* black -> mono 0 */
    mode.bkColor = 0xFF0000FFUL;   /* white */
    extent = S3_TextOut((void *)0, 0, 0, (S3_RECT16 *)0, (const S3_U8 *)"AB", -2,
                        font + S3_FONT_INFO_OFFSET, &mode, (S3_I16 *)0,
                        (S3_RECT16 *)0, 0);
    CHECK(extent == ((3UL << 16) | 7));
    mode.CharExtra = 1;
    extent = S3_TextOut((void *)0, 0, 0, (S3_RECT16 *)0, (const S3_U8 *)"AZ", -2,
                        font + S3_FONT_INFO_OFFSET, &mode, (S3_I16 *)0,
                        (S3_RECT16 *)0, 0);
    CHECK(extent == ((3UL << 16) | 10)); /* 'Z' uses default 'A' */
    mode.CharExtra = 0;
    CHECK(S3_TextCharWidths(font + S3_FONT_INFO_OFFSET, 'A', 'B', widths) &&
          widths[0] == 4 && widths[1] == 3);

    /* Draw "AB" into an 8x3 white monochrome bitmap. */
    memset(&bitmap, 0, sizeof(bitmap));
    memset(bits, 0xFF, sizeof(bits));
    bitmap.bmWidth = 8;
    bitmap.bmHeight = 3;
    bitmap.bmWidthBytes = 2;
    bitmap.bmPlanes = 1;
    bitmap.bmBitsPixel = 1;
    bitmap.bmBits = bits;
    clip.left = 0; clip.top = 0; clip.right = 8; clip.bottom = 3;
    extent = S3_TextOut(&bitmap, 0, 0, &clip, (const S3_U8 *)"AB", 2,
                        font + S3_FONT_INFO_OFFSET, &mode, (S3_I16 *)0,
                        (S3_RECT16 *)0, 0);
    CHECK(extent == ((3UL << 16) | 7));
    /* Row 0: A=1111, B=1.. -> black (0) at x 0-3 and 4. */
    CHECK(bits[0] == 0x07);
    /* Row 1: A=1001, B bar -> 0 at x0, x3, x4. */
    CHECK(bits[2] == 0x67);

    /* Explicit widths and clipping. */
    memset(bits, 0xFF, sizeof(bits));
    widths[0] = 5; widths[1] = 3;
    clip.right = 5;
    CHECK(S3_TextOut(&bitmap, 0, 0, &clip, (const S3_U8 *)"AB", 2,
                     font + S3_FONT_INFO_OFFSET, &mode, widths,
                     (S3_RECT16 *)0, 0) == ((3UL << 16) | 8));
    CHECK(bits[0] == 0x0F); /* 'B' at x 5 is clipped away */

    /* Opaque mode paints the cell background. */
    memset(bits, 0x00, sizeof(bits));
    clip.right = 8;
    mode.bkMode = S3_BK_OPAQUE;
    CHECK(S3_TextOut(&bitmap, 0, 0, &clip, (const S3_U8 *)"B", 1,
                     font + S3_FONT_INFO_OFFSET, &mode, (S3_I16 *)0,
                     (S3_RECT16 *)0, 0) == ((3UL << 16) | 3));
    CHECK(bits[0] == 0x60);

    font[1] = 0x01; /* unknown version */
    CHECK(S3_TextOut(&bitmap, 0, 0, &clip, (const S3_U8 *)"B", 1,
                     font + S3_FONT_INFO_OFFSET, &mode, (S3_I16 *)0,
                     (S3_RECT16 *)0, 0) == 0);
}

static void test_output_scanlines(void)
{
    S3_BITMAP16 bitmap;
    S3_U8 bits[16 * 4];
    S3_POINT16 points[3];
    S3_PPEN16 pen;
    S3_LBRUSH16 logical;
    S3_PBRUSH16 brush;
    S3_DRAWMODE16 mode;
    S3_RECT16 clip;

    CHECK(S3_Rop2ToRop3(13) == 0xF0); /* R2_COPYPEN = PATCOPY */
    CHECK(S3_Rop2ToRop3(6) == 0x55);  /* R2_NOT = DSTINVERT */
    CHECK(S3_Rop2ToRop3(7) == 0x5A);  /* R2_XORPEN = PATINVERT */
    CHECK(S3_Rop2ToRop3(1) == 0x00 && S3_Rop2ToRop3(16) == 0xFF);

    memset(&bitmap, 0, sizeof(bitmap));
    memset(bits, 0x11, sizeof(bits));
    bitmap.bmWidth = 16;
    bitmap.bmHeight = 4;
    bitmap.bmWidthBytes = 16;
    bitmap.bmPlanes = 1;
    bitmap.bmBitsPixel = 8;
    bitmap.bmBits = bits;
    memset(&mode, 0, sizeof(mode));
    mode.Rop2 = 13;
    mode.bkMode = 1;
    memset(&pen, 0, sizeof(pen));
    pen.color = 0xFF000042UL;
    clip.left = 0; clip.top = 0; clip.right = 12; clip.bottom = 4;

    /* Pen-coloured spans [2,5) and [10,20) on row 1, clipped at x = 12. */
    points[0].x = 0; points[0].y = 1;
    points[1].x = 2; points[1].y = 5;
    points[2].x = 10; points[2].y = 20;
    CHECK(S3_OutputScanlines(&bitmap, 3, points, &pen, (S3_PBRUSH16 *)0, &mode, &clip));
    CHECK(bits[16 + 1] == 0x11 && bits[16 + 2] == 0x42 && bits[16 + 4] == 0x42);
    CHECK(bits[16 + 5] == 0x11 && bits[16 + 11] == 0x42 && bits[16 + 12] == 0x11);

    /* Transparent hatch: only the foreground of HS_VERTICAL (x % 8 == 4). */
    memset(&logical, 0, sizeof(logical));
    logical.lbStyle = S3_BS_HATCHED;
    logical.lbHatch = 1;
    logical.lbColor = 0x000000FFUL;
    logical.lbBkColor = 0x00FFFFFFUL;
    CHECK(S3_RealizeBrush(&logical, &brush));
    points[0].y = 2;
    points[1].x = 0; points[1].y = 8;
    CHECK(S3_OutputScanlines(&bitmap, 2, points, &pen, &brush, &mode, &clip));
    CHECK(bits[32 + 4] == 0xE0 && bits[32 + 3] == 0x11 && bits[32 + 5] == 0x11);

    /* Hollow brush draws nothing; outside the clip rows nothing either. */
    logical.lbStyle = S3_BS_HOLLOW;
    CHECK(S3_RealizeBrush(&logical, &brush));
    CHECK(S3_OutputScanlines(&bitmap, 2, points, &pen, &brush, &mode, &clip));
    points[0].y = 5;
    CHECK(S3_OutputScanlines(&bitmap, 2, points, &pen, (S3_PBRUSH16 *)0, &mode, &clip));

    /* Generic polyline on a memory bitmap with R2_NOT. */
    memset(bits, 0x0F, sizeof(bits));
    points[0].x = 0; points[0].y = 0;
    points[1].x = 4; points[1].y = 0;
    mode.Rop2 = 6;
    CHECK(S3_OutputPolyline(&bitmap, 2, points, 0x42, 6, &clip));
    CHECK(bits[0] == 0xF0 && bits[3] == 0xF0 && bits[4] == 0x0F);
}

static void test_cursor_pattern(void)
{
    struct {
        S3_CURSORSHAPE16 header;
        S3_U8 masks[2 * 2 * 2]; /* 16x2 AND mask, then 16x2 XOR mask */
    } shape;
    S3_U8 pattern[S3_HW_CURSOR_BYTES];

    memset(&shape, 0, sizeof(shape));
    shape.header.csWidth = 16;
    shape.header.csHeight = 2;
    shape.header.csWidthBytes = 2;
    shape.header.csColor = 0x0101;
    /* Row 0: pixel 0 black (AND 0 XOR 0), pixel 9 white (AND 0 XOR 1),
       pixel 15 inverted (AND 1 XOR 1); everything else transparent. */
    shape.masks[0] = 0x7F; shape.masks[1] = 0xBF;  /* AND row 0 */
    shape.masks[2] = 0xFF; shape.masks[3] = 0xFF;  /* AND row 1 */
    shape.masks[4] = 0x00; shape.masks[5] = 0x41;  /* XOR row 0 */
    shape.masks[6] = 0x00; shape.masks[7] = 0x00;  /* XOR row 1 */
    CHECK(S3_CursorPattern(&shape.header, pattern));
    CHECK(pattern[0] == 0x7F && pattern[1] == 0xBF);  /* AND pair, group 0 */
    CHECK(pattern[2] == 0x00 && pattern[3] == 0x41);  /* XOR pair, group 0 */
    CHECK(pattern[4] == 0xFF && pattern[6] == 0x00);  /* group 1 transparent */
    CHECK(pattern[16] == 0xFF && pattern[18] == 0x00); /* row 1 transparent */
    CHECK(pattern[1023] == 0x00 && pattern[1021] == 0xFF);
    shape.header.csWidthBytes = 1;
    CHECK(!S3_CursorPattern(&shape.header, pattern));
}

static void test_system_colors_exact(void)
{
    static const S3_U32 statics[20] = {
        0x000000UL, 0x000080UL, 0x008000UL, 0x008080UL, 0x800000UL,
        0x800080UL, 0x808000UL, 0xC0C0C0UL, 0xC0DCC0UL, 0xF0CAA6UL,
        0xF0FBFFUL, 0xA4A0A0UL, 0x808080UL, 0x0000FFUL, 0x00FF00UL,
        0x00FFFFUL, 0xFF0000UL, 0xFF00FFUL, 0xFFFF00UL, 0xFFFFFFUL
    };
    S3_U8 index, seen[256];
    S3_U32 rgb;
    unsigned int i;

    memset(seen, 0, sizeof(seen));
    /* Every static colour maps to its own index and back exactly. */
    for (i = 0; i < 20; ++i) {
        CHECK(S3_RGBToColorIndex(statics[i], &index));
        CHECK(S3_ColorIndexToRGB(index, &rgb) && rgb == statics[i]);
        CHECK(!seen[index]);
        seen[index] = 1;
    }
    CHECK(S3_RGBToColorIndex(0xC0C0C0UL, &index) && index == 0xB6);
}

/* 16 and 32 bpp: colour conversion, fills, mono expansion, DIBs, cursor. */
/* Screen polylines in 64 KiB banks step their byte offset from pixel to
   pixel; lines that cross a bank boundary land exactly where the DDA says. */
static void test_polyline_64k_banks(void)
{
    static S3_U8 vram[5 * 65536UL];
    LARGE_BANK_CONTEXT context;
    S3_SURFACE surface;
    S3_PDEV pdev;
    S3_POINT16 diagonal[2] = {{380, 38}, {390, 48}};
    S3_POINT16 vertical[2] = {{10, 0}, {10, 100}};
    S3_POINT16 shallow[2] = {{0, 39}, {399, 42}};
    S3_U32 i, set = 0;
    S3_I16 k;

    memset(vram, 0, sizeof(vram));
    context.vram = vram;
    context.framebuffer_bytes = 400UL * 4 * 200;
    context.bank_bytes = 65536UL;
    context.calls = 0;
    CHECK(S3_ModeSetVBE(&s3_mode, 400, 200, 32, 1600, 0x112, 0xE0000000UL));
    CHECK(S3_SurfaceInit(&surface, 400, 200, 1600, context.framebuffer_bytes,
                         65536UL, 65536UL, vram, &context, test_map_large_bank));
    surface.bytes_pixel = 4;
    CHECK(S3_InitializePDEV(&pdev));
    pdev.surface_state = &surface;
    CHECK(S3_OutputPolyline(&pdev, 2, diagonal, 0x00AABBCCUL, 13, (S3_RECT16 *)0));
    CHECK(S3_OutputPolyline(&pdev, 2, vertical, 0x00AABBCCUL, 13, (S3_RECT16 *)0));
    for (k = 0; k < 10; ++k) {
        CHECK(S3_PixelLoad(vram + (S3_U32)(38 + k) * 1600 + (380 + k) * 4, 4) == 0x00AABBCCUL);
    }
    for (k = 0; k < 100; ++k) {
        CHECK(S3_PixelLoad(vram + (S3_U32)k * 1600 + 10 * 4, 4) == 0x00AABBCCUL);
    }
    for (i = 0; i < context.framebuffer_bytes; i += 4) set += S3_PixelLoad(vram + i, 4) != 0;
    CHECK(set == 110);
    /* XORPEN twice along a shallow line through two banks restores it. */
    CHECK(S3_OutputPolyline(&pdev, 2, shallow, 0x00FFFFFFUL, 7, (S3_RECT16 *)0));
    CHECK(S3_PixelLoad(vram + 39UL * 1600, 4) == 0x00FFFFFFUL);
    CHECK(S3_OutputPolyline(&pdev, 2, shallow, 0x00FFFFFFUL, 7, (S3_RECT16 *)0));
    set = 0;
    for (i = 0; i < context.framebuffer_bytes; i += 4) set += S3_PixelLoad(vram + i, 4) != 0;
    CHECK(set == 110);
    S3_ModeDefault(&s3_mode);
}

/* RLE8 and RLE4 DIBs: encoded and absolute runs, word padding, deltas
   (skipped pixels stay unchanged), pixels past the width dropped, early end
   of bitmap, clipping in SetDIBitsToDevice. 32 bpp, so a pixel's red byte
   is the colour-table entry's red. */
static S3_U32 rle_px(const S3_U8 *bits, unsigned x, unsigned y)
{
    return S3_PixelLoad(bits + y * 32 + x * 4, 4);
}

static void test_rle_dibs(void)
{
    static const S3_U8 rle8[] = {
        0x03, 0x01, 0x00, 0x03, 0x02, 0x03, 0x04, 0x00, 0x02, 0x05, 0x00, 0x00,
        0x00, 0x02, 0x02, 0x00, 0x02, 0x06, 0x0A, 0x09, 0x00, 0x00,
        0x02, 0x07, 0x00, 0x02, 0x03, 0x01,
        0x01, 0x08, 0x00, 0x01
    };
    static const S3_U8 rle4[] = {
        0x05, 0x12, 0x00, 0x03, 0x34, 0x50, 0x00, 0x00,
        0x00, 0x05, 0x67, 0x89, 0xA0, 0x00, 0x00, 0x00,
        0x00, 0x01
    };
    struct {
        S3_BITMAPINFOHEADER16 header;
        S3_RGBQUAD16 colors[256];
    } info;
    S3_BITMAP16 bitmap;
    S3_U8 bits[8 * 4 * 4];
    S3_U8 big[8 * 4 * 6];
    S3_DIB_FORMAT format;
    S3_RECT16 clip = {3, 0, 6, 6};
    unsigned i, x;
    const S3_U32 old = 0x11111111UL;

    CHECK(S3_ModeSetVBE(&s3_mode, 640, 480, 32, 2560, 0x112, 0xE0000000UL));
    memset(&info, 0, sizeof(info));
    info.header.biSize = sizeof(S3_BITMAPINFOHEADER16);
    info.header.biWidth = 8;
    info.header.biHeight = 4;
    info.header.biPlanes = 1;
    info.header.biBitCount = 8;
    info.header.biCompression = 1;
    info.header.biSizeImage = sizeof(rle8);
    for (i = 0; i < 256; ++i) info.colors[i].rgbRed = (S3_U8)(i * 16 + 1);
    CHECK(S3_DibFormat((S3_BITMAPINFO16 *)&info, &format) && format.compression == 1);

    memset(&bitmap, 0, sizeof(bitmap));
    bitmap.bmWidth = 8;
    bitmap.bmHeight = 4;
    bitmap.bmWidthBytes = 32;
    bitmap.bmPlanes = 1;
    bitmap.bmBitsPixel = 32;
    bitmap.bmBits = bits;
    memset(bits, 0x11, sizeof(bits));
    CHECK(S3_DibBitmapBits(&bitmap, 0, 0, 4, (S3_U8 *)rle8, (S3_BITMAPINFO16 *)&info) == 4);
    /* Scan 0 (bitmap row 3): 1 1 1 | 2 3 4 | 5 5 */
    {
        static const S3_U8 scan0[8] = {1, 1, 1, 2, 3, 4, 5, 5};
        for (x = 0; x < 8; ++x) CHECK(rle_px(bits, x, 3) == ((S3_U32)(scan0[x] * 16 + 1) << 16));
    }
    /* Scan 1 (row 2): delta 2, then 6 6, then 9 to the edge. */
    CHECK(rle_px(bits, 0, 2) == old && rle_px(bits, 1, 2) == old);
    CHECK(rle_px(bits, 2, 2) == (0x61UL << 16) && rle_px(bits, 3, 2) == (0x61UL << 16));
    for (x = 4; x < 8; ++x) CHECK(rle_px(bits, x, 2) == (0x91UL << 16));
    /* Scan 2 (row 1): 7 7, then a delta to scan 3 column 5. */
    CHECK(rle_px(bits, 0, 1) == (0x71UL << 16) && rle_px(bits, 1, 1) == (0x71UL << 16));
    for (x = 2; x < 8; ++x) CHECK(rle_px(bits, x, 1) == old);
    /* Scan 3 (row 0): only column 5. */
    for (x = 0; x < 8; ++x) CHECK(rle_px(bits, x, 0) == (x == 5 ? (0x81UL << 16) : old));

    /* Later scans only: scans 0 and 1 are decoded but not written. */
    memset(bits, 0x11, sizeof(bits));
    CHECK(S3_DibBitmapBits(&bitmap, 0, 2, 2, (S3_U8 *)rle8, (S3_BITMAPINFO16 *)&info) == 2);
    CHECK(rle_px(bits, 0, 3) == old && rle_px(bits, 0, 2) == old);
    CHECK(rle_px(bits, 0, 1) == (0x71UL << 16) && rle_px(bits, 5, 0) == (0x81UL << 16));

    /* No RLE output from GetDIBits; RLE needs biSizeImage and its depth. */
    CHECK(S3_DibBitmapBits(&bitmap, 1, 0, 4, bits, (S3_BITMAPINFO16 *)&info) == 0);
    info.header.biSizeImage = 0;
    CHECK(!S3_DibFormat((S3_BITMAPINFO16 *)&info, &format));
    info.header.biSizeImage = sizeof(rle8);
    info.header.biCompression = 2;
    CHECK(!S3_DibFormat((S3_BITMAPINFO16 *)&info, &format));

    /* SetDIBitsToDevice path at x = 2 into a wider bitmap, clipped to
       columns 3..5: DIB columns 1..3 of each scan land there. */
    info.header.biCompression = 1;
    bitmap.bmWidth = 8;
    bitmap.bmHeight = 6;
    bitmap.bmBits = big;
    memset(big, 0x11, sizeof(big));
    CHECK(S3_DibToDevice(&bitmap, 2, 1, 0, 4, &clip, rle8, (S3_BITMAPINFO16 *)&info) == 4);
    CHECK(rle_px(big, 2, 4) == old && rle_px(big, 6, 4) == old);   /* clipped */
    CHECK(rle_px(big, 3, 4) == (0x11UL << 16));                    /* scan 0, DIB x 1 */
    CHECK(rle_px(big, 5, 4) == (0x21UL << 16));                    /* scan 0, DIB x 3 */
    CHECK(rle_px(big, 3, 3) == old && rle_px(big, 4, 3) == (0x61UL << 16));
    CHECK(rle_px(big, 3, 2) == (0x71UL << 16) && rle_px(big, 4, 2) == old);

    /* RLE4: nibble runs, odd absolute run padded to a word, end of bitmap. */
    info.header.biBitCount = 4;
    info.header.biCompression = 2;
    info.header.biSizeImage = sizeof(rle4);
    info.header.biClrUsed = 16;
    info.header.biHeight = 4;
    bitmap.bmHeight = 4;
    bitmap.bmBits = bits;
    memset(bits, 0x11, sizeof(bits));
    CHECK(S3_DibBitmapBits(&bitmap, 0, 0, 4, (S3_U8 *)rle4, (S3_BITMAPINFO16 *)&info) == 4);
    {
        static const S3_U8 scan0[8] = {1, 2, 1, 2, 1, 3, 4, 5};
        static const S3_U8 scan1[5] = {6, 7, 8, 9, 10};
        for (x = 0; x < 8; ++x) CHECK(rle_px(bits, x, 3) == ((S3_U32)(scan0[x] * 16 + 1) << 16));
        for (x = 0; x < 5; ++x) CHECK(rle_px(bits, x, 2) == ((S3_U32)(scan1[x] * 16 + 1) << 16));
        for (x = 5; x < 8; ++x) CHECK(rle_px(bits, x, 2) == old);
        for (x = 0; x < 8; ++x) CHECK(rle_px(bits, x, 1) == old && rle_px(bits, x, 0) == old);
    }

    /* A truncated stream stops cleanly. */
    info.header.biSizeImage = 3;
    memset(bits, 0x11, sizeof(bits));
    CHECK(S3_DibBitmapBits(&bitmap, 0, 0, 4, (S3_U8 *)rle4, (S3_BITMAPINFO16 *)&info) == 4);
    CHECK(rle_px(bits, 0, 3) == (0x11UL << 16) && rle_px(bits, 5, 3) == old);
    S3_ModeDefault(&s3_mode);
}

/* VBE mode choice: exact first; otherwise the largest drawable mode no
   larger than the request, at the requested depth before any other. */
static S3_VBE_MODE_INFO vbe_info(S3_U16 w, S3_U16 h, S3_U16 bpp)
{
    S3_VBE_MODE_INFO info;
    memset(&info, 0, sizeof(info));
    info.attributes = S3_VBE_ATTR_SUPPORTED | S3_VBE_ATTR_GRAPHICS | S3_VBE_ATTR_LFB;
    info.width = w;
    info.height = h;
    info.bits_pixel = bpp;
    info.pitch = (S3_U16)(w * (bpp / 8));
    info.lfb_physical = 0xE0000000UL;
    info.memory_model = bpp == 8 ? S3_VBE_MODEL_PACKED : S3_VBE_MODEL_DIRECT;
    info.red_mask_size = bpp == 16 ? 5 : 8;
    info.red_position = bpp == 16 ? 11 : 16;
    info.green_mask_size = bpp == 16 ? 6 : 8;
    return info;
}

static void test_vbe_mode_score(void)
{
    S3_VBE_MODE_INFO exact = vbe_info(1280, 720, 32);
    S3_VBE_MODE_INFO big32 = vbe_info(1024, 768, 32);
    S3_VBE_MODE_INFO small32 = vbe_info(800, 600, 32);
    S3_VBE_MODE_INFO big16 = vbe_info(1280, 720, 16);
    S3_VBE_MODE_INFO wide8 = vbe_info(1280, 720, 8);
    S3_VBE_MODE_INFO too_big = vbe_info(1920, 1080, 32);
    S3_VBE_MODE_INFO no_lfb = vbe_info(800, 600, 32);
    S3_VBE_MODE_INFO rgb555 = vbe_info(1024, 768, 16);

    no_lfb.attributes &= (S3_U16)~S3_VBE_ATTR_LFB;
    rgb555.red_mask_size = 5;
    rgb555.red_position = 10;
    rgb555.green_mask_size = 5;
    CHECK(S3_VbeModeScore(&exact, 1280, 720, 32) == S3_VBE_SCORE_EXACT);
    /* 1280x720x32 asked for: 1024x768 does not fit (768 > 720). */
    CHECK(S3_VbeModeScore(&big32, 1280, 720, 32) == 0);
    CHECK(S3_VbeModeScore(&too_big, 1280, 720, 32) == 0);
    CHECK(S3_VbeModeScore(&no_lfb, 1280, 720, 32) == 0);
    CHECK(S3_VbeModeScore(&rgb555, 1280, 800, 16) == 0);
    CHECK(S3_VbeModeScore(&small32, 1280, 720, 32) != 0);
    /* Same depth beats a larger mode at another depth; then area, then depth. */
    CHECK(S3_VbeModeScore(&small32, 1280, 720, 32) > S3_VbeModeScore(&big16, 1280, 720, 32));
    CHECK(S3_VbeModeScore(&big16, 1280, 720, 32) > S3_VbeModeScore(&wide8, 1280, 720, 32));
    CHECK(S3_VbeModeScore(&big16, 1280, 720, 24) > S3_VbeModeScore(&small32, 1280, 720, 24));
}

/* A monochrome pattern brush draws 0 bits in the text colour and 1 bits
   in the background colour of the DRAWMODE; black and white without one. */
static void test_mono_pattern_colours(void)
{
    S3_BITMAP16 pattern, target;
    S3_U8 pattern_bits[8 * 2];
    S3_U8 bits[8 * 4 * 2];
    S3_PBRUSH16 brush;
    S3_DRAWMODE16 mode;
    unsigned x;

    CHECK(S3_ModeSetVBE(&s3_mode, 640, 480, 32, 2560, 0x112, 0xE0000000UL));
    memset(&pattern, 0, sizeof(pattern));
    memset(pattern_bits, 0xF0, sizeof(pattern_bits));  /* columns 0-3 set */
    pattern.bmWidth = 8;
    pattern.bmHeight = 8;
    pattern.bmWidthBytes = 2;
    pattern.bmPlanes = 1;
    pattern.bmBitsPixel = 1;
    pattern.bmBits = pattern_bits;
    memset(&brush, 0, sizeof(brush));
    brush.style = S3_BS_PATTERN;
    CHECK(S3_RealizePatternBrush(&pattern, &brush) && brush.mono == 1);

    memset(&target, 0, sizeof(target));
    target.bmWidth = 8;
    target.bmHeight = 2;
    target.bmWidthBytes = 32;
    target.bmPlanes = 1;
    target.bmBitsPixel = 32;
    target.bmBits = bits;
    memset(&mode, 0, sizeof(mode));
    mode.TextColor = 0x00FF0000UL;   /* physical red (0x00RRGGBB) */
    mode.bkColor = 0x000000FFUL;     /* physical blue */
    CHECK(S3_RasterBitBlt(&target, 0, 0, (void *)0, 0, 0, 8, 2, 0x00F00021UL,
                          &brush, &mode));
    for (x = 0; x < 8; ++x) {
        CHECK(S3_PixelLoad(bits + x * 4, 4) == (x < 4 ? 0x000000FFUL : 0x00FF0000UL));
    }
    CHECK(S3_RasterBitBlt(&target, 0, 0, (void *)0, 0, 0, 8, 2, 0x00F00021UL,
                          &brush, (S3_DRAWMODE16 *)0));
    CHECK(S3_PixelLoad(bits, 4) == 0x00FFFFFFUL && S3_PixelLoad(bits + 28, 4) == 0);

    /* Colour -> mono: WHITENESS stores 0xFFFFFFFF; with a white background
       that pixel is still background (1), black is 0. */
    {
        S3_BITMAP16 mono;
        S3_U8 mono_bits[2 * 2];
        memset(&mono, 0, sizeof(mono));
        mono.bmWidth = 8;
        mono.bmHeight = 2;
        mono.bmWidthBytes = 2;
        mono.bmPlanes = 1;
        mono.bmBitsPixel = 1;
        mono.bmBits = mono_bits;
        CHECK(S3_RasterBitBlt(&target, 0, 0, (void *)0, 0, 0, 4, 2, 0x00FF0062UL,
                              (const S3_PBRUSH16 *)0, (S3_DRAWMODE16 *)0));
        CHECK(S3_PixelLoad(bits, 4) == 0xFFFFFFFFUL);
        mode.bkColor = 0x00FFFFFFUL;
        mode.TextColor = 0;
        memset(mono_bits, 0x55, sizeof(mono_bits));
        CHECK(S3_RasterBitBlt(&mono, 0, 0, &target, 0, 0, 8, 2, 0x00CC0020UL,
                              (const S3_PBRUSH16 *)0, &mode));
        CHECK(mono_bits[0] == 0xF0 && mono_bits[2] == 0xF0);
    }
    S3_ModeDefault(&s3_mode);
}

/* DIBs wider than one row buffer (4096 pixels) convert in pieces, both
   ways, and a visible part may start inside a 1-bpp byte. */
static void test_wide_dibs(void)
{
    static S3_U8 dib24[5000 * 3];
    static S3_U8 back[5000 * 3 + 4];
    static S3_U8 device[5000 * 4];
    static S3_U8 dib1[9000 / 8 + 8];
    struct {
        S3_BITMAPINFOHEADER16 header;
        S3_RGBQUAD16 colors[2];
    } info;
    S3_BITMAP16 bitmap;
    unsigned i;

    CHECK(S3_ModeSetVBE(&s3_mode, 640, 480, 32, 2560, 0x112, 0xE0000000UL));
    for (i = 0; i < sizeof(dib24); ++i) dib24[i] = (S3_U8)(i * 7 + (i >> 8));
    memset(&info, 0, sizeof(info));
    info.header.biSize = sizeof(S3_BITMAPINFOHEADER16);
    info.header.biWidth = 5000;
    info.header.biHeight = 1;
    info.header.biPlanes = 1;
    info.header.biBitCount = 24;
    memset(&bitmap, 0, sizeof(bitmap));
    bitmap.bmWidth = 5000;
    bitmap.bmHeight = 1;
    bitmap.bmWidthBytes = 20000;
    bitmap.bmPlanes = 1;
    bitmap.bmBitsPixel = 32;
    bitmap.bmBits = device;
    CHECK(S3_DibBitmapBits(&bitmap, 0, 0, 1, dib24, (S3_BITMAPINFO16 *)&info) == 1);
    for (i = 0; i < 5000; i += 499) {
        CHECK(device[i * 4] == dib24[i * 3] && device[i * 4 + 2] == dib24[i * 3 + 2]);
    }
    CHECK(device[4999 * 4 + 1] == dib24[4999 * 3 + 1]);
    memset(back, 0xAA, sizeof(back));
    CHECK(S3_DibBitmapBits(&bitmap, 1, 0, 1, back, (S3_BITMAPINFO16 *)&info) == 1);
    CHECK(memcmp(back, dib24, sizeof(dib24)) == 0);
    CHECK(back[sizeof(dib24)] == 0xAA);  /* 15000 bytes: no padding */

    /* 9000-pixel 1-bpp DIB at x = -4101: columns 4101 .. 9000 visible;
       4101 is bit 5 of byte 512. Bit k of the DIB is set when k % 3 == 0. */
    memset(dib1, 0, sizeof(dib1));
    for (i = 0; i < 9000; i += 3) dib1[i >> 3] |= (S3_U8)(0x80U >> (i & 7));
    info.header.biWidth = 9000;
    info.header.biBitCount = 1;
    info.header.biClrUsed = 2;
    info.colors[1].rgbRed = 0xFF;
    bitmap.bmWidth = 4899;
    bitmap.bmWidthBytes = 4899 * 4;
    memset(device, 0x11, sizeof(device));
    CHECK(S3_DibToDevice(&bitmap, -4101, 0, 0, 1, (S3_RECT16 *)0, dib1,
                         (S3_BITMAPINFO16 *)&info) == 1);
    for (i = 0; i < 4899; ++i) {
        CHECK(S3_PixelLoad(device + i * 4, 4) == ((i + 4101) % 3 == 0 ? 0x00FF0000UL : 0));
    }

    /* A copy within one bitmap, wider than a row buffer, moving right:
       chunks run right to left, so every source pixel is read intact. */
    bitmap.bmWidth = 5000;
    bitmap.bmWidthBytes = 20000;
    for (i = 0; i < 5000; ++i) S3_PixelStore(device + i * 4, i, 4);
    CHECK(S3_RasterBitBlt(&bitmap, 100, 0, &bitmap, 0, 0, 4900, 1, 0x00CC0020UL,
                          (const S3_PBRUSH16 *)0, (const S3_DRAWMODE16 *)0));
    for (i = 0; i < 4900; ++i) CHECK(S3_PixelLoad(device + (i + 100) * 4, 4) == i);

    /* A 2-scan DIB set into a 4-row bitmap lands in rows 0 and 1 (the DIB's
       top is the bitmap's top), as GDI's StretchBlt bands expect. */
    info.header.biWidth = 2;
    info.header.biHeight = 2;
    info.header.biBitCount = 24;
    info.header.biClrUsed = 0;
    bitmap.bmWidth = 2;
    bitmap.bmHeight = 4;
    bitmap.bmWidthBytes = 8;
    memset(device, 0x11, 32);
    memset(dib24, 0, 16);
    dib24[2] = 0xFF;          /* scan 0 (bottom of the DIB): red */
    dib24[8 + 0] = 0xFF;      /* scan 1 (top of the DIB): blue */
    CHECK(S3_DibBitmapBits(&bitmap, 0, 0, 2, dib24, (S3_BITMAPINFO16 *)&info) == 2);
    CHECK(S3_PixelLoad(device + 0, 4) == 0x000000FFUL);   /* row 0: blue */
    CHECK(S3_PixelLoad(device + 8, 4) == 0x00FF0000UL);   /* row 1: red */
    CHECK(S3_PixelLoad(device + 16, 4) == 0x11111111UL);  /* rows 2-3 untouched */
    CHECK(S3_PixelLoad(device + 24, 4) == 0x11111111UL);
    S3_ModeDefault(&s3_mode);
}

/* The row ROP (words, unrolled cases) matches the per-value ROP3 for every
   code, including an odd trailing byte and NULL unused operands. */
static void test_rop_bytes(void)
{
    S3_U8 p[7], s[7], d[7], expect[7];
    unsigned rop, i;
    for (rop = 0; rop < 256; ++rop) {
        for (i = 0; i < 7; ++i) {
            p[i] = (S3_U8)(0x5C * (i + 1) + rop);
            s[i] = (S3_U8)(0x3B * (i + 3) ^ rop);
            d[i] = (S3_U8)(0xA7 * (i + 5) + (rop >> 2));
            expect[i] = S3_Rop3((S3_U8)rop, p[i], s[i], d[i]);
        }
        S3_RopBytes((S3_U8)rop, d, p, s, 7);
        CHECK(memcmp(d, expect, 7) == 0);
    }
    for (i = 0; i < 7; ++i) d[i] = (S3_U8)(i * 37);
    S3_RopBytes(0x55, d, (const S3_U8 *)0, (const S3_U8 *)0, 7);
    for (i = 0; i < 7; ++i) CHECK(d[i] == (S3_U8)~(i * 37));
}

static void test_direct_colour_depths(void)
{
    BANK_TEST_CONTEXT context;
    S3_SURFACE surface;
    S3_PDEV pdev;
    S3_U8 initial_window[64];
    S3_LBRUSH16 logical;
    S3_PBRUSH16 brush;
    S3_DRAWMODE16 mode;
    S3_BITMAP16 mono, bitmap;
    S3_U8 mono_bits[2 * 4];
    S3_U8 device_bits[3 * 4 * 2];
    S3_U8 dib24[2 * 12];
    struct {
        S3_BITMAPINFOHEADER16 header;
        S3_RGBQUAD16 colors[256];
    } info;
    struct {
        S3_CURSORSHAPE16 header;
        S3_U8 masks[2 * 32 * 4];
    } shape;
    S3_SWCURSOR cursor;
    S3_U32 physical = 0;
    S3_U8 *p;

    /* 16 bpp 5:6:5. */
    CHECK(S3_ModeSetVBE(&s3_mode, 640, 480, 16, 1280, 0x111, 0xE0000000UL));
    CHECK(S3_RGBToPixel(0x00FFFFFFUL) == 0xFFFF);
    CHECK(S3_RGBToPixel(0x000080FFUL) == 0xFC00);  /* R 255, G 128 */
    CHECK(S3_PixelToRGB(0xFFFF) == 0x00FFFFFFUL);
    CHECK(S3_PixelToRGB(0) == 0);
    CHECK(S3_ColorInfoCore(0x00FFFFFFUL, &physical) == 0x00FFFFFFUL && physical == 0xFFFF);

    /* 32 bpp 0x00RRGGBB on a 6x5 screen, pitch 32, in 64-byte banks. */
    CHECK(S3_ModeSetVBE(&s3_mode, 6, 5, 32, 32, 0x112, 0xE0000000UL));
    CHECK(s3_mode.bytes_pixel == 4 && s3_mode.framebuffer_bytes == 160);
    CHECK(S3_ColorInfoCore(0x00336699UL, &physical) == 0x00336699UL);
    CHECK(physical == 0x00996633UL);
    CHECK(S3_ColorInfoCore(physical, (S3_U32 *)0) == 0x00336699UL);
    CHECK(S3_PixelIsLight(0x00FFFFFFUL) && !S3_PixelIsLight(0x00000080UL));

    memset(&context, 0, sizeof(context));
    memset(context.vram, 0x11, sizeof(context.vram));
    memset(initial_window, 0, sizeof(initial_window));
    context.fail_bank = 0xFFFFFFFFUL;
    CHECK(S3_SurfaceInit(&surface, 6, 5, 32, 160, 64, 64,
                         initial_window, &context, test_map_bank));
    surface.bytes_pixel = 4;
    CHECK(S3_InitializePDEV(&pdev));
    CHECK(pdev.bits_pixel == 32 && pdev.pitch == 32);
    pdev.surface_state = &surface;

    /* Solid red PATCOPY: pixels are B, G, R, 0 in memory. */
    memset(&logical, 0, sizeof(logical));
    logical.lbStyle = S3_BS_SOLID;
    logical.lbColor = 0x000000FFUL;
    CHECK(S3_RealizeBrush(&logical, &brush));
    CHECK(brush.color[0] == 0x00FF0000UL);
    CHECK(BitBlt(&pdev, 1, 1, (void *)0, 0, 0, 2, 2, 0x00F00021L, &brush, (void *)0));
    p = context.vram + 1 * 32 + 4;
    CHECK(p[0] == 0 && p[1] == 0 && p[2] == 0xFF && p[3] == 0);
    CHECK(context.vram[1 * 32 + 8 + 2] == 0xFF && context.vram[2 * 32 + 4 + 2] == 0xFF);
    CHECK(context.vram[1 * 32 + 12 + 2] == 0x11 && context.vram[0] == 0x11);
    /* DSTINVERT inverts all four bytes of a pixel. */
    CHECK(BitBlt(&pdev, 1, 1, (void *)0, 0, 0, 1, 1, 0x00550009L, (void *)0, (void *)0));
    CHECK(p[0] == 0xFF && p[1] == 0xFF && p[2] == 0x00 && p[3] == 0xFF);

    /* Mono to colour: 1 = background (white), 0 = text colour (blue). */
    memset(&mono, 0, sizeof(mono));
    memset(mono_bits, 0, sizeof(mono_bits));
    mono_bits[0] = 0xA0;
    mono.bmWidth = 4;
    mono.bmHeight = 4;
    mono.bmWidthBytes = 2;
    mono.bmPlanes = 1;
    mono.bmBitsPixel = 1;
    mono.bmBits = mono_bits;
    memset(&mode, 0, sizeof(mode));
    mode.bkColor = 0x00FFFFFFUL;
    mode.TextColor = 0x000000FFUL;
    CHECK(BitBlt(&pdev, 0, 3, &mono, 0, 0, 4, 1, (S3_I32)S3_ROP3_SRCCOPY,
                 (void *)0, &mode));
    p = context.vram + 3 * 32;
    CHECK(S3_PixelLoad(p, 4) == 0x00FFFFFFUL && S3_PixelLoad(p + 4, 4) == 0x000000FFUL);
    CHECK(S3_PixelLoad(p + 8, 4) == 0x00FFFFFFUL && S3_PixelLoad(p + 12, 4) == 0x000000FFUL);
    /* And back: colour to mono is 1 where the pixel is the background. */
    memset(mono_bits, 0, sizeof(mono_bits));
    CHECK(BitBlt(&mono, 0, 1, &pdev, 0, 3, 4, 1, (S3_I32)S3_ROP3_SRCCOPY,
                 (void *)0, &mode));
    CHECK(mono_bits[2] == 0xA0);

    /* A 24-bpp DIB into a 32-bpp memory bitmap and back. */
    memset(&info, 0, sizeof(info));
    info.header.biSize = sizeof(S3_BITMAPINFOHEADER16);
    info.header.biWidth = 3;
    info.header.biHeight = 2;
    info.header.biPlanes = 1;
    info.header.biBitCount = 24;
    memset(&bitmap, 0, sizeof(bitmap));
    memset(device_bits, 0x55, sizeof(device_bits));
    bitmap.bmWidth = 3;
    bitmap.bmHeight = 2;
    bitmap.bmWidthBytes = 12;
    bitmap.bmPlanes = 1;
    bitmap.bmBitsPixel = 32;
    bitmap.bmBits = device_bits;
    memset(dib24, 0, sizeof(dib24));
    dib24[0] = 0x12; dib24[1] = 0x34; dib24[2] = 0x56;  /* bottom-left */
    CHECK(S3_DibBitmapBits(&bitmap, 0, 0, 2, dib24, (S3_BITMAPINFO16 *)&info) == 2);
    CHECK(S3_PixelLoad(device_bits + 12, 4) == 0x00563412UL);
    memset(dib24, 0xAA, sizeof(dib24));
    CHECK(S3_DibBitmapBits(&bitmap, 1, 0, 2, dib24, (S3_BITMAPINFO16 *)&info) == 2);
    CHECK(dib24[0] == 0x12 && dib24[1] == 0x34 && dib24[2] == 0x56);

    /* Software cursor: AND 0 / XOR 1 at the hot spot draws white over the
       screen, and erasing restores every byte. */
    memset(&shape, 0, sizeof(shape));
    shape.header.csWidth = 32;
    shape.header.csHeight = 32;
    shape.header.csWidthBytes = 4;
    memset(shape.masks, 0xFF, 32 * 4);        /* AND: transparent */
    shape.masks[0] = 0x7F;                    /* pixel (0,0): AND 0 */
    shape.masks[32 * 4] = 0x80;               /* pixel (0,0): XOR 1 */
    p = context.vram + 4 * 32 + 8;            /* pixel (2, 4) */
    S3_PixelStore(p, 0x00123456UL, 4);        /* before the cursor exists */
    S3_SwCursorInit(&cursor, &surface);
    CHECK(S3_SwCursorSetShape(&cursor, &shape.header));
    S3_SwCursorMove(&cursor, 2, 4);
    CHECK(S3_PixelLoad(p, 4) == 0xFFFFFFFFUL);
    S3_SwCursorExclude(&cursor, (const S3_RECT16 *)0);
    CHECK(S3_PixelLoad(p, 4) == 0x00123456UL);
    S3_SwCursorMove(&cursor, 3, 4);           /* deferred while excluded */
    CHECK(S3_PixelLoad(p + 4, 4) != 0xFFFFFFFFUL);
    S3_SwCursorUnexclude(&cursor);
    CHECK(S3_PixelLoad(p, 4) == 0x00123456UL && S3_PixelLoad(p + 4, 4) == 0xFFFFFFFFUL);
    {
        /* Drawing elsewhere keeps the cursor up but still defers moves;
           drawing over it erases it. Drawn rectangle: (3,4)-(6,5). */
        S3_RECT16 away = {0, 0, 3, 4}, over = {5, 4, 6, 5};
        S3_SwCursorExclude(&cursor, &away);
        CHECK(S3_PixelLoad(p + 4, 4) == 0xFFFFFFFFUL);
        S3_SwCursorMove(&cursor, 2, 4);
        CHECK(S3_PixelLoad(p + 4, 4) == 0xFFFFFFFFUL && S3_PixelLoad(p, 4) == 0x00123456UL);
        S3_SwCursorUnexclude(&cursor);
        CHECK(S3_PixelLoad(p, 4) == 0xFFFFFFFFUL && S3_PixelLoad(p + 4, 4) != 0xFFFFFFFFUL);
        S3_SwCursorMove(&cursor, 3, 4);
        S3_SwCursorExclude(&cursor, &over);
        CHECK(S3_PixelLoad(p + 4, 4) != 0xFFFFFFFFUL);
        S3_SwCursorUnexclude(&cursor);
        CHECK(S3_PixelLoad(p + 4, 4) == 0xFFFFFFFFUL);
    }

    S3_ModeDefault(&s3_mode);
}

int main(void)
{
    test_full_vga_bridge_snapshot();
    test_full_vga_snapshot_in_bridge_close();
    test_gdi_contract();
    test_gdi_entrypoint_declines();
    test_realized_pen_and_output_polyline();
    test_realized_brushes();
    test_raster_rop3_and_targets();
    test_rop_bytes();
    test_wide_dibs();
    test_mono_pattern_colours();
    test_vbe_mode_score();
    test_rle_dibs();
    test_polyline_64k_banks();
    test_direct_colour_depths();
    test_dib_conversions();
    test_raster_text();
    test_output_scanlines();
    test_cursor_pattern();
    test_system_colors_exact();
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
