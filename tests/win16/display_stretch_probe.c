#include <windows.h>
#include <string.h>

/* Win16 stretching probe.

   Draws each case twice: with SRCCOPY, which S3WIDE stretches itself, and
   with MERGECOPY and a white brush, which gives the same pixels but which
   the driver hands back to GDI's own emulation. The two results are
   compared pixel by pixel. Then a 320x180 frame is stretched to the full
   screen both ways and timed. Writes D:\OUTPUT\RESULT.TXT:
       STRETCH_<case>=<mismatching pixels>,<native result>,<GDI result>
       TIME_<case>=<frames>,<native ms per frame>,<GDI ms per frame>      */

#define RESULT_PATH "D:\\OUTPUT\\RESULT.TXT"
#define DW 40
#define DH 30

static HFILE result_file = HFILE_ERROR;

typedef struct {
    BITMAPINFOHEADER h;
    RGBQUAD c[256];
} DIBINFO;

static DIBINFO info8, info4, info1, info24;
static BYTE bits8[DW * DH];
static BYTE bits4[(DW / 2) * DH];
static BYTE bits1[8 * DH];
static BYTE huge *bits24;
static BYTE huge *frame8;
static DIBINFO frame_info;

static void put_line(LPCSTR line)
{
    if (result_file != HFILE_ERROR) {
        _lwrite(result_file, line, lstrlen(line));
        _lwrite(result_file, "\r\n", 2);
    }
}

