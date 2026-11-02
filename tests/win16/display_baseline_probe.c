#include <windows.h>
#include <string.h>

/* Win16 display baseline probe.

   Records what Windows reports for the active display driver, reads the
   screen back through GDI into a 24-bpp BMP, then ends the Windows session.
   The readback goes through the display driver under test, so it is
   complementary to, not a replacement for, an emulator-side capture. */

#define RESULT_PATH "D:\\OUTPUT\\RESULT.TXT"
#define SCREEN_PATH "D:\\OUTPUT\\SCREEN.BMP"
#define READY_PATH "D:\\OUTPUT\\READY.TXT"
#define SETTLE_MS 8000UL
/* Most rows per readback band; fewer when a band would pass 64 KiB. */
#define MAX_BAND_ROWS 16

static HFILE result_file = HFILE_ERROR;

static void put_line(LPCSTR line)
{
    if (result_file != HFILE_ERROR) {
        _lwrite(result_file, line, lstrlen(line));
        _lwrite(result_file, "\r\n", 2);
    }
}

static void put_int(LPCSTR key, long value)
{
    char line[96];
    wsprintf(line, "%s=%ld", key, value);
    put_line(line);
}

static void settle(DWORD milliseconds)
{
    DWORD start = GetTickCount();
    MSG msg;

    /* Let Program Manager and the shell finish painting. */
    while (GetTickCount() - start < milliseconds) {
        while (PeekMessage(&msg, (HWND)0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Yield();
    }
}

static void report_display(HDC screen)
{
    char driver[80];

    put_int("GETWINFLAGS", (long)GetWinFlags());
    put_int("WF_ENHANCED", (GetWinFlags() & WF_ENHANCED) ? 1L : 0L);
    put_int("HORZRES", (long)GetDeviceCaps(screen, HORZRES));
    put_int("VERTRES", (long)GetDeviceCaps(screen, VERTRES));
    put_int("BITSPIXEL", (long)GetDeviceCaps(screen, BITSPIXEL));
    put_int("PLANES", (long)GetDeviceCaps(screen, PLANES));
    put_int("NUMCOLORS", (long)GetDeviceCaps(screen, NUMCOLORS));
    put_int("RASTERCAPS", (long)GetDeviceCaps(screen, RASTERCAPS));
    put_int("SIZEPALETTE", (long)GetDeviceCaps(screen, SIZEPALETTE));
    put_int("SM_CXSCREEN", (long)GetSystemMetrics(SM_CXSCREEN));
    put_int("SM_CYSCREEN", (long)GetSystemMetrics(SM_CYSCREEN));
    GetPrivateProfileString("boot", "display.drv", "", driver,
                            sizeof(driver), "SYSTEM.INI");
    put_line("DISPLAY_DRV_BEGIN");
    put_line(driver);
    put_line("DISPLAY_DRV_END");
}

/* Returns 0 on success, otherwise a stage number for the log. */
static int capture_screen(HDC screen)
{
    int width = GetDeviceCaps(screen, HORZRES);
    int height = GetDeviceCaps(screen, VERTRES);
    DWORD row_bytes = (((DWORD)width * 3UL) + 3UL) & ~3UL;
    int band_rows = MAX_BAND_ROWS;
    DWORD band_bytes;
    BITMAPFILEHEADER file_header;
    BITMAPINFO info;
    HGLOBAL band_memory;
    char FAR *band;
    HDC memory_dc;
    HBITMAP band_bitmap;
    HBITMAP old_bitmap;
    HFILE image;
    int top;
    int rows;
    int stage = 0;

    /* The largest band that fits one segment and divides the height. */
    while (band_rows > 1 &&
           (row_bytes * band_rows > 65520UL || height % band_rows != 0)) {
        --band_rows;
    }
    band_bytes = row_bytes * band_rows;
    if (width <= 0 || height <= 0 || (height % band_rows) != 0 ||
        band_bytes > 65520UL || sizeof(file_header) != 14) {
        return 1;
    }

    band_memory = GlobalAlloc(GMEM_MOVEABLE, band_bytes);
    if (band_memory == (HGLOBAL)0) {
        return 2;
    }
    band = (char FAR *)GlobalLock(band_memory);
    memory_dc = CreateCompatibleDC(screen);
    band_bitmap = CreateCompatibleBitmap(screen, width, band_rows);
    image = _lcreat(SCREEN_PATH, 0);
    if (band == (char FAR *)0 || memory_dc == (HDC)0 ||
        band_bitmap == (HBITMAP)0 || image == HFILE_ERROR) {
        stage = 3;
        goto cleanup;
    }

    _fmemset(&info, 0, sizeof(info));
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = band_rows;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 24;
    info.bmiHeader.biCompression = BI_RGB;

    file_header.bfType = 0x4D42; /* "BM" */
    file_header.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    file_header.bfSize = file_header.bfOffBits + row_bytes * (DWORD)height;
    file_header.bfReserved1 = 0;
    file_header.bfReserved2 = 0;
    _lwrite(image, (LPCSTR)&file_header, sizeof(file_header));
    info.bmiHeader.biHeight = height;
    _lwrite(image, (LPCSTR)&info.bmiHeader, sizeof(BITMAPINFOHEADER));
    info.bmiHeader.biHeight = band_rows;

    /* BMP rows are bottom-up: walk bands from the bottom of the screen. */
    for (top = height - band_rows; top >= 0; top -= band_rows) {
        old_bitmap = (HBITMAP)SelectObject(memory_dc, band_bitmap);
        if (!BitBlt(memory_dc, 0, 0, width, band_rows, screen, 0, top, SRCCOPY)) {
            SelectObject(memory_dc, old_bitmap);
            stage = 4;
            break;
        }
        /* GetDIBits requires the bitmap not to be selected into a DC. */
        SelectObject(memory_dc, old_bitmap);
        rows = GetDIBits(screen, band_bitmap, 0, band_rows, band,
                         &info, DIB_RGB_COLORS);
        if (rows != band_rows) {
            put_int("GETDIBITS_ROWS", (long)rows);
            stage = 5;
            break;
        }
        if (_lwrite(image, band, (UINT)band_bytes) != (UINT)band_bytes) {
            stage = 6;
            break;
        }
    }

cleanup:
    if (image != HFILE_ERROR) {
        _lclose(image);
    }
    if (band_bitmap != (HBITMAP)0) {
        DeleteObject(band_bitmap);
    }
    if (memory_dc != (HDC)0) {
        DeleteDC(memory_dc);
    }
    if (band != (char FAR *)0) {
        GlobalUnlock(band_memory);
    }
    GlobalFree(band_memory);
    return stage;
}

/* Optional decimal argument: seconds to keep the desktop up after the
   readback, so a host-side tool can capture the emulator window. */
static DWORD hold_seconds(LPCSTR command_line)
{
    DWORD seconds = 0;

    while (*command_line == ' ') {
        ++command_line;
    }
    while (*command_line >= '0' && *command_line <= '9' && seconds < 600) {
        seconds = seconds * 10 + (DWORD)(*command_line - '0');
        ++command_line;
    }
    return seconds;
}

/* "demo" anywhere in the command line opens applications before capture. */
static int wants_demo(LPCSTR command_line)
{
    for (; *command_line != '\0'; ++command_line) {
        if ((command_line[0] == 'd' || command_line[0] == 'D') &&
            (command_line[1] == 'e' || command_line[1] == 'E') &&
            (command_line[2] == 'm' || command_line[2] == 'M') &&
            (command_line[3] == 'o' || command_line[3] == 'O')) {
            return 1;
        }
    }
    return 0;
}

int PASCAL WinMain(HINSTANCE instance, HINSTANCE previous,
                   LPSTR command_line, int show_command)
{
    HDC screen;
    HFILE ready;
    DWORD hold = hold_seconds(command_line);

    (void)instance;
    (void)previous;
    (void)show_command;

    result_file = _lcreat(RESULT_PATH, 0);
    if (result_file == HFILE_ERROR) {
        return 2;
    }
    put_line("DISPLAY_BASELINE_PROBE=1");
    settle(SETTLE_MS);
    if (wants_demo(command_line)) {
        /* Put ordinary UI on screen: windows, menus, buttons, edit text. */
        HWND progman = FindWindow("Progman", NULL);
        if (progman != (HWND)0) ShowWindow(progman, SW_SHOWNORMAL);
        put_int("DEMO_CALC", (long)WinExec("CALC.EXE", SW_SHOWNORMAL));
        put_int("DEMO_NOTEPAD", (long)WinExec("NOTEPAD.EXE", SW_SHOWNORMAL));
        settle(SETTLE_MS);
    }

    screen = GetDC((HWND)0);
    if (screen == (HDC)0) {
        put_line("GETDC=FAIL");
    } else {
        report_display(screen);
        put_int("CAPTURE_STAGE", (long)capture_screen(screen));
        ReleaseDC((HWND)0, screen);
    }
    put_int("HOLD_SECONDS", (long)hold);
    put_line("SHUTDOWN_REQUESTED=YES");
    _lclose(result_file);
    result_file = HFILE_ERROR;

    if (hold != 0) {
        ready = _lcreat(READY_PATH, 0);
        if (ready != HFILE_ERROR) {
            _lwrite(ready, "READY\r\n", 7);
            _lclose(ready);
        }
        settle(hold * 1000UL);
    }

    /* End the isolated Windows session so DOSBox-X reaches its exit. */
    ExitWindows(0, 0);
    return 0;
}
