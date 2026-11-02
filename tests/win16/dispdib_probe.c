#include <windows.h>
#include <string.h>
#include <stdlib.h>
#include <conio.h>

/* Win16 probe for dispdib/DISPDIB.DLL: plays generated 8-bpp frames the way
   MCIAVI does (DisplayDibEx BEGIN, frames with NOWAIT, END) and checks what
   reaches the screen by reading the VESA frame buffer back:
     - the mode in use (VBE 4F03/4F01),
     - a 16:9 frame (320x180, with a circle) and a 4:3 frame (240x180) each
       fitted at their own aspect ratio and centred, black around them,
     - an RLE8 frame drawn the same as the uncompressed one,
     - the palette (DAC readback),
     - frames per second for 320x180 frames,
     - the desktop working again after END (Resurrection),
     - a single DisplayDib call (implicit begin and end).
   With a hold time argument (VISIBLE=1 runs) it writes D:\OUTPUT\READY.TXT
   while the circle frame is up and keeps it there that many seconds.
   Writes D:\OUTPUT\RESULT.TXT. */

#define RESULT_PATH "D:\\OUTPUT\\RESULT.TXT"
#define READY_PATH "D:\\OUTPUT\\READY.TXT"

#define DISPLAYDIB_NOWAIT 0x0040
#define DISPLAYDIB_END 0x4000
#define DISPLAYDIB_BEGIN 0x8000

typedef UINT (FAR PASCAL *DISPLAYDIBEX)(LPBITMAPINFOHEADER, int, int, LPSTR, WORD);
typedef UINT (FAR PASCAL *DISPLAYDIBFN)(LPBITMAPINFOHEADER, LPSTR, WORD);

static HFILE result_file = HFILE_ERROR;
static struct {
    BITMAPINFOHEADER h;
    RGBQUAD c[256];
} info;
static BYTE frame[320U * 180U];
static BYTE huge *rle;
static BYTE readrow[1280];
static int failures;

static void put_text(LPCSTR text)
{
    if (result_file != HFILE_ERROR) _lwrite(result_file, text, lstrlen(text));
}

static void put_value(LPCSTR name, long value)
{
    char line[80];
    wsprintf(line, "%s=%ld\r\n", name, value);
    put_text(line);
}

