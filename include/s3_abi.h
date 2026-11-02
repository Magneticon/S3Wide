#ifndef S3_ABI_H
#define S3_ABI_H

#include <stddef.h>

/* Independently authored Win16 DDI declarations from the public DDA contract. */
#if defined(S3_HOST_TEST)
#include <stdint.h>
typedef uint8_t  S3_U8;
typedef uint16_t S3_U16;
typedef int16_t  S3_I16;
typedef uint32_t S3_U32;
typedef int32_t  S3_I32;
#define S3_FAR
#define S3_PASCAL
#else
typedef unsigned char  S3_U8;
typedef unsigned short S3_U16;
typedef short          S3_I16;
typedef unsigned long  S3_U32;
typedef long           S3_I32;
#define S3_FAR __far
#define S3_PASCAL __pascal
#endif

typedef struct S3_POINT16 {
    S3_I16 x;
    S3_I16 y;
} S3_POINT16;

/* Windows 3.1 logical pen passed to RealizeObject(OBJ_PEN). */
#pragma pack(push, 2)
typedef struct S3_LPEN16 {
    S3_U16 lopnStyle;
    S3_POINT16 lopnWidth;
    S3_U32 lopnColor;
} S3_LPEN16;

/* Driver-owned physical pen format returned to GDI by RealizeObject. */
typedef struct S3_PPEN16 {
    S3_U32 style;
    S3_POINT16 width;
    S3_U32 color;
} S3_PPEN16;
#pragma pack(pop)

typedef struct S3_RECT16 {
    S3_I16 left;
    S3_I16 top;
    S3_I16 right;
    S3_I16 bottom;
} S3_RECT16;

/* Windows 3.1 BITMAP/PBITMAP fields used by the memory-source BitBlt path. */
#pragma pack(push, 2)
typedef struct S3_BITMAP16 {
    S3_I16 bmType;
    S3_I16 bmWidth;
    S3_I16 bmHeight;
    S3_I16 bmWidthBytes;
    S3_U8 bmPlanes;
    S3_U8 bmBitsPixel;
    S3_U8 S3_FAR *bmBits;
    S3_U32 bmWidthPlanes;
    S3_U32 bmlpPDevice;
    S3_I16 bmSegmentIndex;
    S3_I16 bmScanSegment;
    S3_I16 bmFillBytes;
    S3_I16 reserved1;
    S3_I16 reserved2;
} S3_BITMAP16;

/* Pixel only consumes Rop2; the remaining fields preserve documented layout. */
typedef struct S3_DRAWMODE16 {
    S3_I16 Rop2;
    S3_I16 bkMode;
    S3_U32 bkColor;
    S3_U32 TextColor;
    S3_I16 TBreakExtra;
    S3_I16 BreakExtra;
    S3_I16 BreakErr;
    S3_I16 BreakRem;
    S3_I16 BreakCount;
    S3_I16 CharExtra;
    S3_U32 LbkColor;
    S3_U32 LTextColor;
} S3_DRAWMODE16;

/* Windows 3.x BITMAPINFO structures used by the direct DIB DDA. */
typedef struct S3_BITMAPINFOHEADER16 {
    S3_U32 biSize;
    S3_U32 biWidth;
    S3_U32 biHeight;
    S3_U16 biPlanes;
    S3_U16 biBitCount;
    S3_U32 biCompression;
    S3_U32 biSizeImage;
    S3_U32 biXPelsPerMeter;
    S3_U32 biYPelsPerMeter;
    S3_U32 biClrUsed;
    S3_U32 biClrImportant;
} S3_BITMAPINFOHEADER16;

typedef struct S3_RGBQUAD16 {
    S3_U8 rgbBlue;
    S3_U8 rgbGreen;
    S3_U8 rgbRed;
    S3_U8 rgbReserved;
} S3_RGBQUAD16;

typedef struct S3_BITMAPINFO16 {
    S3_BITMAPINFOHEADER16 bmiHeader;
    S3_RGBQUAD16 bmiColors[1];
} S3_BITMAPINFO16;
#pragma pack(pop)

/* Win16 Windows 3.1 DDA GDIINFO: 55 16-bit words, exactly 110 bytes. */
#pragma pack(push, 2)
typedef struct S3_GDIINFO {
    S3_I16 dpVersion;
    S3_I16 dpTechnology;
    S3_I16 dpHorzSize;
    S3_I16 dpVertSize;
    S3_I16 dpHorzRes;
    S3_I16 dpVertRes;
    S3_I16 dpBitsPixel;
    S3_I16 dpPlanes;
    S3_I16 dpNumBrushes;
    S3_I16 dpNumPens;
    S3_I16 futureuse;
    S3_I16 dpNumFonts;
    S3_I16 dpNumColors;
    S3_U16 dpDEVICEsize;
    S3_U16 dpCurves;
    S3_U16 dpLines;
    S3_U16 dpPolygonals;
    S3_U16 dpText;
    S3_U16 dpClip;
    S3_U16 dpRaster;
    S3_I16 dpAspectX;
    S3_I16 dpAspectY;
    S3_I16 dpAspectXY;
    S3_I16 dpStyleLen;
    S3_POINT16 dpMLoWin;
    S3_POINT16 dpMLoVpt;
    S3_POINT16 dpMHiWin;
    S3_POINT16 dpMHiVpt;
    S3_POINT16 dpELoWin;
    S3_POINT16 dpELoVpt;
    S3_POINT16 dpEHiWin;
    S3_POINT16 dpEHiVpt;
    S3_POINT16 dpTwpWin;
    S3_POINT16 dpTwpVpt;
    S3_I16 dpLogPixelsX;
    S3_I16 dpLogPixelsY;
    S3_I16 dpDCManage;
    S3_I16 dpCaps1;
    S3_I32 dpSpotSizeX;
    S3_I32 dpSpotSizeY;
    S3_I16 dpPalColors;
    S3_I16 dpPalReserved;
    S3_I16 dpPalResolution;
} S3_GDIINFO;

