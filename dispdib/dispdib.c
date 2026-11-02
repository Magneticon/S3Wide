/* DISPDIB.DLL for S3WIDE: Video for Windows' full-screen display with the
   film's own shape on a widescreen display.

   Microsoft's DISPDIB.DLL shows full-screen video by switching the card to
   a 320x240 or 320x200 VGA mode and centring the frame in it with square
   pixels. A 16:9 display (DOSBox-X with aspect=false, a widescreen monitor)
   stretches that 4:3 mode to its own shape, so a 16:9 film comes out about
   21:9. This replacement keeps the interface (DisplayDib, DisplayDibEx, the
   same flags and results) and still uses a small 256-colour VESA mode, but
   lays the frame out for the shape of the display, so after the stretch
   the film has its own aspect ratio: a 16:9 film fills a 16:9 screen, a 4:3
   film gets black bars at the sides.

   The mode is the smallest 8-bpp linear-frame-buffer VESA mode that holds
   the film at its full resolution (320x200 for a 320x180 film), so a frame
   costs about what it did with the original.

   As the original does, it takes the screen from the display driver with
   GDI's Death and gives it back with Resurrection, so S3WIDE (or any driver)
   steps aside while a film plays.

   SYSTEM.INI [dispdib]:
     Aspect=16:9     shape of the display (default 16:9); Aspect=0 keeps
                     the mode's pixels square (a monitor that shows each
                     mode at its own shape)
     Width, Height   use this mode instead of choosing one
     Log=1           append calls to C:\DISPDIB.LOG */

#include <windows.h>
#include <string.h>
#include <stdlib.h>
#include <i86.h>
#include <conio.h>

#define DISPLAYDIB_NOERROR       0x0000
#define DISPLAYDIB_NOTSUPPORTED  0x0001
#define DISPLAYDIB_INVALIDDIB    0x0002
#define DISPLAYDIB_INVALIDFORMAT 0x0003

#define DISPLAYDIB_NOPALETTE     0x0010
#define DISPLAYDIB_NOCENTER      0x0020
#define DISPLAYDIB_NOWAIT        0x0040
#define DISPLAYDIB_NOIMAGE       0x0080
#define DISPLAYDIB_DONTLOCKTASK  0x0200
#define DISPLAYDIB_TEST          0x0400
#define DISPLAYDIB_END           0x4000
#define DISPLAYDIB_BEGIN         0x8000

#define MAX_WIDTH 1280
#define MAX_FRAME_WIDTH 1280

/* Imported by ordinal (see dispdib.lnk). */
BOOL FAR PASCAL Death(HDC dc);
BOOL FAR PASCAL Resurrection(HDC dc, WORD a, WORD b, WORD c, WORD d, WORD e, WORD f);
HTASK FAR PASCAL LockCurrentTask(BOOL lock);

/* ------------------------------------------------------------------ state */

static int active;                 /* inside BEGIN .. END */
static WORD screen_w, screen_h, pitch, vbe_mode;
static DWORD lfb_linear;
static WORD lfb_selector;
static WORD band_first, band_rows; /* rows reachable through lfb_selector */
static UINT old_error_mode;
static BOOL captured, locked;
static BYTE current_palette[768];
static int palette_valid;
static int last_dw, last_dh;       /* fitted frame size last drawn */
static int log_enabled = -1;
static long log_count;

static BYTE row_out[MAX_WIDTH];
static WORD xmap[MAX_WIDTH];
static BYTE src_row[MAX_FRAME_WIDTH];
static DWORD lfb_physical, lfb_bytes;

/* Persistent frame for RLE8 (delta) frames. */
static HGLOBAL frame_handle;
static BYTE huge *frame_bits;
static int frame_w, frame_h;

/* ---------------------------------------------------------------- logging */

