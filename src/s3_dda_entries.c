#include "s3_abi.h"
#include "s3_trace.h"
#include "s3_dib.h"
#include "s3_text.h"
#include "s3_raster.h"

/* Remaining DDA entry points of the WfW 3.11 stock display-driver export
   table. Signatures follow the Windows 98 DDK reference for the same
   functions (all are __far __pascal, so the callee pops the arguments and
   the parameter list must match exactly). These are bring-up stubs: each
   reports "nothing done" in the documented way so GDI and USER can continue
   while device Enable is completed. */

#define S3_QUERYESCSUPPORT 8

/* CURSORINFO, returned by Inquire. */
typedef struct S3_CURSORINFO16 {
    S3_I16 dpXRate;
    S3_I16 dpYRate;
} S3_CURSORINFO16;

S3_U16 S3_FAR S3_PASCAL Control(void S3_FAR *device, S3_U16 function,
                                void S3_FAR *in_data, void S3_FAR *out_data)
{
    (void)device;
    (void)in_data;
    (void)out_data;
    S3_TRACE_EVENT("Control", function, 0);
    /* No escapes are supported yet, including QUERYESCSUPPORT itself. */
    return 0;
}

S3_U16 S3_FAR S3_PASCAL EnumDFonts(void S3_FAR *device, char S3_FAR *face_name,
                                   void S3_FAR *callback, void S3_FAR *client_data)
{
    (void)device;
    (void)face_name;
    (void)callback;
    (void)client_data;
    S3_TRACE_EVENT("EnumDFonts", 0, 0);
    /* No device fonts: nothing to enumerate, which is not an error. */
    return 1;
}

S3_U16 S3_FAR S3_PASCAL EnumObj(void S3_FAR *device, S3_U16 style,
                                void S3_FAR *callback, void S3_FAR *client_data)
{
    (void)device;
    (void)callback;
    (void)client_data;
    S3_TRACE_EVENT("EnumObj", style, 0);
    return 1;
}

S3_U32 S3_FAR S3_PASCAL ExtTextOut(void S3_FAR *device, S3_I16 x, S3_I16 y,
                                   void S3_FAR *clip_rect, char S3_FAR *text,
                                   S3_I16 count, void S3_FAR *font_info,
                                   void S3_FAR *draw_mode, void S3_FAR *text_xform,
                                   S3_I16 S3_FAR *char_widths,
                                   void S3_FAR *opaque_rect, S3_U16 options)
{
    S3_U32 result;
    (void)text_xform;
    S3_TRACE_EVENT("ExtTextOut.enter", count, options);
#if defined(S3_TRACE)
    {
        /* Log the characters (two per field) so fault dialogs are readable. */
        S3_I16 i, n = (S3_I16)(count < 0 ? -count : count);
        for (i = 0; i + 3 < n && i < 64; i = (S3_I16)(i + 4)) {
            S3_TRACE_EVENT("ExtTextOut.text",
                           ((S3_U16)(S3_U8)text[i] << 8) | (S3_U8)text[i + 1],
                           ((S3_U16)(S3_U8)text[i + 2] << 8) | (S3_U8)text[i + 3]);
        }
    }
#endif
    result = S3_TextOut(device, x, y, (const S3_RECT16 S3_FAR *)clip_rect,
                        (const S3_U8 S3_FAR *)text, count, font_info,
                        (S3_DRAWMODE16 S3_FAR *)draw_mode, char_widths,
                        (const S3_RECT16 S3_FAR *)opaque_rect, options);
    if (result == 0) S3_TRACE_EVENT("ExtTextOut.fail", count, options);
    return result;
}

S3_U32 S3_FAR S3_PASCAL StrBlt(void S3_FAR *device, S3_I16 x, S3_I16 y,
                               void S3_FAR *clip_rect, char S3_FAR *text,
                               S3_I16 count, void S3_FAR *font_info,
                               void S3_FAR *draw_mode, void S3_FAR *text_xform)
{
    /* StrBlt is ExtTextOut without widths, opaque rectangle or options. */
    return ExtTextOut(device, x, y, clip_rect, text, count, font_info,
                      draw_mode, text_xform, (S3_I16 S3_FAR *)0,
                      (void S3_FAR *)0, 0);
}

S3_U16 S3_FAR S3_PASCAL ScanLR(void S3_FAR *device, S3_I16 x, S3_I16 y,
                               S3_U32 physical_color, S3_U16 style)
{
    (void)device;
    (void)physical_color;
    S3_TRACE_EVENT("ScanLR", x, style);
    (void)y;
    return 0xFFFFU; /* -1: error */
}

S3_I16 S3_FAR S3_PASCAL DeviceMode(S3_U16 window, S3_U16 instance,
                                   char S3_FAR *device_name,
                                   char S3_FAR *output)
{
    (void)window;
    (void)instance;
    (void)device_name;
    (void)output;
    S3_TRACE_EVENT("DeviceMode", 0, 0);
    return 0;
}

S3_U16 S3_FAR S3_PASCAL GetCharWidth(void S3_FAR *device,
                                     S3_I16 S3_FAR *buffer,
                                     S3_U16 first_char, S3_U16 last_char,
                                     void S3_FAR *font_info,
                                     void S3_FAR *draw_mode,
                                     void S3_FAR *font_xform)
{
    int ok;
    (void)device;
    (void)draw_mode;
    (void)font_xform;
    ok = S3_TextCharWidths(font_info, first_char, last_char, buffer);
    if (!ok) S3_TRACE_EVENT("GetCharWidth.fail", first_char, last_char);
    return (S3_U16)ok;
}