/* PDEVICE is driver-defined after its required first pdType member. */
typedef struct S3_PDEV {
    S3_I16 pdType;
    S3_U16 width;
    S3_U16 height;
    S3_U16 bits_pixel;
    S3_U16 planes;
    S3_U16 pitch;
    S3_U32 bank_bytes;
    S3_U32 framebuffer_bytes;
    S3_U32 aperture_linear_address;
    S3_U32 bank_count;
    void S3_FAR *surface_state;
} S3_PDEV;
#pragma pack(pop)

typedef char S3_ASSERT_GDIINFO_110[(sizeof(S3_GDIINFO) == 110) ? 1 : -1];
#if defined(S3_HOST_TEST)
typedef char S3_ASSERT_PDEV_HOST_36[(sizeof(S3_PDEV) == 36) ? 1 : -1];
typedef char S3_ASSERT_BITMAP_HOST_BITS_10[(offsetof(S3_BITMAP16, bmBits) == 10) ? 1 : -1];
typedef char S3_ASSERT_PBITMAP_HOST_36[(sizeof(S3_BITMAP16) == 36) ? 1 : -1];
#else
typedef char S3_ASSERT_PDEV_32[(sizeof(S3_PDEV) == 32) ? 1 : -1];
typedef char S3_ASSERT_PBITMAP_32[(sizeof(S3_BITMAP16) == 32) ? 1 : -1];
typedef char S3_ASSERT_PBITMAP_BITS_10[(offsetof(S3_BITMAP16, bmBits) == 10) ? 1 : -1];
#endif
typedef char S3_ASSERT_DRAWMODE_32[(sizeof(S3_DRAWMODE16) == 32) ? 1 : -1];
typedef char S3_ASSERT_DRAWMODE_ROP2_0[(offsetof(S3_DRAWMODE16, Rop2) == 0) ? 1 : -1];
typedef char S3_ASSERT_DRAWMODE_BKCOLOR_4[(offsetof(S3_DRAWMODE16, bkColor) == 4) ? 1 : -1];
typedef char S3_ASSERT_BITMAPINFOHEADER_40[(sizeof(S3_BITMAPINFOHEADER16) == 40) ? 1 : -1];
typedef char S3_ASSERT_RGBQUAD_4[(sizeof(S3_RGBQUAD16) == 4) ? 1 : -1];
typedef char S3_ASSERT_BITMAPINFO_COLORS_40[(offsetof(S3_BITMAPINFO16, bmiColors) == 40) ? 1 : -1];
typedef char S3_ASSERT_POINT_4[(sizeof(S3_POINT16) == 4) ? 1 : -1];
typedef char S3_ASSERT_LPEN_10[(sizeof(S3_LPEN16) == 10) ? 1 : -1];
typedef char S3_ASSERT_LPEN_STYLE_0[(offsetof(S3_LPEN16, lopnStyle) == 0) ? 1 : -1];
typedef char S3_ASSERT_LPEN_WIDTH_2[(offsetof(S3_LPEN16, lopnWidth) == 2) ? 1 : -1];
typedef char S3_ASSERT_LPEN_COLOR_6[(offsetof(S3_LPEN16, lopnColor) == 6) ? 1 : -1];
typedef char S3_ASSERT_PPEN_12[(sizeof(S3_PPEN16) == 12) ? 1 : -1];
typedef char S3_ASSERT_RECT_8[(sizeof(S3_RECT16) == 8) ? 1 : -1];

/* Widest row any drawing path handles, and its size at 4 bytes per pixel.
   Row buffers that size live in their own far data segments (S3_FAR_DATA)
   so DGROUP stays below 64 KiB. */
#define S3_ROW_PIXELS_MAX   4096
#define S3_ROW_BYTES_MAX    16384U
#if defined(S3_HOST_TEST)
#define S3_FAR_DATA
#else
#define S3_FAR_DATA __far
#endif