static void log_line(LPCSTR text)
{
    HFILE f;
    if (log_enabled < 0) {
        log_enabled = GetPrivateProfileInt("dispdib", "Log", 0, "system.ini");
    }
    if (!log_enabled || log_count > 400) return;
    ++log_count;
    f = _lopen("C:\\DISPDIB.LOG", OF_WRITE);
    if (f == HFILE_ERROR) f = _lcreat("C:\\DISPDIB.LOG", 0);
    if (f == HFILE_ERROR) return;
    _llseek(f, 0L, 2);
    _lwrite(f, text, lstrlen(text));
    _lwrite(f, "\r\n", 2);
    _lclose(f);
}

/* ------------------------------------------------------------------- DPMI */

#pragma pack(1)
typedef struct {
    DWORD edi, esi, ebp, reserved, ebx, edx, ecx, eax;
    WORD flags, es, ds, fs, gs, ip, cs, sp, ss;
} RMCS;
#pragma pack()

static RMCS rmcs;

/* INT 10h in real mode through DPMI 0300h; returns AX, 0xFFFF on failure. */
static WORD int10(WORD ax, WORD bx, WORD cx, WORD es, WORD di)
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

static WORD alloc_selector(void)
{
    WORD sel, failed;
    _asm {
        xor ax, ax
        mov cx, 1
        int 31h
        sbb cx, cx
        mov sel, ax
        mov failed, cx
    }
    return failed ? 0 : sel;
}

static BOOL set_base(WORD sel, DWORD base)
{
    WORD hi = HIWORD(base), lo = LOWORD(base), failed;
    _asm {
        mov ax, 0007h
        mov bx, sel
        mov cx, hi
        mov dx, lo
        int 31h
        sbb ax, ax
        mov failed, ax
    }
    return !failed;
}

static BOOL set_limit64k(WORD sel)
{
    WORD failed;
    _asm {
        mov ax, 0008h
        mov bx, sel
        xor cx, cx
        mov dx, 0FFFFh
        int 31h
        sbb ax, ax
        mov failed, ax
    }
    return !failed;
}

static WORD segment_selector(WORD segment)
{
    WORD sel, failed;
    _asm {
        mov ax, 0002h
        mov bx, segment
        int 31h
        sbb cx, cx
        mov sel, ax
        mov failed, cx
    }
    return failed ? 0 : sel;
}

/* -------------------------------------------------------------- VESA mode */

static WORD get16(const BYTE FAR *p) { return (WORD)(p[0] | (p[1] << 8)); }
static DWORD get32(const BYTE FAR *p) { return MAKELONG(get16(p), get16(p + 2)); }

typedef struct {
    WORD mode, width, height, pitch;
    DWORD lfb;
} MODEINFO;

/* Display shape (aspect_x:aspect_y), 0:0 for square mode pixels. */
static WORD aspect_x, aspect_y;

static void read_aspect(void)
{
    char text[16], *colon;
    GetPrivateProfileString("dispdib", "Aspect", "16:9", text, sizeof(text), "system.ini");
    aspect_x = aspect_y = 0;
    colon = strchr(text, ':');
    if (colon != NULL) {
        *colon = 0;
        aspect_x = (WORD)atoi(text);
        aspect_y = (WORD)atoi(colon + 1);
    }
    if (aspect_x == 0 || aspect_y == 0) aspect_x = aspect_y = 0;
}

/* Size in mode pixels of a fw x fh film fitted to a w x h mode shown on
   the display. */
static void fit(int fw, int fh, int w, int h, int *dw, int *dh)
{
    long ax = aspect_x ? aspect_x : w, ay = aspect_x ? aspect_y : h;
    if ((long)fw * ay >= (long)fh * ax) {
        *dw = w;
        *dh = (int)((long)h * ax * fh / (ay * fw));
        if (*dh > h) *dh = h;
    } else {
        *dh = h;
        *dw = (int)((long)w * fw * ay / ((long)fh * ax));
        if (*dw > w) *dw = w;
    }
}

/* The configured mode if there is one, else the smallest mode that shows
   the film at least at its own size, else the largest mode. */
