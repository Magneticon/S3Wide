#include <windows.h>
#include <string.h>

/* Win16 probe: which source column/row GDI's own StretchBlt emulation
   picks for each destination pixel when enlarging. A 1-row (or 1-column)
   8-bpp DIB with a distinct colour per index is stretched with MERGECOPY
   and a white brush (always emulated by GDI); each destination pixel's
   colour gives the source index. Writes D:\OUTPUT\RESULT.TXT:
       MAPX_<src>_<dst>=i0 i1 i2 ...     MAPY_<src>_<dst>=...            */

#define RESULT_PATH "D:\\OUTPUT\\RESULT.TXT"

static HFILE result_file = HFILE_ERROR;
static struct {
    BITMAPINFOHEADER h;
    RGBQUAD c[256];
} info;
static BYTE bits[256 * 4];

static void put_text(LPCSTR text)
{
    if (result_file != HFILE_ERROR) _lwrite(result_file, text, lstrlen(text));
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

static int index_of(COLORREF c)
{
    /* colour i: red = i, green = 255 - i, blue = 128 */
    if (GetBValue(c) != 128 || GetRValue(c) + GetGValue(c) != 255) return -1;
    return GetRValue(c);
}

static void map_case(HDC screen, int vertical, int src, int dst)
{
    char part[16];
    int i, k;
    HBRUSH old = SelectObject(screen, GetStockObject(WHITE_BRUSH));
    memset(&info.h, 0, sizeof(info.h));
    info.h.biSize = sizeof(BITMAPINFOHEADER);
    info.h.biWidth = vertical ? 1 : src;
    info.h.biHeight = vertical ? src : 1;
    info.h.biPlanes = 1;
    info.h.biBitCount = 8;
    info.h.biClrUsed = 256;
    for (i = 0; i < 256; ++i) {
        info.c[i].rgbRed = (BYTE)i;
        info.c[i].rgbGreen = (BYTE)(255 - i);
        info.c[i].rgbBlue = 128;
    }
    memset(bits, 0, sizeof(bits));
    for (i = 0; i < src; ++i) {
        /* Vertical: one byte per row, rows padded to 4 bytes; DIB row 0 is
           the bottom, so index rows from the top. */
        if (vertical) bits[(src - 1 - i) * 4] = (BYTE)i;
        else bits[i] = (BYTE)i;
    }
    PatBlt(screen, 0, 0, vertical ? 4 : dst, vertical ? dst : 4, BLACKNESS);
    StretchDIBits(screen, 0, 0, vertical ? 1 : dst, vertical ? dst : 1, 0, 0,
                  vertical ? 1 : src, vertical ? src : 1, bits, (BITMAPINFO FAR *)&info,
                  DIB_RGB_COLORS, MERGECOPY);
    wsprintf(part, "MAP%c_%d_%d=", vertical ? 'Y' : 'X', src, dst);
    put_text(part);
    for (k = 0; k < dst; ++k) {
        wsprintf(part, "%d ", index_of(GetPixel(screen, vertical ? 0 : k, vertical ? k : 0)));
        put_text(part);
    }
    put_text("\r\n");
    SelectObject(screen, old);
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
    put_text("DISPLAY_STRETCHMAP_PROBE=1\r\n");
    settle(3000);
    screen = GetDC((HWND)0);
    if (screen != (HDC)0) {
        map_case(screen, 0, 40, 103);
        map_case(screen, 0, 20, 70);
        map_case(screen, 0, 25, 76);
        map_case(screen, 0, 3, 8);
        map_case(screen, 0, 7, 10);
        map_case(screen, 1, 30, 70);
        map_case(screen, 1, 10, 45);
        map_case(screen, 1, 19, 57);
        map_case(screen, 1, 3, 8);
        ReleaseDC((HWND)0, screen);
    }
    put_text("SHUTDOWN_REQUESTED=YES\r\n");
    _lclose(result_file);
    result_file = HFILE_ERROR;
    ExitWindows(0, 0);
    return 0;
}
