#include "s3_abi.h"
#include "s3_mode.h"

static void s3_clear(void S3_FAR *memory, S3_U16 bytes)
{
    S3_U8 S3_FAR *p = (S3_U8 S3_FAR *)memory;
    S3_U16 i;
    for (i = 0; i < bytes; ++i) {
        p[i] = 0;
    }
}

static void s3_set_point(S3_POINT16 S3_FAR *p, S3_I16 x, S3_I16 y)
{
    p->x = x;
    p->y = y;
}

int S3_BuildGDIInfo(S3_GDIINFO S3_FAR *info)
{
    if (info == (S3_GDIINFO S3_FAR *)0) {
        return 0;
    }
    s3_clear(info, (S3_U16)sizeof(*info));

    info->dpVersion = 0x030A;
    info->dpTechnology = 1;  /* DT_RASDISPLAY */
    /* Physical size at 96 pixels per inch, rounded to millimetres. */
    info->dpHorzSize = (S3_I16)(((S3_U32)s3_mode.width * 254UL + 480UL) / 960UL);
    info->dpVertSize = (S3_I16)(((S3_U32)s3_mode.height * 254UL + 480UL) / 960UL);
    info->dpHorzRes = (S3_I16)s3_mode.width;
    info->dpVertRes = (S3_I16)s3_mode.height;
    info->dpBitsPixel = (S3_I16)s3_mode.bits_pixel;
    info->dpPlanes = (S3_I16)S3_PLANES;
    info->dpNumBrushes = -1; /* effectively unlimited */
    info->dpNumPens = 100;
    info->futureuse = 0;
    info->dpNumFonts = 0;
    /* 8 bpp: the fixed 256-entry colour cube. Direct colour: the 20 static
       colours, as other Windows 3.1 true-colour drivers report. */
    info->dpNumColors = s3_mode.bits_pixel == 8 ? 256 : 20;
    info->dpDEVICEsize = (S3_U16)sizeof(S3_PDEV);

    /* The capability set of a stock-class 256-colour raster display, without
       RC_PALETTE: the colours are a fixed 3:3:2 cube. GDI simulates curves,
       polygons and styled or wide lines through polylines and scanlines, and
       realizes raster fonts itself for ExtTextOut. */
    info->dpCurves = 0;                      /* CC_NONE */
    info->dpLines = S3_LC_POLYLINE;
    info->dpPolygonals = S3_PC_SCANLINE;
    info->dpText = S3_TC_CP_STROKE | S3_TC_RA_ABLE;
    info->dpClip = S3_CP_RECTANGLE;
    info->dpRaster = S3_RC_BITBLT | S3_RC_BITMAP64 | S3_RC_GDI20_OUTPUT |
                     S3_RC_DI_BITMAP | S3_RC_DIBTODEV;

    info->dpAspectX = 100;
    info->dpAspectY = 100;
    info->dpAspectXY = 141;
    info->dpStyleLen = 100;

    /* Mapping-mode extents are ratios of logical units to pixels at 96
       pixels per inch, reduced so every mode stays in signed 16-bit range:
       0.1 mm = 127/48, 0.01 mm = 635/24, 0.01 in = 25/24, 0.001 in =
       125/12 and twips = 15/1 per pixel. */
    s3_set_point(&info->dpMLoWin, 127, 127);
    s3_set_point(&info->dpMLoVpt, 48, -48);
    s3_set_point(&info->dpMHiWin, 635, 635);
    s3_set_point(&info->dpMHiVpt, 24, -24);
    s3_set_point(&info->dpELoWin, 25, 25);
    s3_set_point(&info->dpELoVpt, 24, -24);
    s3_set_point(&info->dpEHiWin, 125, 125);
    s3_set_point(&info->dpEHiVpt, 12, -12);
    s3_set_point(&info->dpTwpWin, 15, 15);
    s3_set_point(&info->dpTwpVpt, 1, -1);
    info->dpLogPixelsX = 96;
    info->dpLogPixelsY = 96;
    info->dpDCManage = 0x0004; /* DC_IGNOREDFNP: one shared display device */
    info->dpCaps1 = 0;
    info->dpSpotSizeX = 0;
    info->dpSpotSizeY = 0;
    info->dpPalColors = 0;
    info->dpPalReserved = 0;
    info->dpPalResolution = 0;
    return 1;
}

int S3_InitializePDEV(S3_PDEV S3_FAR *pdev)
{
    if (pdev == (S3_PDEV S3_FAR *)0) {
        return 0;
    }
    pdev->pdType = 1; /* nonzero distinguishes a display device from PBITMAP */
    pdev->width = s3_mode.width;
    pdev->height = s3_mode.height;
    pdev->bits_pixel = s3_mode.bits_pixel;
    pdev->planes = S3_PLANES;
    pdev->pitch = s3_mode.pitch;
    pdev->bank_bytes = S3_BANK_BYTES;
    pdev->framebuffer_bytes = s3_mode.framebuffer_bytes;
    pdev->aperture_linear_address = s3_mode.backend == S3_BACKEND_VBE ?
                                    s3_mode.lfb_physical : S3_APERTURE_PHYS;
    pdev->bank_count = (s3_mode.framebuffer_bytes + S3_BANK_BYTES - 1) / S3_BANK_BYTES;
    pdev->surface_state = (void S3_FAR *)0;
    return 1;
}