static int find_mode(int fw, int fh, WORD want_w, WORD want_h, MODEINFO *found)
{
    DWORD dos = GlobalDosAlloc(1024);
    BYTE FAR *buf;
    const WORD FAR *list;
    static WORD modes[256];
    static int listed;
    WORD count = 0, i, version;
    DWORD best = 0xFFFFFFFFUL;
    const BYTE FAR *mi;
    int ok = 0;

    if (dos == 0) return 0;
    buf = (BYTE FAR *)MAKELP(LOWORD(dos), 0);
    _fmemcpy(buf, "VBE2", 4);
    if (int10(0x4F00, 0, 0, HIWORD(dos), 0) != 0x004F || _fmemcmp(buf, "VESA", 4) != 0) {
        GlobalDosFree(LOWORD(dos));
        return 0;
    }
    version = get16(buf + 4);
    list = (const WORD FAR *)MAKELP(segment_selector(HIWORD(get32(buf + 14))),
                                    LOWORD(get32(buf + 14)));
    while (count < 256 && list[count] != 0xFFFF) {
        modes[count] = list[count];
        ++count;
    }
    for (i = 0; i < count; ++i) {
        WORD attr, w, h, p;
        int dw, dh;
        DWORD score, pixels;
        if (int10(0x4F01, 0, modes[i], HIWORD(dos), 512) != 0x004F) continue;
        mi = buf + 512;
        attr = get16(mi);
        w = get16(mi + 0x12);
        h = get16(mi + 0x14);
        if ((attr & 0x91) != 0x91 || mi[0x19] != 8 || mi[0x1B] != 4 || get32(mi + 0x28) == 0) {
            continue;
        }
        p = version >= 0x300 && get16(mi + 0x32) != 0 ? get16(mi + 0x32) : get16(mi + 0x10);
        if (w > MAX_WIDTH || p < w || w < 320 || h < 200) continue;
        if (!listed) {
            char line[48];
            wsprintf(line, "mode %X %ux%u pitch %u", modes[i], w, h, p);
            log_line(line);
        }
        pixels = (DWORD)w * h;
        fit(fw, fh, w, h, &dw, &dh);
        if (w == want_w && h == want_h) score = 0;
        else if (dw >= fw && dh >= fh) score = pixels;
        else score = 0x80000000UL - pixels;
        if (score < best) {
            best = score;
            found->mode = modes[i];
            found->width = w;
            found->height = h;
            found->pitch = p;
            found->lfb = get32(mi + 0x28);
            ok = 1;
        }
    }
    listed = 1;
    GlobalDosFree(LOWORD(dos));
    return ok;
}

/* Point lfb_selector at the 64 KiB band that holds row y; return the row's
   offset in it. */
static WORD row_offset(WORD y)
{
    if (y < band_first || y >= band_first + band_rows) {
        band_first = y;
        set_base(lfb_selector, lfb_linear + (DWORD)y * pitch);
    }
    return (WORD)((y - band_first) * pitch);
}

static void write_row(WORD y, WORD x, const BYTE FAR *src, WORD count)
{
    BYTE FAR *dst = (BYTE FAR *)MAKELP(lfb_selector, row_offset(y) + x);
    _fmemcpy(dst, src, count);
}

static void clear_screen(void)
{
    WORD y;
    _fmemset(row_out, 0, screen_w);
    for (y = 0; y < screen_h; ++y) write_row(y, 0, row_out, screen_w);
    last_dw = last_dh = 0;
}

static void set_palette(const BITMAPINFOHEADER FAR *bi)
{
    const RGBQUAD FAR *colors = (const RGBQUAD FAR *)((const BYTE FAR *)bi + bi->biSize);
    WORD n = bi->biClrUsed ? (WORD)bi->biClrUsed : 256, i;
    BYTE pal[768];
    if (n > 256) n = 256;
    _fmemset(pal, 0, sizeof(pal));
    for (i = 0; i < n; ++i) {
        pal[i * 3] = (BYTE)(colors[i].rgbRed >> 2);
        pal[i * 3 + 1] = (BYTE)(colors[i].rgbGreen >> 2);
        pal[i * 3 + 2] = (BYTE)(colors[i].rgbBlue >> 2);
    }
    if (palette_valid && _fmemcmp(pal, current_palette, sizeof(pal)) == 0) return;
    outp(0x3C8, 0);
    for (i = 0; i < 768; ++i) outp(0x3C9, pal[i]);
    _fmemcpy(current_palette, pal, sizeof(pal));
    palette_valid = 1;
}