static void check(LPCSTR name, BOOL ok)
{
    char line[80];
    wsprintf(line, "%s=%s\r\n", name, ok ? (LPSTR)"PASS" : (LPSTR)"FAIL");
    put_text(line);
    if (!ok) ++failures;
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

/* ------------------------------------------------------- VESA readback */

#pragma pack(1)
typedef struct {
    DWORD edi, esi, ebp, reserved, ebx, edx, ecx, eax;
    WORD flags, es, ds, fs, gs, ip, cs, sp, ss;
} RMCS;
#pragma pack()

static RMCS rmcs;

static WORD int10(WORD ax, WORD bx, WORD cx, WORD es, WORD di, WORD *bx_out)
{
    RMCS FAR *rm = &rmcs;
    WORD failed;
    _fmemset(rm, 0, sizeof(*rm));
    rm->eax = ax;
    rm->ebx = bx;
    rm->ecx = cx;
    rm->es = es;
    rm->edi = di;
    _asm {
        push es
        push di
        les di, rm
        mov ax, 0300h
        mov bx, 0010h
        xor cx, cx
        int 31h
        sbb ax, ax
        pop di
        pop es
        mov failed, ax
    }
    if (bx_out != NULL) *bx_out = (WORD)rm->ebx;
    return failed ? 0xFFFFU : (WORD)rm->eax;
}

static DWORD map_physical(DWORD physical, DWORD bytes)
{
    WORD ph = HIWORD(physical), pl = LOWORD(physical);
    WORD sh = HIWORD(bytes), sl = LOWORD(bytes), lh, ll, failed;
    _asm {
        push si
        push di
        mov bx, ph
        mov cx, pl
        mov si, sh
        mov di, sl
        mov ax, 0800h
        int 31h
        sbb ax, ax
        pop di
        pop si
        mov failed, ax
        mov lh, bx
        mov ll, cx
    }
    return failed ? 0 : MAKELONG(ll, lh);
}

static WORD get16(const BYTE FAR *p) { return (WORD)(p[0] | (p[1] << 8)); }
static DWORD get32(const BYTE FAR *p) { return MAKELONG(get16(p), get16(p + 2)); }

static WORD scr_w, scr_h, scr_pitch, scr_mode, sel, aspect_x, aspect_y;
static DWORD scr_linear, sel_base = 0xFFFFFFFFUL;

/* Learn the current VESA mode and map its frame buffer. */
static BOOL screen_open(void)
{
    DWORD dos = GlobalDosAlloc(512);
    BYTE FAR *mi;
    WORD mode = 0;
    if (dos == 0) return FALSE;
    if (int10(0x4F03, 0, 0, 0, 0, &mode) != 0x004F) {
        GlobalDosFree(LOWORD(dos));
        return FALSE;
    }
    scr_mode = mode & 0x3FFF;
    mi = (BYTE FAR *)MAKELP(LOWORD(dos), 0);
    if (int10(0x4F01, 0, scr_mode, HIWORD(dos), 0, NULL) != 0x004F) {
        GlobalDosFree(LOWORD(dos));
        return FALSE;
    }
    scr_w = get16(mi + 0x12);
    scr_h = get16(mi + 0x14);
    scr_pitch = get16(mi + 0x32) ? get16(mi + 0x32) : get16(mi + 0x10);
    if (scr_linear == 0) {
        scr_linear = map_physical(get32(mi + 0x28), 0x400000UL);
    }
    GlobalDosFree(LOWORD(dos));
    if (sel == 0) {
        sel = AllocSelector(HIWORD((DWORD)(LPVOID)&rmcs));
        SetSelectorLimit(sel, 0xFFFFUL);
    }
    return scr_linear != 0 && sel != 0;
}

static const BYTE FAR *screen_row(WORD y)
{
    DWORD base = scr_linear + (DWORD)y * scr_pitch;
    if (base != sel_base) {
        SetSelectorBase(sel, base);
        sel_base = base;
    }
    return (const BYTE FAR *)MAKELP(sel, 0);
}

/* ---------------------------------------------------------- frames */

static void make_palette(void)
{
    int i;
    memset(&info.h, 0, sizeof(info.h));
    info.h.biSize = sizeof(BITMAPINFOHEADER);
    info.h.biPlanes = 1;
    info.h.biBitCount = 8;
    info.h.biClrUsed = 256;
    /* 0 black; 1..8 colour bars; 16..255 a grey-blue ramp */
    for (i = 0; i < 256; ++i) {
        info.c[i].rgbRed = (BYTE)i;
        info.c[i].rgbGreen = (BYTE)i;
        info.c[i].rgbBlue = (BYTE)(64 + i * 3 / 4);
    }
    info.c[0].rgbRed = info.c[0].rgbGreen = info.c[0].rgbBlue = 0;
    for (i = 1; i <= 8; ++i) {
        info.c[i].rgbRed = (BYTE)((i & 1) ? 255 : 0);
        info.c[i].rgbGreen = (BYTE)((i & 2) ? 255 : 0);
        info.c[i].rgbBlue = (BYTE)((i & 4) ? 255 : 0);
    }
    info.c[9].rgbRed = 255;
    info.c[9].rgbGreen = 255;
    info.c[9].rgbBlue = 255;
}

/* Frame w x h (bottom-up rows): bars in the top fifth, a ramp, a white
   circle of radius h/3 in the middle (round only if pixels stay square),
   a 9-wide border, and a marker square that moves with n. */
static void make_frame(int w, int h, int n)
{
    int x, y, r = h / 3, cx = w / 2, cy = h / 2;
    for (y = 0; y < h; ++y) {
        BYTE *row = frame + (long)(h - 1 - y) * w;   /* y counts from the top */
        for (x = 0; x < w; ++x) {
            long dx = x - cx, dy = y - cy, d = dx * dx + dy * dy;
            BYTE v;
            if (y < h / 5) v = (BYTE)(1 + x * 8 / w);
            else v = (BYTE)(16 + (x + y) % 240);
            if (d <= (long)r * r && d >= (long)(r - 3) * (r - 3)) v = 9;
            if (x < 2 || y < 2 || x >= w - 2 || y >= h - 2) v = 9;
            if (x >= (n * 3) % (w - 16) && x < (n * 3) % (w - 16) + 16 &&
                y >= h - 24 && y < h - 8) v = 3;
            row[x] = v;
        }
    }
    info.h.biWidth = w;
    info.h.biHeight = h;
    info.h.biCompression = BI_RGB;
    info.h.biSizeImage = (DWORD)w * h;
}

/* RLE8-encode the current frame (encoded runs only, end of line codes). */
static DWORD encode_rle8(int w, int h)
{
    DWORD n = 0;
    int x, y;
    for (y = 0; y < h; ++y) {
        const BYTE *row = frame + (long)y * w;
        x = 0;
        while (x < w) {
            int run = 1;
            while (x + run < w && run < 255 && row[x + run] == row[x]) ++run;
            rle[n++] = (BYTE)run;
            rle[n++] = row[x];
            x += run;
        }
        rle[n++] = 0;
        rle[n++] = 0;
    }
    rle[n - 1] = 1;   /* last end of line becomes end of bitmap */
    return n;
}

/* Compare the screen with the frame fitted at its own aspect, centred,
   black (index 0) around it. Returns mismatching pixels. */
static long verify_fit(int w, int h, int *out_dw, int *out_dh)
{
    int dw, dh, x0, y0, x, y;
    long bad = 0;
    /* Fitted for the display's shape (Aspect, default 16:9), as the DLL
       documents; Aspect=0 means square mode pixels. */
    long ax = aspect_x ? aspect_x : scr_w, ay = aspect_x ? aspect_y : scr_h;
    if ((long)w * ay >= (long)h * ax) {
        dw = scr_w;
        dh = (int)((long)scr_h * ax * h / (ay * w));
    } else {
        dh = scr_h;
        dw = (int)((long)scr_w * w * ay / ((long)h * ax));
    }
    x0 = (scr_w - dw) / 2;
    y0 = (scr_h - dh) / 2;
    *out_dw = dw;
    *out_dh = dh;
    for (y = 0; y < (int)scr_h; ++y) {
        const BYTE FAR *row = screen_row((WORD)y);
        _fmemcpy(readrow, row, scr_w);
        for (x = 0; x < (int)scr_w; ++x) {
            BYTE want = 0;
            if (x >= x0 && x < x0 + dw && y >= y0 && y < y0 + dh) {
                int sx = (int)(((long)(2 * (x - x0) + 1) * w) / (2L * dw));
                int sy = (int)(((long)(2 * (y - y0) + 1) * h) / (2L * dh));
                want = frame[(long)(h - 1 - sy) * w + sx];
            }
            if (readrow[x] != want) ++bad;
        }
    }
    return bad;
}

static BOOL verify_palette(void)
{
    int i;
    BOOL ok = TRUE;
    outp(0x3C7, 0);
    for (i = 0; i < 256; ++i) {
        BYTE r = (BYTE)inp(0x3C9), g = (BYTE)inp(0x3C9), b = (BYTE)inp(0x3C9);
        if (r != (info.c[i].rgbRed >> 2) || g != (info.c[i].rgbGreen >> 2) ||
            b != (info.c[i].rgbBlue >> 2)) ok = FALSE;
    }
    return ok;
}

int PASCAL WinMain(HINSTANCE instance, HINSTANCE previous,
                   LPSTR command_line, int show_command)
{
    HINSTANCE lib;
    DISPLAYDIBEX ddex;
    DISPLAYDIBFN dd;
    UINT r;
    int hold = atoi(command_line), dw, dh, n;
    long bad;
    DWORD t0, t1, size;
    HDC screen;
    (void)instance;
    (void)previous;
    (void)show_command;

    result_file = _lcreat(RESULT_PATH, 0);
    if (result_file == HFILE_ERROR) return 2;
    put_text("DISPDIB_PROBE=1\r\n");
    settle(2000);
    lib = LoadLibrary("DISPDIB.DLL");
    put_value("LOADLIBRARY", (long)(UINT)lib);
    if ((UINT)lib < 32) goto done;
    ddex = (DISPLAYDIBEX)GetProcAddress(lib, "DisplayDibEx");
    dd = (DISPLAYDIBFN)GetProcAddress(lib, "DisplayDib");
    check("EXPORTS", ddex != NULL && dd != NULL);
    if (ddex == NULL || dd == NULL) goto done;

    {
        char text[16], *colon;
        GetPrivateProfileString("dispdib", "Aspect", "16:9", text, sizeof(text), "system.ini");
        colon = strchr(text, ':');
        if (colon != NULL) {
            *colon = 0;
            aspect_x = (WORD)atoi(text);
            aspect_y = (WORD)atoi(colon + 1);
        }
        if (aspect_x == 0 || aspect_y == 0) aspect_x = aspect_y = 0;
        put_value("ASPECT_X", aspect_x);
        put_value("ASPECT_Y", aspect_y);
    }
    make_palette();
    make_frame(320, 180, 0);
    r = ddex(&info.h, 0, 0, NULL, DISPLAYDIB_BEGIN | DISPLAYDIB_NOWAIT | 0x0400);
    put_value("TEST_RESULT", r);
    r = ddex(&info.h, 0, 0, NULL, DISPLAYDIB_BEGIN | DISPLAYDIB_NOWAIT);
    put_value("BEGIN_RESULT", r);
    if (r != 0) goto done;

    /* 16:9 frame */
    r = ddex(&info.h, 0, 0, (LPSTR)frame, DISPLAYDIB_NOWAIT);
    if (screen_open()) {
        put_value("MODE", scr_mode);
        put_value("MODE_WIDTH", scr_w);
        put_value("MODE_HEIGHT", scr_h);
        bad = verify_fit(320, 180, &dw, &dh);
        put_value("WIDE_FIT_W", dw);
        put_value("WIDE_FIT_H", dh);
        put_value("WIDE_BAD_PIXELS", bad);
        check("WIDE_FRAME", r == 0 && bad == 0);
        check("PALETTE", verify_palette());
    } else {
        check("SCREEN_OPEN", FALSE);
    }
    if (hold > 0) {
        HFILE ready = _lcreat(READY_PATH, 0);
        if (ready != HFILE_ERROR) _lclose(ready);
        t0 = GetTickCount();
        while (GetTickCount() - t0 < (DWORD)hold * 1000) Yield();
    }

    /* RLE8 of the same picture */
    make_frame(320, 180, 7);
    rle = (BYTE huge *)GlobalLock(GlobalAlloc(GMEM_MOVEABLE, 320UL * 180 * 2 + 1024));
    size = rle ? encode_rle8(320, 180) : 0;
    info.h.biCompression = BI_RLE8;
    info.h.biSizeImage = size;
    r = ddex(&info.h, 0, 0, (LPSTR)rle, DISPLAYDIB_NOWAIT);
    bad = sel ? verify_fit(320, 180, &dw, &dh) : -1;
    put_value("RLE_BAD_PIXELS", bad);
    check("RLE_FRAME", r == 0 && bad == 0);

    /* 4:3 frame: pillarboxed */
    make_frame(240, 180, 0);
    r = ddex(&info.h, 0, 0, (LPSTR)frame, DISPLAYDIB_NOWAIT);
    bad = sel ? verify_fit(240, 180, &dw, &dh) : -1;
    put_value("NARROW_FIT_W", dw);
    put_value("NARROW_FIT_H", dh);
    put_value("NARROW_BAD_PIXELS", bad);
    check("NARROW_FRAME", r == 0 && bad == 0);

    /* Back to 16:9 (the borders must be cleared again) and time frames. */
    make_frame(320, 180, 0);
    t0 = GetTickCount();
    for (n = 0; n < 120; ++n) {
        make_frame(320, 180, n);
        ddex(&info.h, 0, 0, (LPSTR)frame, DISPLAYDIB_NOWAIT);
    }
    t1 = GetTickCount();
    put_value("FRAMES_120_MS", (long)(t1 - t0));
    t0 = GetTickCount();
    for (n = 0; n < 120; ++n) make_frame(320, 180, n);
    put_value("MAKE_120_MS", (long)(GetTickCount() - t0));
    bad = sel ? verify_fit(320, 180, &dw, &dh) : -1;
    check("WIDE_AFTER_NARROW", bad == 0);

    r = ddex(NULL, 0, 0, NULL, DISPLAYDIB_END);
    put_value("END_RESULT", r);
    settle(1500);

    /* The desktop is back: draw and read a pixel through GDI. */
    screen = GetDC((HWND)0);
    put_value("HORZRES", GetDeviceCaps(screen, HORZRES));
    put_value("VERTRES", GetDeviceCaps(screen, VERTRES));
    PatBlt(screen, 10, 10, 20, 20, WHITENESS);
    check("DESKTOP_WHITE", (GetPixel(screen, 15, 15) & 0xFFFFFFUL) == 0xFFFFFFUL);
    PatBlt(screen, 10, 10, 20, 20, BLACKNESS);
    check("DESKTOP_BLACK", (GetPixel(screen, 15, 15) & 0xFFFFFFUL) == 0);
    ReleaseDC((HWND)0, screen);

    /* A single DisplayDib call with NOWAIT: implicit begin and end. */
    make_frame(320, 180, 0);
    r = dd(&info.h, (LPSTR)frame, DISPLAYDIB_NOWAIT);
    put_value("SINGLE_RESULT", r);
    settle(1500);
    screen = GetDC((HWND)0);
    PatBlt(screen, 10, 10, 20, 20, WHITENESS);
    check("DESKTOP_AFTER_SINGLE", (GetPixel(screen, 15, 15) & 0xFFFFFFUL) == 0xFFFFFFUL);
    ReleaseDC((HWND)0, screen);

done:
    put_value("FAILURES", failures);
    put_text("SHUTDOWN_REQUESTED=YES\r\n");
    _lclose(result_file);
    result_file = HFILE_ERROR;
    ExitWindows(0, 0);
    return 0;
}
