#include <windows.h>
#include <string.h>
#include <stdlib.h>

/* Win16 huge-bitmap layout probe.

   Creates compatible bitmaps larger than 64 KiB at the screen's depth,
   lets GDI store a known pattern with SetBitmapBits (GDI's own layout of
   bitmap memory), then reads every row back through the display driver
   (GetPixel on a memory DC, and BitBlt to the screen). Writes to
   D:\OUTPUT\RESULT.TXT:
       HUGE_<w>x<h>=<widthbytes>,<first bad row or -1>,<bad rows>,<path>
   and the BITMAP fields GDI reports. */

#define RESULT_PATH "D:\\OUTPUT\\RESULT.TXT"

static HFILE result_file = HFILE_ERROR;

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

/* Colour of row y, column x in the pattern. */
static COLORREF expected(int x, int y)
{
    return RGB((y * 7) & 0xFF, (y >> 5) * 40 & 0xFF, (x & 1) ? 0xFF : 0x00);
}

/* Equal at 32 bpp; within 5:6:5 rounding at 16 bpp. */
static int close_enough(COLORREF c, COLORREF e, int bytes_pixel)
{
    if (bytes_pixel != 2) return c == e;
    return abs((int)GetRValue(c) - GetRValue(e)) <= 8 &&
           abs((int)GetGValue(c) - GetGValue(e)) <= 4 &&
           abs((int)GetBValue(c) - GetBValue(e)) <= 8;
}

