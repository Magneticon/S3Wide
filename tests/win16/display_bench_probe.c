#include <windows.h>
#include <string.h>

/* Win16 display timing probe.

   Times common GDI operations on the screen DC of the active display
   driver and writes one line per test to D:\OUTPUT\RESULT.TXT:
       BENCH_<name>=<reps>,<total ms>,<us per rep>
   Each test repeats until it has run for at least MIN_MS (and at least
   once). Run the emulator with fixed cycles so the times compare between
   runs; GetTickCount then counts emulated time. */

#define RESULT_PATH "D:\\OUTPUT\\RESULT.TXT"
#define SETTLE_MS 8000UL
#define MIN_MS 1500UL
#define MAX_REPS 2000
#define DIB_SIZE 256

static HFILE result_file = HFILE_ERROR;
static HDC screen;
static int width, height;
static HBRUSH solid_brush, solid_brush2, hatch_brush;
static HBITMAP memory_bitmap, original_bitmap;
static HDC memory_dc;
static HICON icon;
static BITMAPINFO FAR *dib24_info;
static BITMAPINFO FAR *dib8_info;
static BYTE huge *dib24_bits;
static BYTE huge *dib8_bits;
static DWORD total_ms;

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

    while (GetTickCount() - start < milliseconds) {
        while (PeekMessage(&msg, (HWND)0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Yield();
    }
}

typedef void (*BENCH_FN)(int rep);

static void bench(LPCSTR name, BENCH_FN fn)
{
    char line[96];
    DWORD start = GetTickCount(), elapsed;
    int reps = 0;

    do {
        fn(reps);
        ++reps;
        elapsed = GetTickCount() - start;
    } while (elapsed < MIN_MS && reps < MAX_REPS);
    total_ms += elapsed;
    wsprintf(line, "BENCH_%s=%d,%lu,%lu", name, reps, elapsed,
             elapsed * 1000UL / (DWORD)reps);
    put_line(line);
}

/* Full-screen solid fill (PatBlt PATCOPY, constant path). */
static void t_fill_solid(int rep)
{
    HBRUSH old = SelectObject(screen, (rep & 1) ? solid_brush2 : solid_brush);
    PatBlt(screen, 0, 0, width, height, PATCOPY);
    SelectObject(screen, old);
}

/* Full-screen hatched fill (pattern path). */
static void t_fill_pattern(int rep)
{
    HBRUSH old = SelectObject(screen, hatch_brush);
    (void)rep;
    PatBlt(screen, 0, 0, width, height, PATCOPY);
    SelectObject(screen, old);
}

/* Full-screen DSTINVERT (destination read-modify-write). */
static void t_dstinvert(int rep)
{
    (void)rep;
    PatBlt(screen, 0, 0, width, height, DSTINVERT);
}

/* Full-screen PATINVERT with a pattern brush. */
static void t_patinvert(int rep)
{
    HBRUSH old = SelectObject(screen, hatch_brush);
    (void)rep;
    PatBlt(screen, 0, 0, width, height, PATINVERT);
    SelectObject(screen, old);
}

/* Full-screen screen-to-screen copy, eight rows up. */
static void t_scroll(int rep)
{
    (void)rep;
    BitBlt(screen, 0, 0, width, height - 8, screen, 0, 8, SRCCOPY);
}

/* 256x256 screen -> memory -> screen, like saving under a menu. */
static void t_save_restore(int rep)
{
    int x = (rep * 16) % (width - DIB_SIZE);
    BitBlt(memory_dc, 0, 0, DIB_SIZE, DIB_SIZE, screen, x, 64, SRCCOPY);
    BitBlt(screen, x, 64 + DIB_SIZE, DIB_SIZE, DIB_SIZE, memory_dc, 0, 0, SRCCOPY);
}

static const char text_line[] =
    "The quick brown fox jumps over the lazy dog 0123456789 ABCDEFGHIJKLMNOP";

/* 40 lines of opaque system-font text. */
static void t_text_opaque(int rep)
{
    int i, line_height = 16;
    SetBkMode(screen, OPAQUE);
    SetTextColor(screen, RGB(0, 0, 0));
    SetBkColor(screen, RGB(255, 255, 255));
    for (i = 0; i < 40; ++i) {
        TextOut(screen, 8 + (rep & 7), 8 + i * line_height, text_line,
                sizeof(text_line) - 1);
    }
}

/* 40 lines of transparent text over the existing pixels. */
static void t_text_transparent(int rep)
{
    int i, line_height = 16;
    SetBkMode(screen, TRANSPARENT);
    SetTextColor(screen, RGB(0, 0, 128));
    for (i = 0; i < 40; ++i) {
        TextOut(screen, 16 + (rep & 7), 8 + i * line_height, text_line,
                sizeof(text_line) - 1);
    }
}

static void t_dib24(int rep)
{
    SetDIBitsToDevice(screen, (rep * 8) % (width - DIB_SIZE), 32,
                      DIB_SIZE, DIB_SIZE, 0, 0, 0, DIB_SIZE,
                      (LPVOID)dib24_bits, dib24_info, DIB_RGB_COLORS);
}

static void t_dib8(int rep)
{
    SetDIBitsToDevice(screen, (rep * 8) % (width - DIB_SIZE), 32 + DIB_SIZE,
                      DIB_SIZE, DIB_SIZE, 0, 0, 0, DIB_SIZE,
                      (LPVOID)dib8_bits, dib8_info, DIB_RGB_COLORS);
}

/* Screen -> memory bitmap -> 24-bpp DIB (the readback path). */
static void t_getdib24(int rep)
{
    (void)rep;
    SelectObject(memory_dc, memory_bitmap);
    BitBlt(memory_dc, 0, 0, DIB_SIZE, DIB_SIZE, screen, 0, 0, SRCCOPY);
    /* GetDIBits requires the bitmap not to be selected into a DC. */
    SelectObject(memory_dc, original_bitmap);
    GetDIBits(screen, memory_bitmap, 0, DIB_SIZE, (LPVOID)dib24_bits,
              dib24_info, DIB_RGB_COLORS);
}

/* 200 one-pixel solid lines. */
static void t_lines(int rep)
{
    int i;
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(255, 0, 0)), old;
    old = SelectObject(screen, pen);
    for (i = 0; i < 200; ++i) {
        MoveTo(screen, (i * 37 + rep) % width, (i * 11) % height);
        LineTo(screen, (i * 53) % width, (i * 29 + rep) % height);
    }
    SelectObject(screen, old);
    DeleteObject(pen);
}

