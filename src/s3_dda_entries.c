#include "s3_abi.h"
#include "s3_trace.h"

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
    (void)device;
    (void)x;
    (void)y;
    (void)clip_rect;
    (void)text;
    (void)font_info;
    (void)draw_mode;
    (void)text_xform;
    (void)char_widths;
    (void)opaque_rect;
    S3_TRACE_EVENT("ExtTextOut", count, options);
    return 0;
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
    (void)device;
    (void)buffer;
    (void)font_info;
    (void)draw_mode;
    (void)font_xform;
    S3_TRACE_EVENT("GetCharWidth", first_char, last_char);
    return 0;
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
    (void)bitmap;
    (void)dib_bits;
    (void)bitmap_info;
    (void)draw_mode;
    (void)translate;
    S3_TRACE_EVENT("DeviceBitmapBits", get, scans);
    (void)start;
    return 0;
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