/* ------------------------------------------------------------ begin / end */

static UINT check_format(const BITMAPINFOHEADER FAR *bi)
{
    if (bi == NULL) return DISPLAYDIB_NOERROR;
    if (bi->biSize < sizeof(BITMAPINFOHEADER)) return DISPLAYDIB_INVALIDDIB;
    if (bi->biBitCount != 8 || (bi->biCompression != BI_RGB && bi->biCompression != BI_RLE8) ||
        bi->biWidth <= 0 || bi->biWidth > MAX_FRAME_WIDTH || bi->biHeight <= 0) {
        return DISPLAYDIB_INVALIDFORMAT;
    }
    return DISPLAYDIB_NOERROR;
}

static UINT begin(const BITMAPINFOHEADER FAR *bi, WORD flags)
{
    MODEINFO m;
    HDC dc;
    DWORD bytes;
    char line[96];
    WORD want_w = (WORD)GetPrivateProfileInt("dispdib", "Width", 0, "system.ini");
    WORD want_h = (WORD)GetPrivateProfileInt("dispdib", "Height", 0, "system.ini");
    UINT r = check_format(bi);

    if (r != DISPLAYDIB_NOERROR) return r;
    read_aspect();
    if (!find_mode(bi ? (int)bi->biWidth : 320, bi ? (int)bi->biHeight : 240, want_w, want_h,
                   &m)) {
        log_line("begin: no 8-bpp linear VESA mode");
        return DISPLAYDIB_NOTSUPPORTED;
    }
    if (flags & DISPLAYDIB_TEST) return DISPLAYDIB_NOERROR;

    old_error_mode = SetErrorMode(0x8001);
    dc = GetDC(NULL);
    Death(dc);
    ReleaseDC(NULL, dc);

    if (int10(0x4F02, (WORD)(m.mode | 0x4000), 0, 0, 0) != 0x004F) {
        log_line("begin: mode set failed");
        dc = GetDC(NULL);
        Resurrection(dc, 0, 0, 0, 0, 0, 0);
        ReleaseDC(NULL, dc);
        SetErrorMode(old_error_mode);
        return DISPLAYDIB_NOTSUPPORTED;
    }
    screen_w = m.width;
    screen_h = m.height;
    pitch = m.pitch;
    vbe_mode = m.mode;
    bytes = ((DWORD)pitch * screen_h + 0xFFFFUL) & ~0xFFFFUL;
    if (lfb_linear == 0 || lfb_physical != m.lfb || lfb_bytes < bytes) {
        lfb_linear = map_physical(m.lfb, bytes);
        lfb_physical = m.lfb;
        lfb_bytes = bytes;
    }
    if (lfb_selector == 0) {
        lfb_selector = alloc_selector();
        if (lfb_selector != 0) set_limit64k(lfb_selector);
    }
    band_rows = (WORD)(65536UL / pitch);
    band_first = 0xFFFF;
    palette_valid = 0;
    if (lfb_linear == 0 || lfb_selector == 0) {
        log_line("begin: no frame buffer access");
        int10(0x0003, 0, 0, 0, 0);
        dc = GetDC(NULL);
        Resurrection(dc, 0, 0, 0, 0, 0, 0);
        ReleaseDC(NULL, dc);
        SetErrorMode(old_error_mode);
        return DISPLAYDIB_NOTSUPPORTED;
    }
    clear_screen();
    if (bi != NULL && !(flags & DISPLAYDIB_NOPALETTE)) set_palette(bi);
    locked = !(flags & DISPLAYDIB_DONTLOCKTASK);
    if (locked) LockCurrentTask(TRUE);
    captured = !(flags & DISPLAYDIB_NOWAIT);
    if (captured) SetCapture(GetActiveWindow());
    active = 1;
    wsprintf(line, "begin: mode %X %ux%u pitch %u aspect %u:%u flags %X", vbe_mode, screen_w,
             screen_h, pitch, aspect_x, aspect_y, flags);
    log_line(line);
    return DISPLAYDIB_NOERROR;
}

