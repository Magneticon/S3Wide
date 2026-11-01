# First desktop drawn at 1280x720x8 (1 November 2026)

With the general raster engine (`src/s3_raster.c`), USER paints the Windows
desktop across the full 1280x720 guest screen. The capture
([`emulator-window-1280x720.png`](emulator-window-1280x720.png)) is the
DOSBox-X window at the guest resolution: no 4:3 image is stretched.

All 35 BitBlt calls in this boot succeeded
([`s3trace.txt`](s3trace.txt)): 29 SRCCOPY (`00CC`), 3 PATCOPY (`00F0`),
SRCAND (`0088`), SRCINVERT (`0066`) and BLACKNESS (`0000`), on the screen and
on memory bitmaps.

Known defects visible in the capture:

- The minimized Program Manager icon is garbage: icons are drawn from
  bitmaps that GDI fills through `DeviceBitmapBits` (SetDIBits), which is
  still a stub.
- The bottom 32 rows (icon-title area) stay black; text output
  (`ExtTextOut`, 142 calls) is still a stub.
- The grey is the nearest 3:3:2 colour (182,182,170), not Windows' exact
  (192,192,192). A palette that includes the 20 static system colours exactly
  is needed.
- No mouse cursor is drawn yet.

Probe report: [`probe-RESULT.txt`](probe-RESULT.txt) (`HORZRES=1280`,
`VERTRES=720`, `BITSPIXEL=8`; its own GDI readback fails at
`DeviceBitmapBits`, stage 5).