#define S3_WIDTH            1280U
#define S3_HEIGHT           720U
#define S3_BITS_PIXEL       8U
#define S3_PLANES           1U
#define S3_PITCH            1280U
#define S3_BANK_BYTES       65536UL
/* 32-bit product: in the 16-bit driver build 1280U * 720U wraps to 4096. */
#define S3_FRAMEBUFFER_BYTES ((unsigned long)S3_PITCH * (unsigned long)S3_HEIGHT)
#define S3_APERTURE_PHYS    0x000A0000UL

/* Checked by every compiler, so the 16-bit Watcom build catches overflow
   that the 32-bit host tests cannot see. */
typedef char S3_ASSERT_FRAMEBUFFER_BYTES[(S3_FRAMEBUFFER_BYTES == 921600UL) ? 1 : -1];

#define S3_ENABLE_DEVICE    0x0000U
#define S3_INQUIRE_INFO     0x0001U
#define S3_INFO_CONTEXT     0x8000U
#define S3_PIXEL_ERROR      0x80000000UL
#define S3_ROP3_SRCCOPY     0x00CC0020UL
#define S3_R2_COPYPEN       13
#define S3_PDEV_DISPLAY     1
#define S3_OBJ_PEN          1U
#define S3_LS_SOLID         0UL
#define S3_LS_NOLINE        5UL
#define S3_OS_POLYLINE      18U

/* GDIINFO capability bits (Windows 3.1 DDK values). */
#define S3_LC_POLYLINE      0x0002
#define S3_PC_SCANLINE      0x0008
#define S3_TC_CP_STROKE     0x0004
#define S3_TC_RA_ABLE       0x2000
#define S3_CP_RECTANGLE     0x0001
#define S3_RC_BITBLT        0x0001
#define S3_RC_BITMAP64      0x0008
#define S3_RC_GDI20_OUTPUT  0x0010
#define S3_RC_DI_BITMAP     0x0080
#define S3_RC_DIBTODEV      0x0200
#define S3_DDA_NOT_SUPPORTED 0xFFFFU

int S3_BuildGDIInfo(S3_GDIINFO S3_FAR *info);
int S3_InitializePDEV(S3_PDEV S3_FAR *pdev);

S3_U32 S3_ColorInfoCore(S3_U32 color_in, S3_U32 S3_FAR *physical_out);
int S3_ColorIndexToRGB(S3_U8 index, S3_U32 S3_FAR *rgb_out);
int S3_RGBToColorIndex(S3_U32 rgb, S3_U8 S3_FAR *index_out);

int S3_FAR S3_PASCAL Enable(void S3_FAR *device, S3_I16 style,
                            char S3_FAR *device_type,
                            char S3_FAR *output_file,
                            char S3_FAR *extra_info);
int S3_FAR S3_PASCAL Disable(void S3_FAR *device);
S3_U32 S3_FAR S3_PASCAL ColorInfo(S3_PDEV S3_FAR *device,
                                  S3_U32 color_in,
                                  S3_U32 S3_FAR *physical_out);
int S3_FAR S3_PASCAL GetDriverResourceID(int resource_id,
                                         char S3_FAR *resource_type);

/* Windows 3.1 DDA entry contracts. BitBlt supports only 8-bpp bitmap SRCCOPY;
   Output supports only solid one-pixel OS_POLYLINE with R2_COPYPEN; Pixel
   supports retrieval and R2_COPYPEN writes. Surface calls fail without an
   attached software surface. */
S3_I16 S3_FAR S3_PASCAL BitBlt(void S3_FAR *destination,
                               S3_U16 destination_x, S3_U16 destination_y,
                               void S3_FAR *source,
                               S3_U16 source_x, S3_U16 source_y,
                               S3_U16 width, S3_U16 height,
                               S3_I32 rop3, void S3_FAR *physical_brush,
                               void S3_FAR *draw_mode);
S3_U16 S3_FAR S3_PASCAL Output(void S3_FAR *destination,
                               S3_U16 style, S3_U16 point_count,
                               S3_POINT16 S3_FAR *points,
                               void S3_FAR *physical_pen,
                               void S3_FAR *physical_brush,
                               void S3_FAR *draw_mode,
                               S3_RECT16 S3_FAR *clip_rect);
S3_U32 S3_FAR S3_PASCAL RealizeObject(S3_PDEV S3_FAR *device,
                                      S3_U16 style,
                                      void S3_FAR *logical_object,
                                      void S3_FAR *physical_object,
                                      void S3_FAR *text_xform);
S3_U32 S3_FAR S3_PASCAL Pixel(void S3_FAR *destination,
                               S3_U16 x, S3_U16 y, S3_U32 physical_color,
                               void S3_FAR *draw_mode);
S3_U16 S3_FAR S3_PASCAL SetDIBitsToDevice(
                               void S3_FAR *destination,
                               S3_U16 x, S3_U16 y,
                               S3_U16 scan_start, S3_U16 scan_count,
                               S3_RECT16 S3_FAR *clip_rect,
                               S3_DRAWMODE16 S3_FAR *draw_mode,
                               S3_U8 S3_FAR *dib_bits,
                               S3_BITMAPINFO16 S3_FAR *bitmap_info,
                               S3_I16 S3_FAR *translate);

#endif