/* Wait for a key or mouse click (a single DisplayDib call without NOWAIT). */
static void wait_for_input(void)
{
    MSG msg;
    for (;;) {
        if (PeekMessage(&msg, NULL, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE)) {
            if (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN) return;
            continue;
        }
        if (PeekMessage(&msg, NULL, WM_MOUSEFIRST, WM_MOUSELAST, PM_REMOVE)) {
            if (msg.message == WM_LBUTTONDOWN || msg.message == WM_RBUTTONDOWN ||
                msg.message == WM_MBUTTONDOWN) return;
            continue;
        }
        WaitMessage();
    }
}

static void end(WORD flags)
{
    HDC dc;
    if (!active) return;
    if (!(flags & DISPLAYDIB_NOWAIT)) wait_for_input();
    if (captured) ReleaseCapture();
    captured = FALSE;
    int10(0x0003, 0, 0, 0, 0);
    if (locked) LockCurrentTask(FALSE);
    locked = FALSE;
    dc = GetDC(NULL);
    Resurrection(dc, 0, 0, 0, 0, 0, 0);
    ReleaseDC(NULL, dc);
    SetErrorMode(old_error_mode);
    InvalidateRect(NULL, NULL, TRUE);
    active = 0;
    log_line("end");
}

/* ------------------------------------------------------------------ frames */

/* Decode an RLE8 frame onto the persistent frame (deltas keep pixels). */
static void decode_rle8(const BYTE huge *p, DWORD size, int w, int h)
{
    DWORD pos = 0;
    int x = 0, y = 0;
    BYTE count, code, v;
    WORD i;
#define NEXT(b) do { if (pos >= size) return; (b) = p[pos++]; } while (0)
#define PUT(b) do { if (x < w && y < h) frame_bits[(DWORD)y * w + x] = (b); ++x; } while (0)
    for (;;) {
        NEXT(count);
        NEXT(code);
        if (count) {
            for (i = 0; i < count; ++i) PUT(code);
            continue;
        }
        if (code == 0) {
            x = 0;
            ++y;
        } else if (code == 1) {
            return;
        } else if (code == 2) {
            BYTE dx, dy;
            NEXT(dx);
            NEXT(dy);
            x += dx;
            y += dy;
        } else {
            for (i = 0; i < code; ++i) {
                NEXT(v);
                PUT(v);
            }
            if (code & 1) ++pos;
        }
    }
#undef NEXT
#undef PUT
}

static BOOL ensure_frame(int w, int h)
{
    if (frame_bits != NULL && frame_w == w && frame_h == h) return TRUE;
    if (frame_handle != NULL) {
        GlobalUnlock(frame_handle);
        GlobalFree(frame_handle);
    }
    frame_handle = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, (DWORD)w * h);
    frame_bits = frame_handle ? (BYTE huge *)GlobalLock(frame_handle) : NULL;
    frame_w = w;
    frame_h = h;
    return frame_bits != NULL;
}

/* Draw an 8-bpp frame fitted to the display at its own aspect ratio,
   centred (nearest pixel; each source row is scaled once). */
