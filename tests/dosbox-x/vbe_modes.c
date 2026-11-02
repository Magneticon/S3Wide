/*
 * DOS-only survey of the VESA BIOS a DOSBox-X machine type offers: VBE
 * version, total video memory and every listed graphics mode. With a mode
 * number argument it also sets that mode (banked), writes and reads back a
 * marker in the last 64 KiB bank that holds the frame, and restores text mode.
 * A second argument ("hold") draws a 32 bpp test pattern over the whole
 * frame, writes READY.TXT and keeps the mode for 20 s for a screenshot.
 * Output goes to stdout; redirect it to a file. Each MODE line ends with
 * s3wide=yes when S3WIDE.DRV's VESA backend can use that mode.
 */
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char U8;
typedef unsigned short U16;
typedef unsigned long U32;

#pragma pack(1)
typedef struct VBE_INFO {
    char signature[4];
    U16 version;
    U32 oem_string;
    U32 capabilities;
    U32 mode_list;
    U16 total_memory_64k;
    U8 rest[492];
} VBE_INFO;

typedef struct VBE_MODE {
    U16 attributes;
    U8 win_a_attributes;
    U8 win_b_attributes;
    U16 win_granularity_kb;
    U16 win_size_kb;
    U16 win_a_segment;
    U16 win_b_segment;
    U32 win_function;
    U16 bytes_per_scanline;
    U16 x_resolution;
    U16 y_resolution;
    U8 x_char_size;
    U8 y_char_size;
    U8 planes;
    U8 bits_per_pixel;
    U8 banks;
    U8 memory_model;
    U8 bank_size;
    U8 image_pages;
    U8 reserved0;
    U8 color_fields[9];
    U32 phys_base;
    U8 rest[212];
} VBE_MODE;
#pragma pack()

static VBE_INFO info;
static VBE_MODE mode;

/* Whether S3WIDE.DRV's VESA backend can draw this mode (the rules of
   S3_VbeModeMatches): supported graphics mode with a linear frame buffer,
   8 bpp packed, 16 bpp 5:6:5 or 32 bpp with red at bit 16. */
static int s3wide_usable(void)
{
    if ((mode.attributes & 0x91) != 0x91 || mode.phys_base == 0) return 0;
    switch (mode.bits_per_pixel) {
    case 8:
        return mode.memory_model == 4;
    case 16:
        return mode.memory_model == 6 && mode.color_fields[0] == 5 &&
               mode.color_fields[1] == 11 && mode.color_fields[2] == 6;
    case 32:
        return mode.memory_model == 6 && mode.color_fields[0] == 8 &&
               mode.color_fields[1] == 16;
    }
    return 0;
}
static U16 modes[512];

static U16 vbe(U16 ax, U16 bx, U16 cx, U16 dx, void __far *buffer)
{
    union REGS r;
    struct SREGS s;
    memset(&r, 0, sizeof(r));
    segread(&s);
    r.x.ax = ax;
    r.x.bx = bx;
    r.x.cx = cx;
    r.x.dx = dx;
    s.es = FP_SEG(buffer);
    r.x.di = FP_OFF(buffer);
    int86x(0x10, &r, &r, &s);
    return r.x.ax;
}

static int query_mode(U16 number)
{
    memset(&mode, 0, sizeof(mode));
    return vbe(0x4F01, 0, number, 0, &mode) == 0x004F;
}

static U8 row[16384];

/* Copy bytes to a linear framebuffer offset through the bank window. */
static void bank_copy(U32 offset, const U8 *data, U16 bytes, U32 granularity)
{
    while (bytes != 0) {
        U16 bank = (U16)(offset / granularity);
        U16 within = (U16)(offset % granularity);
        U32 room = granularity - within;
        U16 chunk = room < bytes ? (U16)room : bytes;
        vbe(0x4F05, 0, 0, bank, 0);
        _fmemcpy((U8 __far *)MK_FP(mode.win_a_segment, within), data, chunk);
        offset += chunk;
        data += chunk;
        bytes -= chunk;
    }
}

/* 32 bpp pattern: red rises left to right, green top to bottom, with a
   16-pixel white frame on all four edges. Then wait 20 s for a capture. */