static void settle(DWORD ms)
{
    DWORD start = GetTickCount();
    MSG msg;
    while (GetTickCount() - start < ms) {
        while (PeekMessage(&msg, (HWND)0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Yield();
    }
}

static void header(DIBINFO *info, int w, int h, int bits, int colors)
{
    int i;
    memset(info, 0, sizeof(*info));
    info->h.biSize = sizeof(BITMAPINFOHEADER);
    info->h.biWidth = w;
    info->h.biHeight = h;
    info->h.biPlanes = 1;
    info->h.biBitCount = (WORD)bits;
    info->h.biClrUsed = colors;
    for (i = 0; i < colors; ++i) {
        info->c[i].rgbRed = (BYTE)(i * 37);
        info->c[i].rgbGreen = (BYTE)(255 - i * 11);
        info->c[i].rgbBlue = (BYTE)(i * 3);
    }
}

static void make_dibs(void)
{
    int x, y;
    HGLOBAL h;
    header(&info8, DW, DH, 8, 64);
    header(&info4, DW, DH, 4, 16);
    header(&info1, DW, DH, 1, 2);
    header(&info24, DW, DH, 24, 0);
    memset(bits4, 0, sizeof(bits4));
    memset(bits1, 0, sizeof(bits1));
    h = GlobalAlloc(GMEM_FIXED, (DWORD)DW * 3 * DH);
    bits24 = (BYTE huge *)GlobalLock(h);
    for (y = 0; y < DH; ++y) {
        for (x = 0; x < DW; ++x) {
            BYTE v = (BYTE)((x * 3 + y * 5) & 63);
            bits8[y * DW + x] = v;
            bits4[y * (DW / 2) + x / 2] |= (BYTE)((x & 1) ? (v & 15) : ((v & 15) << 4));
            if ((x + y) % 3 == 0) bits1[y * 8 + x / 8] |= (BYTE)(0x80 >> (x & 7));
            bits24[(DWORD)y * DW * 3 + x * 3] = (BYTE)(x * 6);
            bits24[(DWORD)y * DW * 3 + x * 3 + 1] = (BYTE)(y * 8);
            bits24[(DWORD)y * DW * 3 + x * 3 + 2] = (BYTE)(x * y);
        }
    }
    /* A 320x180 8-bpp "video frame". */
    header(&frame_info, 320, 180, 8, 256);
    h = GlobalAlloc(GMEM_FIXED, 320UL * 180);
    frame8 = (BYTE huge *)GlobalLock(h);
    for (y = 0; y < 180; ++y)
        for (x = 0; x < 320; ++x) frame8[(DWORD)y * 320 + x] = (BYTE)(x + y * 3);
}

static long compare(HDC dc, int ax, int bx, int y, int w, int h)
{
    long bad = 0;
    int i, j;
    for (j = 0; j < h; ++j)
        for (i = 0; i < w; ++i)
            if (GetPixel(dc, ax + i, y + j) != GetPixel(dc, bx + i, y + j)) ++bad;
    return bad;
}

static int row_y = 10;

/* StretchDIBits case: native at x 10, GDI reference at x 10 + dw + 20. */
static void dib_case(HDC screen, LPCSTR name, BITMAPINFO FAR *info, const void FAR *bits,
                     int sx, int sy, int sw, int sh, int dw, int dh)
{
    char line[96];
    int bx = 10 + dw + 20, a, b;
    HBRUSH old = SelectObject(screen, GetStockObject(WHITE_BRUSH));
    PatBlt(screen, 10, row_y, dw, dh, BLACKNESS);
    PatBlt(screen, bx, row_y, dw, dh, BLACKNESS);
    a = StretchDIBits(screen, 10, row_y, dw, dh, sx, sy, sw, sh, bits, info,
                      DIB_RGB_COLORS, SRCCOPY);
    b = StretchDIBits(screen, bx, row_y, dw, dh, sx, sy, sw, sh, bits, info,
                      DIB_RGB_COLORS, MERGECOPY);
    SelectObject(screen, old);
    wsprintf(line, "STRETCH_%s=%ld,%d,%d", name, compare(screen, 10, bx, row_y, dw, dh), a, b);
    put_line(line);
    row_y += dh + 6;
}

/* StretchBlt from a memory bitmap made from the 8-bpp DIB. */
static void blt_case(HDC screen, LPCSTR name, int sx, int sy, int sw, int sh, int dw, int dh)
{
    char line[96];
    int bx = 10 + dw + 20, a, b;
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, DW, DH);
    HBITMAP old_bitmap;
    HBRUSH old = SelectObject(screen, GetStockObject(WHITE_BRUSH));
    SetDIBits(screen, bitmap, 0, DH, bits8, (BITMAPINFO FAR *)&info8, DIB_RGB_COLORS);
    old_bitmap = SelectObject(dc, bitmap);
    PatBlt(screen, 10, row_y, dw, dh, BLACKNESS);
    PatBlt(screen, bx, row_y, dw, dh, BLACKNESS);
    a = StretchBlt(screen, 10, row_y, dw, dh, dc, sx, sy, sw, sh, SRCCOPY);
    b = StretchBlt(screen, bx, row_y, dw, dh, dc, sx, sy, sw, sh, MERGECOPY);
    SelectObject(screen, old);
    wsprintf(line, "STRETCH_%s=%ld,%d,%d", name, compare(screen, 10, bx, row_y, dw, dh), a, b);
    put_line(line);
    row_y += dh + 6;
    SelectObject(dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

static void time_case(HDC screen, int width, int height)
{
    char line[96];
    DWORD start, native_ms, gdi_ms;
    int n, frames = 6;
    HBRUSH old = SelectObject(screen, GetStockObject(WHITE_BRUSH));
    start = GetTickCount();
    for (n = 0; n < frames; ++n)
        StretchDIBits(screen, 0, 0, width, height, 0, 0, 320, 180, (LPVOID)frame8,
                      (BITMAPINFO FAR *)&frame_info, DIB_RGB_COLORS, SRCCOPY);
    native_ms = GetTickCount() - start;
    start = GetTickCount();
    for (n = 0; n < frames; ++n)
        StretchDIBits(screen, 0, 0, width, height, 0, 0, 320, 180, (LPVOID)frame8,
                      (BITMAPINFO FAR *)&frame_info, DIB_RGB_COLORS, MERGECOPY);
    gdi_ms = GetTickCount() - start;
    SelectObject(screen, old);
    wsprintf(line, "TIME_320x180_TO_%dx%d=%d,%lu,%lu", width, height, frames,
             native_ms / frames, gdi_ms / frames);
    put_line(line);
}

int PASCAL WinMain(HINSTANCE instance, HINSTANCE previous,
                   LPSTR command_line, int show_command)
{
    HDC screen;
    char line[48];
    (void)instance;
    (void)previous;
    (void)command_line;
    (void)show_command;

    result_file = _lcreat(RESULT_PATH, 0);
    if (result_file == HFILE_ERROR) return 2;
    put_line("DISPLAY_STRETCH_PROBE=1");
    settle(4000);
    make_dibs();
    screen = GetDC((HWND)0);
    if (screen != (HDC)0) {
        wsprintf(line, "BITSPIXEL=%d", GetDeviceCaps(screen, BITSPIXEL));
        put_line(line);
        wsprintf(line, "RASTERCAPS=%04X", GetDeviceCaps(screen, RASTERCAPS));
        put_line(line);
        dib_case(screen, "DIB8_1X", (BITMAPINFO FAR *)&info8, bits8, 0, 0, DW, DH, DW, DH);
        dib_case(screen, "DIB8_2X", (BITMAPINFO FAR *)&info8, bits8, 0, 0, DW, DH, 80, 60);
        dib_case(screen, "DIB8_ODD", (BITMAPINFO FAR *)&info8, bits8, 0, 0, DW, DH, 103, 77);
        dib_case(screen, "DIB8_SUB", (BITMAPINFO FAR *)&info8, bits8, 5, 4, 20, 10, 70, 45);
        dib_case(screen, "DIB4_3X", (BITMAPINFO FAR *)&info4, bits4, 3, 2, 21, 13, 63, 39);
        dib_case(screen, "DIB1_2X", (BITMAPINFO FAR *)&info1, bits1, 1, 0, 30, 20, 60, 40);
        dib_case(screen, "DIB24_2X", (BITMAPINFO FAR *)&info24, (const void FAR *)bits24,
                 0, 0, DW, DH, 80, 70);
        dib_case(screen, "DIB8_WIDE", (BITMAPINFO FAR *)&info8, bits8, 0, 0, DW, DH, 120, 30);
        blt_case(screen, "BLT_2X", 0, 0, DW, DH, 80, 60);
        blt_case(screen, "BLT_SUB", 7, 3, 25, 19, 76, 57);
        time_case(screen, GetDeviceCaps(screen, HORZRES), GetDeviceCaps(screen, VERTRES));
        ReleaseDC((HWND)0, screen);
    }
    put_line("SHUTDOWN_REQUESTED=YES");
    _lclose(result_file);
    result_file = HFILE_ERROR;
    ExitWindows(0, 0);
    return 0;
}
