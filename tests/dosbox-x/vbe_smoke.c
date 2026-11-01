/*
 * DOS-only baseline test for DOSBox-X's S3 VBE 1280x720x8 mode.
 * This does not load or validate the Windows display driver or its private
 * CRTC timing candidate. It verifies BIOS mode info, banked aperture writes,
 * full-frame readback, and visible scanout independently of Windows media.
 */
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <stdio.h>

typedef unsigned char U8;
typedef unsigned short U16;
typedef unsigned long U32;

typedef struct VBE_MODE_PREFIX {
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
} VBE_MODE_PREFIX;

static U8 vbe_mode_info[256];
static int mode_is_set;
static U32 granularity_bytes;
static U32 window_bytes;
static U16 active_bank = 0xFFFFU;
static volatile U8 __far *aperture;

static void write_result(const char *text)
{
    FILE *result = fopen("VBERES.TXT", "wt");
    if (result != NULL) {
        fputs(text, result);
        fputs("\n", result);
        fclose(result);
    }
}

static int vbe_call(U16 ax, U16 bx, U16 cx, U16 dx,
                    U16 es, U16 di, union REGS *out_regs)
{
    union REGS in_regs;
    struct SREGS segments;
    in_regs.x.ax = ax;
    in_regs.x.bx = bx;
    in_regs.x.cx = cx;
    in_regs.x.dx = dx;
    in_regs.x.si = 0;
    in_regs.x.di = di;
    segread(&segments);
    segments.es = es;
    int86x(0x10, &in_regs, out_regs, &segments);
    return out_regs->x.ax == 0x004FU;
}

static int set_bank(U32 byte_offset)
{
    U32 bank = byte_offset / granularity_bytes;
    union REGS result;
    if (bank > 0xFFFFUL || (U16)bank == active_bank) return bank <= 0xFFFFUL;
    if (!vbe_call(0x4F05U, 0, 0, (U16)bank, 0, 0, &result)) return 0;
    active_bank = (U16)bank;
    return 1;
}

static U8 pattern(U32 offset, U16 pitch)
{
    U16 x = (U16)(offset % pitch);
    U16 y = (U16)(offset / pitch);
    if (x >= 1280U || y >= 720U) return 0;
    return (U8)((((x / 160U) & 7U) << 5) |
                (((y / 90U) & 7U) << 2) | ((x / 320U) & 3U));
}

static void program_test_palette(void)
{
    U16 i;
    outp(0x03C8, 0);
    for (i = 0; i < 256; ++i) {
        outp(0x03C9, ((i >> 5) & 7) * 9);
        outp(0x03C9, ((i >> 2) & 7) * 9);
        outp(0x03C9, (i & 3) * 21);
    }
}

static void restore_text_mode(void)
{
    union REGS in_regs, out_regs;
    if (!mode_is_set) return;
    in_regs.x.ax = 0x0003;
    int86(0x10, &in_regs, &out_regs);
    mode_is_set = 0;
}