static void draw_and_hold(U32 granularity)
{
    U16 x, y, w = mode.x_resolution, h = mode.y_resolution;
    U32 start;
    FILE *ready;
    /* Interior template: blue and red fixed per column, green set per row. */
    for (x = 0; x < w; ++x) {
        U8 *p = row + (U32)x * 4;
        int edge = x < 16 || x >= w - 16;
        p[0] = edge ? 0xFF : 0x40;
        p[1] = 0xFF;
        p[2] = edge ? 0xFF : (U8)((U32)x * 255 / w);
        p[3] = 0;
    }
    for (y = 0; y < h; ++y) {
        int edge_row = y < 16 || y >= h - 16;
        U8 g = edge_row ? 0xFF : (U8)((U32)y * 255 / h);
        U8 *p = row + 16 * 4 + 1;
        for (x = 16; x < w - 16; ++x, p += 4) {
            *p = g;
        }
        if (edge_row) {
            for (x = 16, p = row + 16 * 4; x < w - 16; ++x, p += 4) {
                p[0] = p[2] = 0xFF;
            }
        } else if (y == 16) {
            for (x = 16, p = row + 16 * 4; x < w - 16; ++x, p += 4) {
                p[0] = 0x40;
                p[2] = (U8)((U32)x * 255 / w);
            }
        }
        bank_copy((U32)y * mode.bytes_per_scanline, row, (U16)(w * 4),
                  granularity);
    }
    ready = fopen("READY.TXT", "wt");
    if (ready != NULL) {
        fputs("ready\n", ready);
        fclose(ready);
    }
    start = *(volatile U32 __far *)MK_FP(0x40, 0x6C);
    while (*(volatile U32 __far *)MK_FP(0x40, 0x6C) - start < 20UL * 18UL) {
    }
}

static int test_mode(U16 number, int hold)
{
    U32 frame, last_byte, granularity;
    U16 bank, offset, result;
    volatile U8 __far *window;
    union REGS r;
    if (!query_mode(number)) {
        printf("SET %03X: 4F01 failed\n", number);
        return 1;
    }
    frame = (U32)mode.bytes_per_scanline * mode.y_resolution;
    granularity = (U32)mode.win_granularity_kb * 1024UL;
    if (granularity == 0) {
        printf("SET %03X: no bank window\n", number);
        return 1;
    }
    last_byte = frame - 1;
    bank = (U16)(last_byte / granularity);
    offset = (U16)(last_byte % granularity);
    result = vbe(0x4F02, number, 0, 0, 0);
    if (result != 0x004F) {
        printf("SET %03X: 4F02 AX=%04X\n", number, result);
        return 1;
    }
    result = vbe(0x4F05, 0, 0, bank, 0);
    window = (volatile U8 __far *)MK_FP(mode.win_a_segment, 0);
    window[offset] = 0x5A;
    window[0] = 0xA5;
    r.x.ax = (window[offset] == 0x5A && window[0] == 0xA5) ? 1 : 0;
    vbe(0x4F05, 0, 0, 0, 0);
    if (hold && mode.bits_per_pixel == 32 && mode.x_resolution <= 4096) {
        draw_and_hold(granularity);
    }
    {
        union REGS t;
        t.x.ax = 0x0003;
        int86(0x10, &t, &t);
    }
    printf("SET %03X: %ux%ux%u ok, frame=%lu bytes, last bank %u (4F05 AX=%04X), "
           "readback %s\n",
           number, mode.x_resolution, mode.y_resolution, mode.bits_per_pixel,
           frame, bank, result, r.x.ax ? "ok" : "FAILED");
    return r.x.ax ? 0 : 1;
}

int main(int argc, char **argv)
{
    U16 __far *list;
    char __far *oem;
    unsigned count = 0, i;
    memset(&info, 0, sizeof(info));
    memcpy(info.signature, "VBE2", 4);
    if (vbe(0x4F00, 0, 0, 0, &info) != 0x004F ||
        memcmp(info.signature, "VESA", 4) != 0) {
        printf("NO VBE\n");
        return 1;
    }
    oem = (char __far *)MK_FP((U16)(info.oem_string >> 16),
                              (U16)info.oem_string);
    printf("VBE %u.%u OEM \"%Fs\" memory %lu KiB\n", info.version >> 8,
           info.version & 0xFF, oem, (U32)info.total_memory_64k * 64UL);
    /* Copy the list first: 4F01 calls may reuse the info buffer. */
    list = (U16 __far *)MK_FP((U16)(info.mode_list >> 16),
                              (U16)info.mode_list);
    while (count < 512 && list[count] != 0xFFFF) {
        modes[count] = list[count];
        ++count;
    }
    for (i = 0; i < count; ++i) {
        if (!query_mode(modes[i])) continue;
        if (!(mode.attributes & 0x10) || mode.bits_per_pixel < 8) continue;
        printf("MODE %03X %5ux%-5u %2ubpp pitch=%5u model=%u attr=%04X "
               "win=%uK/%uK lfb=%08lX s3wide=%s\n",
               modes[i], mode.x_resolution, mode.y_resolution,
               mode.bits_per_pixel, mode.bytes_per_scanline,
               mode.memory_model, mode.attributes, mode.win_granularity_kb,
               mode.win_size_kb, mode.phys_base, s3wide_usable() ? "yes" : "no");
    }
    printf("MODES %u\n", count);
    if (argc > 1) {
        return test_mode((U16)strtoul(argv[1], NULL, 16), argc > 2);
    }
    return 0;
}
