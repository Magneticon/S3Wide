# Drawing speed at 4K (2 November 2026)

At 3840x2160x32 the driver from [the VESA backend work](../20261101-vbe-backend/README.md)
booted, but many common drawing operations took seconds. This note records
where the time went, what changed, and the measured effect.

## Measuring

`tests/win16/display_bench_probe.c` (`build/DISPBNCH.EXE`, built by
`tests/win16/build-win16-probes.sh`) times GDI operations on the screen DC.
Each test repeats until it has run for 1.5 s, and the probe writes
`BENCH_<name>=<reps>,<total ms>,<us per rep>` to `RESULT.TXT`. The emulator
runs at a fixed rate (`CYCLES="fixed 100000"`, new in
`run-display-baseline.sh`), so `GetTickCount` counts emulated instructions,
not host speed, and runs are comparable even when the host is busy:

    PROBE=build/DISPBNCH.EXE CYCLES="fixed 100000" FRAMESKIP=10 \
    VESA_HD=1 MACHINE=svga_dosbox VMEMSIZE=64 OVERLAY=<guest overlay> \
    TIME_LIMIT=1800 sh tests/win16/run-display-baseline.sh <run dir>

The overlay's `SYSTEM.INI` selects the mode in `[s3wide.drv]`. The tests:

| Test | What it draws |
|---|---|
| FILL_SOLID | full-screen PatBlt PATCOPY, solid brush |
| FILL_PATTERN | full-screen PatBlt PATCOPY, hatched brush |
| DSTINVERT | full-screen PatBlt DSTINVERT |
| PATINVERT | full-screen PatBlt PATINVERT, hatched brush |
| SCROLL | full-screen screen-to-screen BitBlt, 8 rows up |
| SAVE_RESTORE | 256x256 screen to memory bitmap and back |
| TEXT_OPAQUE / TEXT_TRANSPARENT | 40 lines of 71 system-font characters |
| DIB24 / DIB8 | SetDIBitsToDevice of a 256x256 24-bpp / 8-bpp DIB |
| LINES | 200 one-pixel lines (Output polyline) |
| RECTS | 100 Rectangle() calls, 100x100 (GDI simulates them with scanlines) |
| ICONS | 100 DrawIcon calls |
| GETDIB24 | 256x256 screen to memory bitmap, then GetDIBits to 24 bpp |

## Where the time went

- **Software cursor exclusion.** Every drawing call erased and redrew the
  32x32 cursor, byte by byte, wherever the call drew. GDI draws a filled
  rectangle as one Output call per scan line, so a 100x100 rectangle cost
  about 200 cursor redraws.
- **Per-pixel ROP3.** BitBlt evaluated every pixel through a function that
  tests eight minterms on 32-bit values. The 8086 code Watcom generated did
  each 32-bit operation as two 16-bit halves.
- **Per-pixel bank arithmetic.** Lines and Pixel located every pixel with a
  32-bit multiply and a 32-bit divide, both through runtime helpers.
- **Format switches inside pixel loops.** DIB conversions and text decided
  the pixel format, and text also re-read the font tables, for every pixel
  or scan row.

## Changes

- **Cursor** (`s3_swcursor.c`, `S3_DisplayExclude`): each entry point passes
  the screen rectangle it can touch: BitBlt destination and source, Output
  scanline or polyline bounds, text cells plus the opaque rectangle, the DIB
  rectangle, the Pixel point, the FastBorder rectangle. The cursor is still
  held for the whole call, so mouse moves are deferred, but it is erased
  only when it overlaps. Extent queries (ExtTextOut with a negative count)
  no longer touch it.
- **Raster** (`s3_raster.c`): a ROP3 is bitwise, so it is applied to whole
  rows of bytes at any pixel size (`S3_RopBytes`). The common ROPs (DSTINVERT,
  PATINVERT, SRCAND, SRCINVERT, SRCPAINT, SRCERASE, NOTSRCCOPY, and so on)
  are one or two passes of 386 `lodsd` loops (`s3_copy.c`). The rest use a
  dword loop in C. Pattern brushes build one row per scan line by doubling 8
  pixels. ROPs that read the destination combine directly into the frame
  buffer or bitmap instead of a copy of its row. PATCOPY with a pattern
  brush writes the pattern row directly.
- **Copies** (`s3_copy.h`): row copies use `rep movsd`.
- **Banks** (`s3_surface.c`): 64 KiB banks (every backend) split an offset
  with a shift, not a divide.
- **Lines** (`S3_LineVisit`, `s3_output.c`): the DDA steps its remainder
  instead of a multiply and divide per pixel. Screen pixels step their byte
  offset from the previous pixel and stay in the mapped bank.