/* 100 filled rectangles with a border. */
static void t_rects(int rep)
{
    int i, x, y;
    HBRUSH old = SelectObject(screen, solid_brush);
    for (i = 0; i < 100; ++i) {
        x = (i * 97 + rep * 5) % (width - 120);
        y = (i * 61) % (height - 120);
        Rectangle(screen, x, y, x + 100, y + 100);
    }
    SelectObject(screen, old);
}

/* 100 icons (mask and colour blits). */
static void t_icons(int rep)
{
    int i;
    for (i = 0; i < 100; ++i) {
        DrawIcon(screen, (i * 41 + rep) % (width - 32), (i * 23) % (height - 32), icon);
    }
}

static int make_dibs(void)
{
    DWORD bytes24 = (DWORD)DIB_SIZE * 3UL * DIB_SIZE;
    DWORD bytes8 = (DWORD)DIB_SIZE * DIB_SIZE;
    HGLOBAL h;
    DWORD i;
    int k;

    h = GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, sizeof(BITMAPINFOHEADER));
    dib24_info = (BITMAPINFO FAR *)GlobalLock(h);
    h = GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT,
                    sizeof(BITMAPINFOHEADER) + 256 * sizeof(RGBQUAD));
    dib8_info = (BITMAPINFO FAR *)GlobalLock(h);
    h = GlobalAlloc(GMEM_FIXED, bytes24);
    dib24_bits = (BYTE huge *)GlobalLock(h);
    h = GlobalAlloc(GMEM_FIXED, bytes8);
    dib8_bits = (BYTE huge *)GlobalLock(h);
    if (dib24_info == NULL || dib8_info == NULL || dib24_bits == NULL ||
        dib8_bits == NULL) {
        return 0;
    }
    dib24_info->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    dib24_info->bmiHeader.biWidth = DIB_SIZE;
    dib24_info->bmiHeader.biHeight = DIB_SIZE;
    dib24_info->bmiHeader.biPlanes = 1;
    dib24_info->bmiHeader.biBitCount = 24;
    dib8_info->bmiHeader = dib24_info->bmiHeader;
    dib8_info->bmiHeader.biBitCount = 8;
    for (k = 0; k < 256; ++k) {
        dib8_info->bmiColors[k].rgbRed = (BYTE)k;
        dib8_info->bmiColors[k].rgbGreen = (BYTE)(255 - k);
        dib8_info->bmiColors[k].rgbBlue = (BYTE)(k * 3);
    }
    for (i = 0; i < bytes24; ++i) {
        dib24_bits[i] = (BYTE)(i * 7 + (i >> 9));
    }
    for (i = 0; i < bytes8; ++i) {
        dib8_bits[i] = (BYTE)(i + (i >> 8));
    }
    return 1;
}