static void draw_frame(const BITMAPINFOHEADER FAR *bi, const BYTE huge *bits)
{
    int fw = (int)bi->biWidth, fh = (int)bi->biHeight;
    DWORD stride = ((DWORD)fw + 3) & ~3UL;
    int dw, dh, x0, y0, j, last = -1, i;
    const BYTE huge *src_rows;
    DWORD src_stride;

    fit(fw, fh, screen_w, screen_h, &dw, &dh);
    if (dw < 1 || dh < 1) return;
    if (dw != last_dw || dh != last_dh) {
        clear_screen();
        last_dw = dw;
        last_dh = dh;
    }
    x0 = (screen_w - dw) / 2;
    y0 = (screen_h - dh) / 2;
    if (bi->biCompression == BI_RLE8) {
        if (!ensure_frame(fw, fh)) return;
        decode_rle8(bits, bi->biSizeImage ? bi->biSizeImage : 0xFFFFFFUL, fw, fh);
        src_rows = frame_bits;          /* row 0 = DIB bottom row */
        src_stride = fw;
    } else {
        src_rows = bits;
        src_stride = stride;
    }
    for (i = 0; i < dw; ++i) xmap[i] = (WORD)(((long)(2 * i + 1) * fw) / (2L * dw));
    for (j = 0; j < dh; ++j) {
        int s = (int)(((long)(2 * j + 1) * fh) / (2L * dh));
        if (s != last) {
            const BYTE huge *row = src_rows + (DWORD)(fh - 1 - s) * src_stride;
            const BYTE FAR *near_row = (const BYTE FAR *)row;
            /* A row that crosses a 64 KiB boundary is gathered first. */
            if ((DWORD)FP_OFF(near_row) + fw > 0x10000UL) {
                for (i = 0; i < fw; ++i) src_row[i] = row[i];
                near_row = src_row;
            }
            for (i = 0; i < dw; ++i) row_out[i] = near_row[xmap[i]];
            last = s;
        }
        write_row((WORD)(y0 + j), (WORD)x0, row_out, (WORD)dw);
    }
}

/* ---------------------------------------------------------------- exports */

UINT FAR PASCAL DisplayDibEx(LPBITMAPINFOHEADER bi, int x, int y,
                                      LPSTR bits, WORD flags)
{
    char line[120];
    UINT r;
    int implicit = 0;

    if (log_count < 40) {
        wsprintf(line, "DisplayDibEx flags %04X x %d y %d bi %dx%d bpp %d comp %ld bits %s",
                 flags, x, y, bi ? (int)bi->biWidth : 0, bi ? (int)bi->biHeight : 0,
                 bi ? bi->biBitCount : 0, bi ? bi->biCompression : 0L, bits ? "set" : "null");
        log_line(line);
    }
    if (flags & DISPLAYDIB_BEGIN) {
        if (active) return DISPLAYDIB_NOERROR;
        return begin(bi, flags);
    }
    if (flags & DISPLAYDIB_END) {
        end(flags | DISPLAYDIB_NOWAIT);
        return DISPLAYDIB_NOERROR;
    }
    if (bi == NULL) return DISPLAYDIB_INVALIDDIB;
    r = check_format(bi);
    if (r != DISPLAYDIB_NOERROR) return r;
    if (!active) {
        /* A single DisplayDib call: show the DIB, then wait unless NOWAIT. */
        r = begin(bi, (WORD)(flags | DISPLAYDIB_NOWAIT));
        if (r != DISPLAYDIB_NOERROR) return r;
        implicit = 1;
    }
    if (flags & DISPLAYDIB_TEST) return DISPLAYDIB_NOERROR;
    if (bits == NULL) {
        WORD colors = bi->biClrUsed ? (WORD)bi->biClrUsed : 256;
        bits = (LPSTR)bi + bi->biSize + colors * sizeof(RGBQUAD);
    }
    if (!(flags & DISPLAYDIB_NOPALETTE)) set_palette(bi);
    if (!(flags & DISPLAYDIB_NOIMAGE)) draw_frame(bi, (const BYTE huge *)bits);
    if (implicit) end(flags);
    return DISPLAYDIB_NOERROR;
}

UINT FAR PASCAL DisplayDib(LPBITMAPINFOHEADER bi, LPSTR bits, WORD flags)
{
    return DisplayDibEx(bi, 0, 0, bits, flags);
}

int FAR PASCAL LibMain(HINSTANCE instance, WORD data_segment, WORD heap_size,
                       LPSTR command_line)
{
    (void)instance;
    (void)data_segment;
    (void)heap_size;
    (void)command_line;
    return 1;
}

int FAR PASCAL WEP(int reason)
{
    (void)reason;
    if (frame_handle != NULL) {
        GlobalUnlock(frame_handle);
        GlobalFree(frame_handle);
        frame_handle = NULL;
    }
    return 1;
}