- **DIBs** (`s3_dib.c`): one tight loop per source format and pixel size
  (24 to 32 bpp is a byte copy). 1- and 4-bpp indices are unpacked once per
  row. Scans are copied with `rep movsd`.
- **Text** (`s3_text.c`): glyph pointer, width and position are worked out
  once per call (strings up to 256 characters). Each glyph is clipped to the
  cells once per row, and empty glyph bytes are skipped.
- **Compiler** (`build.sh`): `-3 -ox -ot -zfp -zgp`. The driver already
  needed a 386 (Enhanced Mode, DPMI linear frame buffer). Compiled code may
  now use the upper halves of 32-bit registers, which 16-bit interrupt
  handlers do not save, so MoveCursor and CheckCursor, which run at
  interrupt time, wrap their bodies in `pushad`/`popad`. The `__asm` row
  loops save EAX themselves.

New host tests check `S3_RopBytes` against the reference ROP3 for all 256
codes (with an odd tail), polylines across 64 KiB bank boundaries (COPYPEN
pixel positions and XORPEN twice restoring the frame), and cursor exclusion
with overlapping and distant rectangles.

## Results

Emulated microseconds per operation (`us per rep` / 1000 = ms), DOSBox-X
2026.08.31, `machine=svga_dosbox`, `vmemsize=64`, `cycles=fixed 100000`, WfW
3.11 Enhanced Mode, disposable guest copies; the source guest was unchanged
after every run. Before: driver `a0c654bf39a5e801` (5ddf84f source). After:
driver `a5ed73ac51febf7b` (this change). Both are local builds that carry
the imported VGA.DRV resources, so only the source is committed.

| Test | 1280x720x32 before | after | 3840x2160x32 before | after | 4K speed-up |
|---|---:|---:|---:|---:|---:|
| FILL_SOLID | 29 ms | 15.5 ms | 220 ms | 110 ms | 2.0x |
| FILL_PATTERN | 1813 ms | 31 ms | 16148 ms | 235 ms | 69x |
| DSTINVERT | 2142 ms | 33 ms | 19224 ms | 256 ms | 75x |
| PATINVERT | 2307 ms | 67 ms | 21091 ms | 549 ms | 38x |
| SCROLL | 57 ms | 30 ms | 453 ms | 228 ms | 2.0x |
| SAVE_RESTORE | 15.1 ms | 5.8 ms | 15.4 ms | 5.3 ms | 2.9x |
| TEXT_OPAQUE | 351 ms | 81 ms | 352 ms | 81 ms | 4.3x |
| TEXT_TRANSPARENT | 341 ms | 85 ms | 352 ms | 85 ms | 4.1x |
| DIB24 | 183 ms | 14.2 ms | 183 ms | 14.4 ms | 12.7x |
| DIB8 | 66.9 ms | 11.7 ms | 66.9 ms | 11.1 ms | 6.0x |
| LINES | 1757 ms | 275 ms | 4669 ms | 641 ms | 7.3x |
| RECTS | 27627 ms | 453 ms | 28616 ms | 330 ms | 87x |
| ICONS | 1264 ms | 123 ms | 1291 ms | 114 ms | 11x |
| GETDIB24 | 159 ms | 18.5 ms | 159 ms | 18.3 ms | 8.7x |

At 4K, LINES draws longer lines than at 720p (the end points spread over the
whole screen), and RECTS is partly GDI's own scanline work, which the driver
does not control.

Correctness, with the final build and DISPBASE.EXE `DEMO=1` (Notepad and
Calculator):

- S3 path, 1280x720x8 on `svga_s3trio64`: the emulator capture equals the
  GDI readback except 27 pixels of the I-beam pointer (the hardware cursor
  is not in the readback).
- VESA path, 1280x720x32: capture equals readback except 57 pixels (the
  I-beam and the blinking caret).
- The 8-bpp S3 readback and the 32-bpp VESA readback are identical except
  the caret, so both paths draw the same pixels in the system colours.

Full 4K readback: with `cycles=max`, `FRAMESKIP=30` and `DEMO=1`, DISPBASE.EXE
read the whole 3840x2160x32 screen back through GDI (`CAPTURE_STAGE=0`;
[quarter-size image](notepad-3840x2160x32-readback-quarter.png), Notepad
maximized). The run, from emulator start to the end of the readback, took
under 1.5 minutes of host time. Before this change the same readback
stopped at 465 of 2160 rows when it hit an 1100 s limit.

## Not covered

- The emulator itself spends much of the host's time converting 4K frames
  for display. `FRAMESKIP` in the runner reduces that for unattended runs.
  It does not change anything the guest measures.
- Screen-to-screen copies still go through one row buffer, because both
  rows share the drawing window. A second selector for the source would
  save one pass.
- 24-bit DIBs drawn at 8 bpp still look up each pixel's colour-cube index
  separately.
