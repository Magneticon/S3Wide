# RLE bitmaps, review fixes and screen switching (2 November 2026)

Work done before handing a build to the owner for testing on a live Windows
3.11 machine with multimedia software.

## Compressed bitmaps (BI_RLE8, BI_RLE4)

`s3_dib.c` decodes run-length DIBs for SetDIBitsToDevice and for
DeviceBitmapBits (SetDIBits into a bitmap): encoded and absolute runs, word
padding, deltas, end of scan and end of bitmap. Pixels the stream skips
(deltas, scans or bitmap ended early, truncated data) are left unchanged,
as Windows does. The stream is read from the start of the image on every
call, bounded by `biSizeImage`, through the huge-pointer segment walk.
GetDIBits into RLE is not offered. RLE DIBs may be up to 4096 pixels wide.
Before this the driver refused RLE DIBs and GDI drew nothing.

Evidence: host tests (`test_rle_dibs`) cover every record type, clipping,
scan ranges and a truncated stream. In the guest, `DISPRLE.EXE`
(`tests/win16/display_rle_probe.c`) draws the same picture uncompressed and
as RLE8/RLE4 through SetDIBitsToDevice, StretchDIBits (1:1 and 2x) and
SetDIBits, then compares the screen pixels with GetPixel. Every case matched
exactly at 8 bpp (S3 path, and VESA 1024x768), 16 bpp and 32 bpp (VESA
1280x720). The previous driver failed every case (GDI returned 0).

## Independent review

A separate reviewer read the driver at 126eab5 without my notes. Its
findings and what was done:

| Finding | Fix |
|---|---|
| The S3 mode is programmed without unlocking CR39. Real Trio64 boards gate CR40-CR6F behind it; DOSBox-X does not | CR39 = A5h from capture until Disable, which restores the original value |
| Any VBE OEM string starting "S3" took the Trio64 register path | The native path also requires chip ID CR2D/CR2E = 88h/11h (Trio64). Other S3 chips use VESA |
| Interrupt-time code and data (MoveCursor, CheckCursor, cursor state) were in MOVEABLE segments | All segments FIXED (`driver_smoke.lnk`) |
| Monochrome pattern brushes were always black/white | 0 bits draw in the DC's text colour, 1 bits in its background colour, as Windows specifies |
| Chunk counters wrapped for memory bitmaps wider than 28672 pixels and overran the row buffers | 32-bit counters |
| Copies within one bitmap wider than 4096 pixels, moving right, read already-overwritten source | Chunks run right to left in that case |
| DIBs wider than 4096 pixels (24-bpp wider than 5461) failed | Converted in 4096-pixel pieces. SetDIBitsToDevice reads only the visible bytes, including a start inside a 1/4-bpp byte |
| The S3 path did not reload the text font on exit | Disable sets the saved BIOS mode again (INT 10h), which reloads it |
| ColorInfo at 8 bpp left the physical colour unwritten for PALETTERGB/PALETTEINDEX values | Flag byte stripped; the colour is matched as RGB |

Host tests were added for the mono brush colours, wide DIBs in both
directions with a sub-byte start, and the right-to-left overlapping copy.

## Mode fallback

On a card without the requested VESA mode, Windows used to fail to start.
`S3_VbeModeScore` now picks, failing an exact match, the largest drawable
mode no larger than the request: the requested depth first, then the
larger area, then the deeper format. On a Trio64 the native 1280x720x8
remains the fallback. Checked in the guest: a 1300x750x32 request started
at 1280x720x32. `VBEMODES.EXE` (`tests/dosbox-x/vbe_modes.c`) now marks
each mode `s3wide=yes/no` with the same rules.

## Full-screen DOS sessions

Found while preparing the release: after a full-screen DOS session the
screen came back black (VESA) or scrambled (S3). Windows kept running, and
the driver's own readback still showed the desktop. The VGA virtual device
cannot restore modes it does not know.

`s3_switch.c` implements the Windows display-driver switch protocol:

- At Enable it hooks INT 2Fh and calls AX=4000h, so the driver saves and
  restores the screen itself.
- AX=4001h (a DOS session takes the screen): drawing on the display is
  skipped and the software cursor is held.
- AX=4002h (Windows is back): the mode is set again (VBE 4F02 and palette,
  or the S3 registers), banks are remapped, the cursor is reloaded, then
  USER's RepaintScreen is called, or deferred while USER has repainting
  disabled through UserRepaintDisable.