static void run(HDC screen, int w, int h)
{
    char line[160];
    int bits = GetDeviceCaps(screen, BITSPIXEL);
    int bytes_pixel = bits / 8;
    HBITMAP bitmap = CreateCompatibleBitmap(screen, w, h);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP old;
    BITMAP info;
    DWORD row_bytes, total, i;
    HGLOBAL memory;
    BYTE huge *data;
    int x, y, bad_rows, first_bad, row_bad;
    COLORREF c;

    if (bitmap == 0 || dc == 0 || bytes_pixel < 1) {
        put_line("HUGE_SETUP=FAIL");
        return;
    }
    GetObject(bitmap, sizeof(info), &info);
    row_bytes = (DWORD)info.bmWidthBytes;
    wsprintf(line, "BITMAP_%dx%d=widthbytes %d planes %d bits %d", w, h,
             info.bmWidthBytes, info.bmPlanes, info.bmBitsPixel);
    put_line(line);

    /* The pattern as packed rows; SetBitmapBits places them as GDI lays
       out bitmap memory. Pixel values as the driver stores them:
       8 bpp colour-cube index unknown, so only 16/32 bpp are checked. */
    total = row_bytes * (DWORD)h;
    memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, total);
    data = (BYTE huge *)GlobalLock(memory);
    if (data == NULL) {
        put_line("HUGE_ALLOC=FAIL");
        return;
    }
    for (y = 0; y < h; ++y) {
        for (x = 0; x < w; ++x) {
            COLORREF rgb = expected(x, y);
            DWORD at = (DWORD)y * row_bytes + (DWORD)x * bytes_pixel;
            if (bytes_pixel == 4) {
                data[at] = GetBValue(rgb);
                data[at + 1] = GetGValue(rgb);
                data[at + 2] = GetRValue(rgb);
                data[at + 3] = 0;
            } else if (bytes_pixel == 2) {
                WORD v = (WORD)(((GetRValue(rgb) >> 3) << 11) |
                                ((GetGValue(rgb) >> 2) << 5) | (GetBValue(rgb) >> 3));
                data[at] = (BYTE)v;
                data[at + 1] = (BYTE)(v >> 8);
            }
        }
    }
    i = SetBitmapBits(bitmap, total, (const void FAR *)data);
    wsprintf(line, "SETBITMAPBITS_%dx%d=%ld of %ld", w, h, (long)i, (long)total);
    put_line(line);

    old = SelectObject(dc, bitmap);
    /* Through the driver: GetPixel on the memory DC. */
    bad_rows = 0;
    first_bad = -1;
    for (y = 0; y < h; ++y) {
        row_bad = 0;
        for (x = 0; x < w; x += 7) {
            c = GetPixel(dc, x, y);
            if (bytes_pixel == 2) {
                COLORREF e = expected(x, y);
                if (abs((int)GetRValue(c) - GetRValue(e)) > 8 ||
                    abs((int)GetGValue(c) - GetGValue(e)) > 4 ||
                    abs((int)GetBValue(c) - GetBValue(e)) > 8) row_bad = 1;
            } else if (c != expected(x, y)) {
                row_bad = 1;
            }
        }
        if (row_bad) {
            if (first_bad < 0) first_bad = y;
            ++bad_rows;
        }
    }
    wsprintf(line, "HUGE_%dx%d=%ld,%d,%d,getpixel", w, h, (long)row_bytes, first_bad,
             bad_rows);
    put_line(line);
    if (first_bad >= 0) {
        c = GetPixel(dc, 0, first_bad);
        wsprintf(line, "HUGE_%dx%d_ROW%d=got %06lX want %06lX", w, h, first_bad,
                 (long)c, (long)expected(0, first_bad));
        put_line(line);
    }
    /* Through the screen: BitBlt and StretchBlt (GDI simulates stretching
       through DIBs). Destination row j should show source row j * h / dh. */
    {
        static const int heights[3] = {0, 10, 37};
        int k, dh, j, src_y;
        for (k = 0; k < 3; ++k) {
            dh = h + heights[k];
            if (k == 0) BitBlt(screen, 0, 0, w, h, dc, 0, 0, SRCCOPY);
            else StretchBlt(screen, 0, 0, w, dh, dc, 0, 0, w, h, SRCCOPY);
            bad_rows = 0;
            first_bad = -1;
            for (j = 0; j < dh; ++j) {
                src_y = (int)((long)j * h / dh);
                c = GetPixel(screen, 2, j);
                if (!close_enough(c, expected(2, src_y), bytes_pixel) &&
                    (src_y + 1 >= h || !close_enough(c, expected(2, src_y + 1), bytes_pixel)) &&
                    (src_y == 0 || !close_enough(c, expected(2, src_y - 1), bytes_pixel))) {
                    if (first_bad < 0) first_bad = j;
                    ++bad_rows;
                }
            }
            wsprintf(line, "SCREEN_%dx%d_TO_%d=%d,%d", w, h, dh, first_bad, bad_rows);
            put_line(line);
            if (first_bad >= 0) {
                c = GetPixel(screen, 2, first_bad);
                wsprintf(line, "SCREEN_%dx%d_TO_%d_ROW%d=got %06lX", w, h, dh, first_bad,
                         (long)c);
                put_line(line);
            }
        }
    }
    SelectObject(dc, old);

    /* GDI's own view: GetBitmapBits back and compare with what was set. */
    for (i = 0; i < total; ++i) data[i] = 0xEE;
    GetBitmapBits(bitmap, total, (LPVOID)data);
    bad_rows = 0;
    first_bad = -1;
    for (y = 0; y < h; ++y) {
        DWORD at = (DWORD)y * row_bytes;
        if (data[at + 2] != GetRValue(expected(0, y)) && bytes_pixel == 4) {
            if (first_bad < 0) first_bad = y;
            ++bad_rows;
        }
    }
    wsprintf(line, "GETBITMAPBITS_%dx%d=%d,%d", w, h, first_bad, bad_rows);
    put_line(line);

    GlobalUnlock(memory);
    GlobalFree(memory);
    DeleteDC(dc);
    DeleteObject(bitmap);
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
    put_line("DISPLAY_HUGE_PROBE=1");
    settle(4000);
    screen = GetDC((HWND)0);
    if (screen != (HDC)0) {
        run(screen, 122, 100);  /* one segment */
        run(screen, 122, 450);  /* Paintbrush tool palette size */
        run(screen, 640, 200);
        run(screen, 300, 300);
        ReleaseDC((HWND)0, screen);
    }
    put_line("SHUTDOWN_REQUESTED=YES");
    _lclose(result_file);
    result_file = HFILE_ERROR;
    ExitWindows(0, 0);
    return 0;
}
