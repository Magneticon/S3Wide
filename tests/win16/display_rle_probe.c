#include <windows.h>
#include <string.h>

/* Win16 RLE DIB probe.

   Draws the same picture as an uncompressed DIB and as its RLE8 / RLE4
   encoding through each GDI path that takes DIBs, then compares the
   pixels on screen with GetPixel. Writes one line per case to
   D:\OUTPUT\RESULT.TXT:
       RLE_<case>=<mismatching pixels>,<GDI return value>
   0 mismatches means the compressed picture drew exactly like the
   uncompressed one. */

#define RESULT_PATH "D:\\OUTPUT\\RESULT.TXT"
#define SETTLE_MS 6000UL
#define W 64
#define H 32

static HFILE result_file = HFILE_ERROR;

typedef struct {
    BITMAPINFOHEADER header;
    RGBQUAD colors[256];
} DIBINFO;

static DIBINFO raw_info, rle_info;
static BYTE raw_bits[W * H];
static BYTE rle_bits[W * H * 2 + 64];

static void put_line(LPCSTR line)
{
    if (result_file != HFILE_ERROR) {
        _lwrite(result_file, line, lstrlen(line));
        _lwrite(result_file, "\r\n", 2);
    }
}

static void settle(DWORD milliseconds)
{
    DWORD start = GetTickCount();
    MSG msg;
    while (GetTickCount() - start < milliseconds) {
        while (PeekMessage(&msg, (HWND)0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Yield();
    }
}

/* Index of pixel (x, scan) for a picture with runs of five. */
static BYTE index_at(int x, int scan, int levels)
{
    return (BYTE)(((x / 5) + (scan / 3) * 3) % levels);
}

/* Build the uncompressed DIB (8 bpp, or 4 bpp packed) and its RLE stream
   of encoded runs only. Returns the RLE length. */
static DWORD build(int bits)
{
    int levels = bits == 8 ? 40 : 16;
    int stride = bits == 8 ? W : W / 2;
    int x, scan, run;
    DWORD n = 0;
    BYTE v;

    memset(raw_bits, 0, sizeof(raw_bits));
    for (scan = 0; scan < H; ++scan) {
        for (x = 0; x < W; ++x) {
            v = index_at(x, scan, levels);
            if (bits == 8) {
                raw_bits[scan * stride + x] = v;
            } else if (x & 1) {
                raw_bits[scan * stride + x / 2] |= v;
            } else {
                raw_bits[scan * stride + x / 2] = (BYTE)(v << 4);
            }
        }
        for (x = 0; x < W; x += run) {
            v = index_at(x, scan, levels);
            for (run = 1; x + run < W && run < 255 &&
                          index_at(x + run, scan, levels) == v; ++run) {
            }
            rle_bits[n++] = (BYTE)run;
            rle_bits[n++] = bits == 8 ? v : (BYTE)((v << 4) | v);
        }
        rle_bits[n++] = 0;
        rle_bits[n++] = 0;                       /* end of scan */
    }
    rle_bits[n - 1] = 1;                         /* last one: end of bitmap */

    memset(&raw_info, 0, sizeof(raw_info));
    raw_info.header.biSize = sizeof(BITMAPINFOHEADER);
    raw_info.header.biWidth = W;
    raw_info.header.biHeight = H;
    raw_info.header.biPlanes = 1;
    raw_info.header.biBitCount = (WORD)bits;
    raw_info.header.biClrUsed = levels;
    for (x = 0; x < levels; ++x) {
        raw_info.colors[x].rgbRed = (BYTE)(x * 37);
        raw_info.colors[x].rgbGreen = (BYTE)(255 - x * 23);
        raw_info.colors[x].rgbBlue = (BYTE)((x & 3) * 85);
    }
    rle_info = raw_info;
    rle_info.header.biCompression = bits == 8 ? BI_RLE8 : BI_RLE4;
    rle_info.header.biSizeImage = n;
    return n;
}

/* Pixels that differ between the reference at (rx, ry) and (tx, ty). */
static long compare(HDC dc, int rx, int ry, int tx, int ty)
{
    long bad = 0;
    int x, y;
    for (y = 0; y < H; ++y) {
        for (x = 0; x < W; ++x) {
            if (GetPixel(dc, rx + x, ry + y) != GetPixel(dc, tx + x, ty + y)) ++bad;
        }
    }
    return bad;
}

static void report(LPCSTR name, long bad, long gdi)
{
    char line[96];
    wsprintf(line, "RLE_%s=%ld,%ld", name, bad, gdi);
    put_line(line);
}

static void run_cases(HDC screen, int bits, int top)
{
    char name[32];
    HDC memory_dc;
    HBITMAP bitmap, old;
    long r;

    build(bits);
    /* Reference: the uncompressed DIB. */
    SetDIBitsToDevice(screen, 0, top, W, H, 0, 0, 0, H, raw_bits,
                      (BITMAPINFO FAR *)&raw_info, DIB_RGB_COLORS);

    r = SetDIBitsToDevice(screen, 80, top, W, H, 0, 0, 0, H, rle_bits,
                          (BITMAPINFO FAR *)&rle_info, DIB_RGB_COLORS);
    wsprintf(name, "%d_SETDIBITSTODEVICE", bits);
    report(name, compare(screen, 0, top, 80, top), r);

    r = StretchDIBits(screen, 160, top, W, H, 0, 0, W, H, rle_bits,
                      (BITMAPINFO FAR *)&rle_info, DIB_RGB_COLORS, SRCCOPY);
    wsprintf(name, "%d_STRETCHDIBITS", bits);
    report(name, compare(screen, 0, top, 160, top), r);

    memory_dc = CreateCompatibleDC(screen);
    bitmap = CreateCompatibleBitmap(screen, W, H);
    r = SetDIBits(screen, bitmap, 0, H, rle_bits, (BITMAPINFO FAR *)&rle_info,
                  DIB_RGB_COLORS);
    old = SelectObject(memory_dc, bitmap);
    BitBlt(screen, 240, top, W, H, memory_dc, 0, 0, SRCCOPY);
    SelectObject(memory_dc, old);
    wsprintf(name, "%d_SETDIBITS", bits);
    report(name, compare(screen, 0, top, 240, top), r);
    DeleteObject(bitmap);
    DeleteDC(memory_dc);

    /* Twice the size through StretchDIBits: compare the uncompressed and
       compressed stretches with each other. */
    StretchDIBits(screen, 320, top, W * 2, H, 0, 0, W, H, raw_bits,
                  (BITMAPINFO FAR *)&raw_info, DIB_RGB_COLORS, SRCCOPY);
    r = StretchDIBits(screen, 460, top, W * 2, H, 0, 0, W, H, rle_bits,
                      (BITMAPINFO FAR *)&rle_info, DIB_RGB_COLORS, SRCCOPY);
    wsprintf(name, "%d_STRETCH2X", bits);
    report(name, compare(screen, 320, top, 460, top) +
                 compare(screen, 320 + W, top, 460 + W, top), r);
}

int PASCAL WinMain(HINSTANCE instance, HINSTANCE previous,
                   LPSTR command_line, int show_command)
{
    HDC screen;
    (void)instance;
    (void)previous;
    (void)command_line;
    (void)show_command;

    result_file = _lcreat(RESULT_PATH, 0);
    if (result_file == HFILE_ERROR) return 2;
    put_line("DISPLAY_RLE_PROBE=1");
    settle(SETTLE_MS);
    screen = GetDC((HWND)0);
    if (screen == (HDC)0) {
        put_line("GETDC=FAIL");
    } else {
        char line[64];
        wsprintf(line, "BITSPIXEL=%d", GetDeviceCaps(screen, BITSPIXEL));
        put_line(line);
        run_cases(screen, 8, 40);
        run_cases(screen, 4, 100);
        ReleaseDC((HWND)0, screen);
    }
    put_line("SHUTDOWN_REQUESTED=YES");
    _lclose(result_file);
    result_file = HFILE_ERROR;
    ExitWindows(0, 0);
    return 0;
}