int main(int argc, char **argv)
{
    VBE_MODE_PREFIX __far *info = (VBE_MODE_PREFIX __far *)vbe_mode_info;
    union REGS result;
    U16 pitch;
    U32 frame_bytes, bank_base, chunk_bytes, done, run, j;
    U16 x;
    U8 expected;

    if (!vbe_call(0x4F01U, 0, 0x023EU, 0, FP_SEG(vbe_mode_info),
                  FP_OFF(vbe_mode_info), &result)) {
        printf("FAIL: VBE 4F01 mode query AX=%04X\r\n", result.x.ax);
        write_result("FAIL: VBE 4F01 mode query");
        return 1;
    }
    if ((info->attributes & 0x0019U) != 0x0019U ||
        (info->win_a_attributes & 0x07U) != 0x07U ||
        info->x_resolution != 1280U || info->y_resolution != 720U ||
        info->planes != 1U || info->bits_per_pixel != 8U ||
        info->memory_model != 4U || info->win_a_segment == 0 ||
        info->win_granularity_kb == 0 || info->win_size_kb == 0 ||
        info->win_size_kb > 64U || info->win_granularity_kb > info->win_size_kb ||
        info->bytes_per_scanline < 1280U) {
        printf("FAIL: unexpected mode info %ux%u %ubpp pitch=%u model=%u win=%u/%u\r\n",
               info->x_resolution, info->y_resolution, info->bits_per_pixel,
               info->bytes_per_scanline, info->memory_model,
               info->win_granularity_kb, info->win_size_kb);
        write_result("FAIL: unexpected VBE mode info");
        return 1;
    }
    pitch = info->bytes_per_scanline;
    granularity_bytes = (U32)info->win_granularity_kb * 1024UL;
    window_bytes = (U32)info->win_size_kb * 1024UL;
    frame_bytes = (U32)pitch * 720UL;
    if (frame_bytes == 0) {
        printf("FAIL: invalid framebuffer byte count\r\n");
        write_result("FAIL: invalid framebuffer byte count");
        return 1;
    }

    if (!vbe_call(0x4F02U, 0x023EU, 0, 0, 0, 0, &result)) {
        printf("FAIL: VBE 4F02 mode set AX=%04X\r\n", result.x.ax);
        write_result("FAIL: VBE 4F02 mode set");
        return 1;
    }
    mode_is_set = 1;
    active_bank = 0xFFFFU;
    aperture = (volatile U8 __far *)MK_FP(info->win_a_segment, 0);
    program_test_palette();

    for (bank_base = 0; bank_base < frame_bytes; bank_base += granularity_bytes) {
        chunk_bytes = frame_bytes - bank_base;
        if (chunk_bytes > granularity_bytes) chunk_bytes = granularity_bytes;
        if (granularity_bytes > window_bytes || !set_bank(bank_base)) {
            printf("FAIL: cannot map byte %lu through banked window\r\n", bank_base);
            write_result("FAIL: cannot map framebuffer byte through bank window");
            restore_text_mode();
            return 1;
        }
        x = (U16)(bank_base % pitch);
        done = 0;
        while (done < chunk_bytes) {
            run = pitch - x;
            if (x < 1280U) {
                U32 to_color_edge = 160U - (x % 160U);
                if (run > to_color_edge) run = to_color_edge;
                if (run > 1280U - x) run = 1280U - x;
            }
            if (run > chunk_bytes - done) run = chunk_bytes - done;
            expected = pattern(bank_base + done, pitch);
            for (j = 0; j < run; ++j) aperture[done + j] = expected;
            done += run;
            x = (U16)(x + run);
            if (x == pitch) x = 0;
        }
    }

    for (bank_base = 0; bank_base < frame_bytes; bank_base += granularity_bytes) {
        chunk_bytes = frame_bytes - bank_base;
        if (chunk_bytes > granularity_bytes) chunk_bytes = granularity_bytes;
        if (!set_bank(bank_base)) {
            printf("FAIL: cannot map byte %lu for readback\r\n", bank_base);
            write_result("FAIL: cannot map framebuffer byte for readback");
            restore_text_mode();
            return 1;
        }
        x = (U16)(bank_base % pitch);
        done = 0;
        while (done < chunk_bytes) {
            run = pitch - x;
            if (x < 1280U) {
                U32 to_color_edge = 160U - (x % 160U);
                if (run > to_color_edge) run = to_color_edge;
                if (run > 1280U - x) run = 1280U - x;
            }
            if (run > chunk_bytes - done) run = chunk_bytes - done;
            expected = pattern(bank_base + done, pitch);
            for (j = 0; j < run; ++j) {
                if (aperture[done + j] != expected) {
                    printf("FAIL: readback mismatch at framebuffer byte %lu\r\n",
                           bank_base + done + j);
                    write_result("FAIL: framebuffer readback mismatch");
                    restore_text_mode();
                    return 1;
                }
            }
            done += run;
            x = (U16)(x + run);
            if (x == pitch) x = 0;
        }
    }

    printf("PASS: VBE 0x023E 1280x720x8, pitch=%u, frame=%lu bytes\r\n",
           pitch, frame_bytes);
    printf("PASS: bank window=%u KiB, granularity=%u KiB, complete write/readback\r\n",
           info->win_size_kb, info->win_granularity_kb);
    write_result("PASS: VBE 0x023E 1280x720x8; banked full-frame write/readback");
    if (argc > 1) {
        restore_text_mode();
        return 0;
    }
    printf("Pattern remains on screen. Capture with DOSBox-X host-key+P; press any key to restore text mode.\r\n");
    (void)getch();
    restore_text_mode();
    return 0;
}