- At Disable it calls AX=4007h and unhooks INT 2Fh.

`DISPBASE.EXE dosrun` (runner `DOSRUN=1`) runs `COMMAND.COM /C VER` full
screen and then captures. With the fix, the emulator screen equals the GDI
readback except the caret and the I-beam, on both paths. A trace build
showed the switch: `switch.resume 0001` after the mode was set again.

## Release build

Driver SHA-256 `8cb7ad736070b18c0a7b5f84cb131b7bcba3880af29b41a2d86d195dcadeb7e2`
(local build with VGA.DRV resources; not committed). DOSBox-X 2026.08.31,
WfW 3.11 Enhanced Mode, disposable guests, source guest unchanged after
every run:

| Mode | Machine | Result |
|---|---|---|
| 1280x720x8 (S3 path) | svga_s3trio64 | Notepad demo and a full-screen DOS session: capture = readback except the 57 caret/I-beam pixels |
| 1280x720x32 | svga_dosbox | Same check; its readback equals the S3 run's readback exactly |
| 1280x720x32, 1024x768x8 | svga_dosbox | DISPRLE.EXE: all 16 RLE cases 0 mismatches |
| 1920x1080x16 | svga_dosbox | Full GDI readback, desktop correct |
| 3840x2160x32 | svga_dosbox | Full GDI readback, desktop correct |

The package (driver, VBEMODES.EXE, [INSTALL.TXT](../../../package/INSTALL.TXT), OEMSETUP.INF,
checksums) is assembled locally under `output/release/`.

## Stretched bitmaps at 16 and 32 bpp

Reported by the owner: toolbar, ruler and picture corruption in Word at
1920x1080x32 (`machine=svga_s3`). Reproduced with Paintbrush's tool palette:
at 32 bpp it was drawn 3 rows low with stale rows on top, while BitBlt from
the same bitmaps was exact.

The driver has no StretchBlt, so GDI emulates it at 16 and 32 bpp in bands.
For each band it reads the source with GetDIBits (24 bpp), stretches the DIB
itself, sets it with SetDIBits into a scratch bitmap a few rows taller than
the band, and BitBlts the top rows to the destination. The driver placed
DIB scan 0 at the bottom row of the *bitmap*; GDI expects it at the bottom
row of the *DIB*, whose top is bitmap row 0. DeviceBitmapBits now counts
rows from the DIB's height and skips rows past the bitmap. At 8 bpp GDI
stretches device bitmaps itself, which is why the S3 path never showed it.

`DISPHUGE.EXE` (`tests/win16/display_huge_probe.c`) checks GDI's huge-bitmap
layout against the driver (SetBitmapBits, then GetPixel), BitBlt, and
StretchBlt to 1x, +10 and +37 rows, for 122x100, 122x450, 640x200 and
300x300. Before: most stretched rows wrong at 32 bpp, and at 16 bpp. After:
all rows correct at both depths. Paintbrush at 1280x720x32 on `svga_s3`
then matches the 8-bpp S3 rendering except the palette approximations.

Follow-up (owner): with that build Word's stripes were gone, but its
toolbar faces, ruler and Tip of the Day background came out black. The row
rule itself was checked against Microsoft's VGA.DRV with `DIBROWS.EXE`
(`tests/win16/display_dibrows_probe.c`): SetDIBits/GetDIBits with DIB
heights above and below the bitmap's, with and without start scans. VGA.DRV
and the new S3WIDE place every row identically (the DIB's top is bitmap row
0; rows outside the bitmap are dropped); the old S3WIDE did not. Shrinking
StretchBlt follows GDI's default BLACKONWHITE mode (rows ANDed), as
expected. Word's remaining black areas are not reproduced with the programs
on hand; a tracing build (dib.skip and blit-source events) went to the
owner to capture Word's calls.

The owner's trace (26,243 calls) showed 952 failures, all the same:
ExtTextOut with no characters and ETO_OPAQUE, which applications use to fill
a rectangle with the background colour. S3_TextOut required a font and text
before looking at the count, so it failed, and Word's toolbar faces, ruler
and Tip of the Day panel (drawn into memory bitmaps) were never painted.
Into the screen GDI covered the failure; into a memory bitmap it did not.
An empty string now fills just the opaque rectangle. `DISPRLE.EXE` gained
TEXTFILL checks against FillRect: into a memory bitmap the old driver left
all 2048 pixels black, the new one matches exactly.

