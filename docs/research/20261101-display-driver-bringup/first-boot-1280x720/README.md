# First Windows boot at 1280x720x8 (1 November 2026)

Windows for Workgroups 3.11 in Enhanced Mode completed startup with
`display.drv=s3wide.drv` and ran at a genuine 1280x720, 8 bpp guest mode on
DOSBox-X `svga_s3trio64`. **Nothing is drawn yet: the screen is black.** This
is the first boot milestone, not GDI acceptance.

## Evidence

| Field | Value |
|---|---|
| Driver build | local `VGA_DRV=… S3_TRACE=1 ./build.sh` (imported stock resources, trace on) |
| DOSBox-X | 2026.08.31, `svga_s3trio64`, 4 MiB VRAM ([`baseline.conf`](baseline.conf)) |
| Windows | WfW 3.11 Enhanced Mode, `GetWinFlags=0x0C29` |
| Reported mode | `HORZRES=1280 VERTRES=720 BITSPIXEL=8 PLANES=1 NUMCOLORS=256 RASTERCAPS=0x0299`, `SM_CXSCREEN/SM_CYSCREEN=1280/720` ([`probe-RESULT.txt`](probe-RESULT.txt)) |
| Emulator window | 1280x720 client area, captured from the host ([`emulator-window-1280x720.png`](emulator-window-1280x720.png)) |
| Boot | `[boot]` log through `progman.exe`, `Final USER`, installable drivers ([`bootlog.txt`](bootlog.txt)) |
| Shutdown | `Disable` restored the saved VGA/S3 state (`hw.restore.ok`) and DOSBox-X exited 0 |

The capture is the emulator's own output at the guest's resolution, so there
is no 4:3-to-16:9 scaling. It is black because every drawing call still
declines: in [`s3trace.txt`](s3trace.txt) USER and GDI issue 35 `BitBlt`,
142 `ExtTextOut` and 108 `DeviceBitmapBits` calls, which return failure.

## What made it boot

1. **Resources.** Local builds import the cursors, icons, OEM bitmaps and
   `OEMBIN` tables of the user's licensed `VGA.DRV`
   (`tools/import_vga_resources.py`, `VGA_DRV=`). The result is never
   committed; a release needs original resources. Ordinal 450
   (`GetDriverResourceID`) is no longer exported, matching `VGA.DRV`.
2. **Export set.** USER needs the cursor functions; `src/s3_dda_entries.c`
   adds Control, EnumDFonts, EnumObj, StrBlt, ScanLR, DeviceMode,
   ExtTextOut, GetCharWidth, DeviceBitmap, DeviceBitmapBits, Inquire,
   SetCursor, MoveCursor, CheckCursor and UserRepaintDisable at the stock
   ordinals, with Pascal signatures from the Windows 98 DDK reference. They
   are traced stubs; MoveCursor and CheckCursor run at interrupt time and do
   not trace.
3. **Device Enable.** `src/s3_win16_hw.c` drives the existing selector
   bridge with direct port I/O: it saves the S3 timing snapshot plus
   SEQ/GC/AC/DAC state, applies the 1280x720x8 sequence proven by
   `tests/dosbox-x/raw_s3_cold_smoke.c`, loads the 3:3:2 palette, and
   restores everything on Disable. *VDDVGA did not block the driver's port
   I/O in this emulator.*
4. **16-bit framebuffer size bug.** `S3_FRAMEBUFFER_BYTES` was
   `1280U * 720U`, which wraps to 4096 in the 16-bit build, so the surface
   rejected the aperture. It is now a 32-bit product with a compile-time
   check that runs in the Watcom build.
5. **Brushes and capabilities.** GDI's CreateDC realizes a brush and checks
   the GDIINFO capabilities. `src/s3_brush.c` realizes solid, hollow, hatched
   and 8x8 pattern brushes (host tested), and GDIINFO now advertises the
   stock-class set (`LC_POLYLINE`, `PC_SCANLINE`, `TC_RA_ABLE|TC_CP_STROKE`,
   `CP_RECTANGLE`, `RC_BITBLT|RC_BITMAP64|RC_GDI20_OUTPUT|RC_DI_BITMAP|RC_DIBTODEV`).

## Next

Implement the drawing those capabilities promise: BitBlt raster operations
with brushes on the screen and on memory bitmaps, Output scanlines and
polylines with clipping, ExtTextOut for GDI raster fonts, DeviceBitmapBits,
and a cursor.