int PASCAL WinMain(HINSTANCE instance, HINSTANCE previous,
                   LPSTR command_line, int show_command)
{
    (void)instance;
    (void)previous;
    (void)command_line;
    (void)show_command;

    result_file = _lcreat(RESULT_PATH, 0);
    if (result_file == HFILE_ERROR) {
        return 2;
    }
    put_line("DISPLAY_BENCH_PROBE=1");
    settle(SETTLE_MS);

    screen = GetDC((HWND)0);
    if (screen == (HDC)0) {
        put_line("GETDC=FAIL");
        goto done;
    }
    width = GetDeviceCaps(screen, HORZRES);
    height = GetDeviceCaps(screen, VERTRES);
    put_int("HORZRES", (long)width);
    put_int("VERTRES", (long)height);
    put_int("BITSPIXEL", (long)GetDeviceCaps(screen, BITSPIXEL));

    solid_brush = CreateSolidBrush(RGB(0, 128, 128));
    solid_brush2 = CreateSolidBrush(RGB(192, 192, 192));
    hatch_brush = CreateHatchBrush(HS_DIAGCROSS, RGB(128, 0, 0));
    memory_dc = CreateCompatibleDC(screen);
    memory_bitmap = CreateCompatibleBitmap(screen, DIB_SIZE, DIB_SIZE);
    icon = LoadIcon((HINSTANCE)0, IDI_EXCLAMATION);
    if (solid_brush == 0 || solid_brush2 == 0 || hatch_brush == 0 ||
        memory_dc == 0 || memory_bitmap == 0 || icon == 0 || !make_dibs()) {
        put_line("SETUP=FAIL");
        goto release;
    }
    original_bitmap = SelectObject(memory_dc, memory_bitmap);

    bench("FILL_SOLID", t_fill_solid);
    bench("FILL_PATTERN", t_fill_pattern);
    bench("DSTINVERT", t_dstinvert);
    bench("PATINVERT", t_patinvert);
    bench("SCROLL", t_scroll);
    bench("SAVE_RESTORE", t_save_restore);
    bench("TEXT_OPAQUE", t_text_opaque);
    bench("TEXT_TRANSPARENT", t_text_transparent);
    bench("DIB24", t_dib24);
    bench("DIB8", t_dib8);
    bench("LINES", t_lines);
    bench("RECTS", t_rects);
    bench("ICONS", t_icons);
    bench("GETDIB24", t_getdib24);
    put_int("BENCH_TOTAL_MS", (long)total_ms);

release:
    if (memory_dc != 0) {
        if (original_bitmap != 0) SelectObject(memory_dc, original_bitmap);
        DeleteDC(memory_dc);
    }
    if (memory_bitmap != 0) DeleteObject(memory_bitmap);
    if (hatch_brush != 0) DeleteObject(hatch_brush);
    if (solid_brush2 != 0) DeleteObject(solid_brush2);
    if (solid_brush != 0) DeleteObject(solid_brush);
    ReleaseDC((HWND)0, screen);
done:
    put_line("SHUTDOWN_REQUESTED=YES");
    _lclose(result_file);
    result_file = HFILE_ERROR;
    /* Repaint before leaving so the session ends on a clean desktop. */
    InvalidateRect((HWND)0, NULL, TRUE);
    ExitWindows(0, 0);
    return 0;
}