The same trace showed the owner's SYSTEM.INI asking for 1920x1080x16 on
`machine=svga_s3` without DOSBox-X's high-definition VESA options, so the
BIOS lacked the mode and the driver fell back to the native 1280x720x8 S3
mode (`vbe.nomode`, `mode.selected 0001`).

## Native stretching (StretchBlt, StretchDIBits)

Video for Windows' full-screen mode switches to a 320x240/320x200 VGA mode
through DISPDIB and bypasses the display driver, so a 16:9 film is shown
in a 4:3 mode (DOSBox-X then stretches it to the window; the replacement
DISPDIB.DLL in `../20261102-dispdib/` fixes the shape). Playing in a
window through the driver keeps the desktop's shape, but every frame was
stretched by GDI's emulation (GetDIBits/SetDIBits bands and BitBlt), which
is slow and, at 16/32 bpp beyond about 800 pixels wide, draws nothing.

The driver now exports StretchBlt (27) and StretchDIBits (28) and reports
RC_STRETCHBLT | RC_STRETCHDIB. It draws SRCCOPY enlargements (and same
size) itself: a column map built once per call, each source row converted
once, repeated destination rows written again from the scaled buffer.
Shrinking (whose result depends on the stretch mode, which Windows 3.1's
DRAWMODE does not carry), mirroring, other ROPs, RLE sources, a device
source to itself and GetDIBits-style reads return -1 so GDI emulates them
as before.

GDI's choice of source pixel was measured with `DISPSMAP.EXE`: a
Bresenham replicator, destination pixel i shows source pixel
((i + 1) * src - e0 - 1) / dst with e0 = ceil(src / 2) for columns and
ceil(src / 2) - 1 for rows. The driver uses the same rule, and host tests
check it against the measured sequences.

`DISPSTR.EXE` draws ten cases natively (SRCCOPY) and through GDI's
emulation (MERGECOPY with a white brush, same pixels, always emulated):
1x, 2x, fractional 40->103 and 30->77, sub-rectangles, 1/4/8/24-bpp DIBs,
StretchBlt from bitmaps. All match pixel for pixel on the S3 path (8 bpp)
and at 1280x720x32 and 1920x1080 16/32 bpp. Frame time for 320x180 to the
full screen at `cycles=fixed 100000`: 1280x720x32 55 ms native vs 585 ms
GDI; 1920x1080x16 73 ms; 1920x1080x32 91 ms (GDI's emulation draws
nothing at that width).

## Windows Setup (OEMSETUP.INF)

`package/OEMSETUP.INF` (written by `package/make_oemsetup.py`) offers 39
entries: 13 resolutions from 640x480 to 3840x2160, each at 256, 65K and 16M
colours. Every entry installs S3WIDE.DRV with small fonts ("100,96,96"),
*vddvga, VGA.3GR and the VGA logo, and its work section writes Width,
Height and Depth to `[s3wide.drv]` in SYSTEM.INI. The line format and the
work-section syntax are those of WfW 3.11's own SETUP.INF (and of the
vbesvga OEMSETUP.INF). SETUP.EXE contains the `oemsetup.inf` and
`oemdisks` strings it reads. Profile and section names stay within 8
characters, as in SETUP.INF. `package/make-package.sh` builds the zip.

First live test (owner, WfW 3.11 in DOSBox-X): Setup listed the entries,
then asked for "Windows for Workgroups 3.11 Disk 2" for CGA40WOA.FON. For a
display with VGA.3GR and "100,96,96", Setup copies VGA.3GR, VGALOGO.LGO/.RLE,
VGASYS/VGAFIX/VGAOEM.FON and the four *WOA.FON DOS-window fonts (SETUP.INF
[VGA.3gr], [sysfonts], [fixedfonts], [oemfonts]). The INF now takes the
grabber and logo from the package disk, and make-package.sh copies all of
these (and DOSAPP.FON) from an installed WINDOWS\SYSTEM (`WINSYS=`) into the
local package, so the package folder answers every disk prompt.

The INF has not been run through Setup here. Driving DOS-mode SETUP with
DOSBox-X AUTOTYPE reached the "Other" disk prompt, but keystrokes were then
lost or stalled from run to run, so that experiment proved nothing either
way.

Not tested: real hardware, Windows Setup with the OEMSETUP.INF, windowed DOS boxes, Video for Windows, switching
with Alt+Tab or Alt+Enter by hand.
