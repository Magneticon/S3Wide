#include "s3_abi.h"

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
    info->dpHorzSize = 339;
    info->dpVertSize = 191;
    info->dpHorzRes = (S3_I16)S3_WIDTH;
    info->dpVertRes = (S3_I16)S3_HEIGHT;
    info->dpBitsPixel = (S3_I16)S3_BITS_PIXEL;
    info->dpPlanes = (S3_I16)S3_PLANES;
    info->dpNumBrushes = -1; /* effectively unlimited */
    info->dpNumPens = 100;
    info->futureuse = 0;
    info->dpNumFonts = 0;
    info->dpNumColors = 256;
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

    s3_set_point(&info->dpMLoWin, 3390, 1910);
    s3_set_point(&info->dpMLoVpt, 1280, -720);
    /* 33900 x 0.01 mm overflows a signed 16-bit extent; halve both members
       so the metric high-resolution pair keeps the exact low-metric ratio. */
    s3_set_point(&info->dpMHiWin, 16950, 9550);
    s3_set_point(&info->dpMHiVpt, 640, -360);
    s3_set_point(&info->dpELoWin, 8475, 4775);
    s3_set_point(&info->dpELoVpt, 8128, -4572);
    /* Scale both members by 160 to keep the English high-resolution pair in
       signed 16-bit range; the last half-unit is rounded to nearest. */
    s3_set_point(&info->dpEHiWin, 21188, 11938);
    s3_set_point(&info->dpEHiVpt, 2032, -1143);
    s3_set_point(&info->dpTwpWin, 15255, 1910);
    s3_set_point(&info->dpTwpVpt, 1016, -127);
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
    pdev->width = S3_WIDTH;
    pdev->height = S3_HEIGHT;
    pdev->bits_pixel = S3_BITS_PIXEL;
    pdev->planes = S3_PLANES;
    pdev->pitch = S3_PITCH;
    pdev->bank_bytes = S3_BANK_BYTES;
    pdev->framebuffer_bytes = S3_FRAMEBUFFER_BYTES;
    pdev->aperture_linear_address = S3_APERTURE_PHYS;
    pdev->bank_count = (S3_FRAMEBUFFER_BYTES + S3_BANK_BYTES - 1) / S3_BANK_BYTES;
    pdev->surface_state = (void S3_FAR *)0;
    return 1;
}
