# VESA linear-framebuffer backend: 4K and 16M colours (1 November 2026)

Decision (project owner, 2026-11-01): add an integrated-video/VESA path to
`S3WIDE.DRV` for modes the emulated S3 cannot provide, keeping the S3 path
for its native mode. The survey behind it is in
[`20261101-vesa-limits`](../20261101-vesa-limits/README.md).

## Design

- **Mode choice** (`src/s3_vbe.c`, `S3_ModeSelect`): read `Width`,
  `Height`, `Depth` from `[s3wide.drv]` in `SYSTEM.INI` (KERNEL
  `GetPrivateProfileInt`). Ask VBE 4F00 for the OEM string. On S3 hardware a
  1280x720x8 request uses the S3 register path. Otherwise the driver scans
  the VBE mode list (4F01 through DPMI 0300h with a `GlobalDosAlloc` buffer)
  for an exact linear-framebuffer mode: 8 bpp packed, 16 bpp 5:6:5 with red
  at bit 11, or 32 bpp with red at bit 16. It then maps the framebuffer with
  DPMI 0800h. This happens before the first GDIINFO request, so GDI sees the
  real geometry.
- **Geometry** (`src/s3_mode.c`): GDIINFO, the PDEV and the surface read
  `s3_mode`. The mapping-mode extents are now resolution-independent
  96 dpi ratios (0.1 mm = 127/48 per pixel, and so on).
- **Memory access** (`S3_Win16BridgeOpenLinear`): the same 64 KiB selector
  window as the S3 path, but each "bank switch" moves the selector base
  instead of programming CR35/CR51. A 3840x2160x32 frame is 507 banks.
- **Mode set/restore**: VBE 4F02 with the LFB bit; the 8-bpp colour cube
  is loaded through the DAC ports. Restore returns to the mode saved with
  4F03 (text mode 3 in practice).
- **Software cursor** (`src/s3_swcursor.c`): drawn through its own DPMI
  selector, so moves at mouse-interrupt time never touch the drawing
  window. Every screen-drawing entry point (BitBlt, Output, Pixel,
  ExtTextOut/StrBlt, FastBorder, SetDIBitsToDevice) brackets itself with
  exclude/unexclude. Moves during drawing are deferred to Unexclude or
  CheckCursor. AND/XOR masks are applied bytewise, so one routine serves
  every depth.
- **Pixel depth** (`include/s3_pixel.h`): pixels are 1, 2 or 4 bytes. At
  8 bpp physical colours keep the 0xFF000000 index marker; in direct
  colour they are the pixel value (5:6:5 or 0x00RRGGBB). Brushes hold
  pixel values. Raster, DIB, text, scanline, polyline and Pixel paths read
  and write rows of whole pixels; ROP3 is evaluated on 32-bit values.
  Memory bitmaps may be huge (multi-segment), which 32-bpp compatible
  bitmaps over 64 KiB require.
- **Row buffers** are 4096 pixels x 4 bytes, in their own far data
  segments (DGROUP stays about 8 KiB).

## Results

DOSBox-X 2026.08.31, `machine=svga_dosbox`, `vmemsize=64`, VESA HD modes
listed (runner `VESA_HD=1`), WfW 3.11 Enhanced Mode, disposable guest
copies; the source guest was unchanged after every run.

| Mode | Machine, vmemsize | Windows reports | Evidence | Driver SHA-256 (first 16) |
|---|---|---|---|---|
| 1280x720x8, S3 path | `svga_s3trio64`, 4 | 1280x720, 8 bpp | regression: scanout = readback except 126 cursor pixels | `1257bb84e3780543` |
| 1280x720x8, VESA | `svga_dosbox`, 64 | 1280x720, 8 bpp | scanout = readback except 126 cursor pixels | `17014636cd53f5fc` (before depth work) |
| 3840x2160x8, VESA | `svga_dosbox`, 64 | 3840x2160, 8 bpp | [bottom-left of readback](desktop-3840x2160x8-readback-bottomleft.png) | `fb140085b2e91f0c` (before depth work) |
| 1280x720x32, VESA | `svga_dosbox`, 64 | 1280x720, 32 bpp | [Notepad](notepad-1280x720x32-emulator.png); scanout = readback exactly except 27 I-beam pixels, no colour mapping | `60dc4af73fea4e36` |
| 1920x1080x16, VESA | `svga_dosbox`, 16 | 1920x1080, 16 bpp | [Notepad, readback at half size](notepad-1920x1080x16-readback-half.png); the 887 on-screen rows of the scanout equal the readback except 57 caret and I-beam pixels | `1257bb84e3780543` |
| 3840x2160x32, VESA | `svga_dosbox`, 64 | 3840x2160, 32 bpp, NUMCOLORS 20 | [bottom-left of readback](desktop-3840x2160x32-readback-bottomleft.png); the run hit its 1100 s limit after 465 of 2160 readback rows; the source guest was hashed by hand afterwards and was unchanged | `60dc4af73fea4e36` |

`60dc4af7...` and `1257bb84...` differ in 2 bytes (the build stamp), not in
code. No trace from any VESA run recorded a failed driver call.

## Known limits

- The host window at 3840x2160 is larger than this machine's screen, so the
  emulator capture only shows its upper-left part. The full-frame evidence
  is the GDI readback.
- The test tool's GDI readback of a 4K screen takes many minutes in
  emulation; the driver itself has no per-frame cost there.
- All drawing is done by the CPU; no acceleration exists on this path. The
  2 November speed work ([`20261102-4k-speed`](../20261102-4k-speed/README.md))
  made common operations 2x to 87x faster at 4K.
- DOS boxes, full-screen switching and Windows' own mode-change paths are
  untested on the VESA path (no VDD for it).
- 24-bpp packed modes are not used; 32 bpp gives the same 16M colours.
