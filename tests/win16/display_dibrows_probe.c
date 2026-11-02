#include <windows.h>
#include <string.h>

/* Win16 DIB row-placement probe.

   SetDIBits / GetDIBits with DIB headers whose height differs from the
   bitmap's, and with start scans, then reports which bitmap rows were
   written or read. Run it on Microsoft's VGA.DRV for the reference
   behaviour and on another driver to compare. Writes D:\OUTPUT\RESULT.TXT:
       SET_<case>=<row of each DIB scan, top = 0, '-' = not found>
       GET_<case>=<bitmap row each returned scan came from>            */

#define RESULT_PATH "D:\\OUTPUT\\RESULT.TXT"
#define W 8
#define H 8

static HFILE result_file = HFILE_ERROR;

typedef struct {
    BITMAPINFOHEADER h;
    RGBQUAD c[16];
} INFO4;

/* Eight distinct VGA colours, one per row marker. */
static const BYTE marks[8][3] = {
    {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 0},
    {0, 255, 255}, {255, 0, 255}, {128, 0, 0}, {0, 128, 0}
};

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

static void make_info(INFO4 *info, int height)
{
    int i;
    memset(info, 0, sizeof(*info));
    info->h.biSize = sizeof(BITMAPINFOHEADER);
    info->h.biWidth = W;
    info->h.biHeight = height;
    info->h.biPlanes = 1;
    info->h.biBitCount = 4;
    info->h.biClrUsed = 16;
    for (i = 0; i < 8; ++i) {
        info->c[i + 1].rgbRed = marks[i][0];
        info->c[i + 1].rgbGreen = marks[i][1];
        info->c[i + 1].rgbBlue = marks[i][2];
    }
    /* index 0 stays black */
}

/* Which marker colour (1..8) is at row y of the bitmap selected in dc. */
static int marker_at(HDC dc, int y)
{
    COLORREF c = GetPixel(dc, 3, y);
    int i;
    for (i = 0; i < 8; ++i) {
        if (GetRValue(c) == marks[i][0] && GetGValue(c) == marks[i][1] &&
            GetBValue(c) == marks[i][2]) return i + 1;
    }
    return 0;
}

/* SetDIBits of `lines` scans (scan k carries marker start+k+1) starting at
   `start` with a DIB header of `dib_height`; report the bitmap row of each. */
static void set_case(HDC screen, LPCSTR name, int start, int lines, int dib_height)
{
    char line[96], part[8];
    INFO4 info;
    BYTE bits[4 * H];
    HBITMAP bitmap = CreateCompatibleBitmap(screen, W, H);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP old;
    int k, y, r;

    make_info(&info, dib_height);
    for (k = 0; k < lines; ++k) memset(bits + 4 * k, (start + k + 1) * 0x11, 4);
    old = SelectObject(dc, bitmap);
    PatBlt(dc, 0, 0, W, H, BLACKNESS);
    SelectObject(dc, old);
    r = SetDIBits(screen, bitmap, start, lines, bits, (BITMAPINFO FAR *)&info,
                  DIB_RGB_COLORS);
    old = SelectObject(dc, bitmap);
    wsprintf(line, "SET_%s=%d:", name, r);
    for (k = 0; k < lines; ++k) {
        lstrcpy(part, " -");
        for (y = 0; y < H; ++y) {
            if (marker_at(dc, y) == start + k + 1) {
                wsprintf(part, " %d", y);
                break;
            }
        }
        lstrcat(line, part);
    }
    put_line(line);
    SelectObject(dc, old);
    DeleteDC(dc);
    DeleteObject(bitmap);
}

/* Fill bitmap row y with marker y+1, GetDIBits `lines` scans from `start`
   with a header of `dib_height`, report which row each scan came from. */
static void get_case(HDC screen, LPCSTR name, int start, int lines, int dib_height)
{
    char line[96], part[8];
    INFO4 info;
    BYTE bits[4 * H];
    HBITMAP bitmap = CreateCompatibleBitmap(screen, W, H);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP old;
    HBRUSH brush;
    RECT rect;
    int k, y, r, value;

    old = SelectObject(dc, bitmap);
    for (y = 0; y < H; ++y) {
        brush = CreateSolidBrush(RGB(marks[y][0], marks[y][1], marks[y][2]));
        SetRect(&rect, 0, y, W, y + 1);
        FillRect(dc, &rect, brush);
        DeleteObject(brush);
    }
    SelectObject(dc, old);
    make_info(&info, dib_height);
    memset(bits, 0xEE, sizeof(bits));
    r = GetDIBits(screen, bitmap, start, lines, bits, (BITMAPINFO FAR *)&info,
                  DIB_RGB_COLORS);
    wsprintf(line, "GET_%s=%d:", name, r);
    for (k = 0; k < lines; ++k) {
        value = bits[4 * k + 1] & 0x0F;
        lstrcpy(part, " -");
        /* The returned colour table may be reordered: match by colour. */
        for (y = 0; y < H; ++y) {
            if (info.c[value].rgbRed == marks[y][0] && info.c[value].rgbGreen == marks[y][1] &&
                info.c[value].rgbBlue == marks[y][2]) {
                wsprintf(part, " %d", y);
                break;
            }
        }
        lstrcat(line, part);
    }
    put_line(line);
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
    put_line("DISPLAY_DIBROWS_PROBE=1");
    settle(4000);
    screen = GetDC((HWND)0);
    if (screen != (HDC)0) {
        char line[48];
        wsprintf(line, "BITSPIXEL=%d", GetDeviceCaps(screen, BITSPIXEL));
        put_line(line);
        set_case(screen, "FULL", 0, 8, 8);
        set_case(screen, "S0_N2_H2", 0, 2, 2);
        set_case(screen, "S2_N2_H8", 2, 2, 8);
        set_case(screen, "S2_N2_H2", 2, 2, 2);
        set_case(screen, "S2_N2_H4", 2, 2, 4);
        set_case(screen, "S0_N2_H12", 0, 2, 12);
        get_case(screen, "FULL", 0, 8, 8);
        get_case(screen, "S0_N1_H1", 0, 1, 1);
        get_case(screen, "S3_N1_H8", 3, 1, 8);
        get_case(screen, "S3_N1_H1", 3, 1, 1);
        get_case(screen, "S0_N2_H2", 0, 2, 2);
        ReleaseDC((HWND)0, screen);
    }
    put_line("SHUTDOWN_REQUESTED=YES");
    _lclose(result_file);
    result_file = HFILE_ERROR;
    ExitWindows(0, 0);
    return 0;
}