/* Clip one border strip and fill it with the brush and ROP3. */
static int s3_border_strip(void S3_FAR *device, S3_I16 left, S3_I16 top,
                           S3_I16 right, S3_I16 bottom, S3_U32 rop,
                           const S3_PBRUSH16 S3_FAR *brush,
                           const S3_DRAWMODE16 S3_FAR *mode,
                           const S3_RECT16 S3_FAR *clip)
{
    if (clip != (const S3_RECT16 S3_FAR *)0) {
        if (clip->left > left) left = clip->left;
        if (clip->top > top) top = clip->top;
        if (clip->right < right) right = clip->right;
        if (clip->bottom < bottom) bottom = clip->bottom;
    }
    if (left >= right || top >= bottom) return 1;
    return S3_RasterBitBlt(device, left, top, (void S3_FAR *)0, 0, 0,
                           (S3_I16)(right - left), (S3_I16)(bottom - top),
                           rop, brush, mode);
}

/* FastBorder (ordinal 17): GDI's FastWindowFrame calls it through the
   driver table whenever dpRaster has RC_GDI20_OUTPUT, with this exact
   Pascal argument order (28 bytes). Draws the frame of hBorder-wide
   vertical and vBorder-high horizontal strips inside rect. */
S3_U16 S3_FAR S3_PASCAL FastBorder(S3_RECT16 S3_FAR *rect, S3_U16 h_border,
                                   S3_U16 v_border, S3_U32 rop,
                                   void S3_FAR *device, void S3_FAR *brush,
                                   void S3_FAR *draw_mode,
                                   S3_RECT16 S3_FAR *clip)
{
    const S3_PBRUSH16 S3_FAR *pbrush = (const S3_PBRUSH16 S3_FAR *)brush;
    const S3_DRAWMODE16 S3_FAR *mode = (const S3_DRAWMODE16 S3_FAR *)draw_mode;
    S3_I16 l, t, r, b, hw, vw;
    int ok;

    if (rect == (S3_RECT16 S3_FAR *)0) return 0;
    l = rect->left;
    t = rect->top;
    r = rect->right;
    b = rect->bottom;
    hw = (S3_I16)h_border;
    vw = (S3_I16)v_border;
    S3_TRACE_EVENT("FastBorder", (S3_U16)hw, (S3_U16)vw);
    ok = s3_border_strip(device, l, t, r, (S3_I16)(t + vw), rop, pbrush, mode, clip) &&
         s3_border_strip(device, l, (S3_I16)(b - vw), r, b, rop, pbrush, mode, clip) &&
         s3_border_strip(device, l, (S3_I16)(t + vw), (S3_I16)(l + hw),
                         (S3_I16)(b - vw), rop, pbrush, mode, clip) &&
         s3_border_strip(device, (S3_I16)(r - hw), (S3_I16)(t + vw), r,
                         (S3_I16)(b - vw), rop, pbrush, mode, clip);
    return (S3_U16)(ok ? 1 : 0);
}

S3_U16 S3_FAR S3_PASCAL DeviceBitmap(void S3_FAR *device, S3_U16 command,
                                     void S3_FAR *bitmap, char S3_FAR *bits)
{
    (void)device;
    (void)bitmap;
    (void)bits;
    S3_TRACE_EVENT("DeviceBitmap", command, 0);
    return 0;
}

S3_U16 S3_FAR S3_PASCAL DeviceBitmapBits(void S3_FAR *bitmap, S3_U16 get,
                                         S3_U16 start, S3_U16 scans,
                                         char S3_FAR *dib_bits,
                                         void S3_FAR *bitmap_info,
                                         void S3_FAR *draw_mode,
                                         S3_I16 S3_FAR *translate)
{
    S3_U16 copied;
    (void)draw_mode;
    S3_TRACE_EVENT("DeviceBitmapBits.enter", get, scans);
    (void)translate; /* only supplied for palette devices */
    copied = S3_DibBitmapBits(bitmap, get, start, scans, (S3_U8 S3_FAR *)dib_bits,
                              (S3_BITMAPINFO16 S3_FAR *)bitmap_info);
    S3_TRACE_EVENT(copied ? "DeviceBitmapBits" : "DeviceBitmapBits.fail",
                   get, scans);
    return copied;
}

S3_U16 S3_FAR S3_PASCAL Inquire(S3_CURSORINFO16 S3_FAR *cursor_info)
{
    S3_TRACE_EVENT("Inquire", 0, 0);
    if (cursor_info != (S3_CURSORINFO16 S3_FAR *)0) {
        /* Mickey-to-pixel ratios for the mouse driver. */
        cursor_info->dpXRate = 1;
        cursor_info->dpYRate = 1;
    }
    return (S3_U16)sizeof(S3_CURSORINFO16);
}

S3_U16 S3_FAR S3_PASCAL SetCursor(void S3_FAR *cursor_shape)
{
    S3_TRACE_EVENT("SetCursor", cursor_shape == (void S3_FAR *)0, 0);
    return 1;
}

/* MoveCursor and CheckCursor run at mouse and timer interrupt time: they
   must not trace (file I/O would re-enter DOS). No cursor is drawn yet. */
void S3_FAR S3_PASCAL MoveCursor(S3_I16 x, S3_I16 y)
{
    (void)x;
    (void)y;
}

void S3_FAR S3_PASCAL CheckCursor(void)
{
}

void S3_FAR S3_PASCAL UserRepaintDisable(S3_U16 disable)
{
    S3_TRACE_EVENT("UserRepaintDisable", disable, 0);
}
