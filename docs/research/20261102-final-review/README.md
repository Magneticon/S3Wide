# Final review and release 1.0.0 (2026-11-02)

## Review

Three independent read-only reviews covered the DISPDIB replacement, the
raster/DIB/stretch code, and the mode/switch/cursor code. Confirmed defects
and their fixes:

| Area | Defect | Fix |
|---|---|---|
| DISPDIB | RLE frame buffer owned by the first calling task (MCIAVI's player task); after that task ended, the next RLE film wrote through a freed selector | Allocated `GMEM_DDESHARE`, so the DLL owns it |
| DISPDIB | Malformed RLE8 deltas or runs could push x/y past 32767 and write outside the frame | Stop at the last row; clamp x to the width |
| DISPDIB | No upper bound on `biHeight` before 16-bit arithmetic | Reject frames taller than 1024 |
| DISPDIB | Single `DisplayDib` call without NOWAIT never took the mouse capture | `begin` gets the caller's flags |
| DISPDIB | Minor: `biClrUsed` above 256 truncated, mode-list selector unchecked, 768-byte palette on the caller's stack | Clamped, checked, static |
| Raster/DIB | StretchBlt/StretchDIBits into memory bitmaps wider than 4096 pixels overran the 4096-entry column map and the row buffer | Spans wider than the buffers go back to GDI's emulation (-1) |
| DIB | GetDIBits into a DIB wider than the bitmap cleared the whole scan tail in the 16 KiB row buffer | Only the pixels read are converted; the rest of the scan is cleared in buffer-sized pieces |
| DIB | Colour tables longer than the depth indexes (256 entries on a 4-bpp DIB) were refused; tables after a header larger than 40 bytes were read at offset 40 | Clamped; read at `biSize` (headers up to 1024 bytes) |
| Cursor | Software cursor (VESA path) re-initialised on every Enable, so after GDI Death/Resurrection (every full-screen video) the pointer stayed invisible until its shape changed | Initialised once; Enable restarts it with the old shape and position; shape and moves are recorded while disabled |
| S3 path | Resume after a DOS session did not unlock CR39, so on a real Trio64 the mode set would fail after the BIOS re-locked it | CR39=A5h before programming; DAC mask 3C6h reset to FFh |
| S3 path | Hardware cursor stayed enabled while a DOS session owned the screen | Disabled in Suspend, restored on Resume |
| Switch | A repeated 4001h/4002h unbalanced the software cursor's hold | Only real foreground/background changes are acted on |
| Text | ExtTextOut opaque fill refused spans wider than 4096 pixels (memory bitmaps) | Filled in pieces |

Not changed (judged low risk or needing more design): no screen-switch
critical section (INT 2Fh 4003h/4004h); brush origin ignored;
`lpTranslate` ignored for StretchDIBits; Disable ordering against a 4002h
arriving during the mode restore.

## Microsoft-free release: S3PREP

A Windows display driver must carry the cursors, icons, window bitmaps and
OEMBIN tables USER loads from it. Until now the driver was linked with those
resources from a licensed VGA.DRV, so it could not be published. The
release ships the driver as built from source (`S3WIDE.BIN`, only its
version resource) and `tools/s3prep/s3prep.c` (`S3PREP.EXE`, DOS). S3PREP:

1. rewrites the NE header of S3WIDE.BIN with a resource table holding the
   driver's version resource plus every VGA.DRV resource except its
   version (79), keeping VGA.DRV's flags;
2. shifts the resident/module/imported-name and entry tables, places the
   non-resident names, copies the segments with their relocation records
   (sector alignment kept), then the resource data (16-byte units);
3. copies the files Setup installs for a VGA-style display (VGA.3GR,
   VGALOGO.*, VGA*.FON, *WOA.FON, DOSAPP.FON) from the user's
   WINDOWS\SYSTEM (or WINDOWS) into the package folder.

Checks:
- S3PREP built for the host and built for DOS (run inside DOSBox-X) give
  byte-identical S3WIDE.DRV.
- Compared with the same source linked with `VGA_DRV` (wlink), the result
  has identical segments and relocations, name and entry tables,
  non-resident names, header fields and resource data; only the
  resources' memory flags differ (S3PREP keeps VGA.DRV's original ones).
- `package/make-release.sh` refuses a driver with any resource other than
  its version.

## Regression (S3PREP-made driver, DOSBox-X 2026.08.31)

| Run | Configuration | Result |
|---|---|---|
| fin1080 | svga_s3 + HD VESA, 1920x1080x16, demo apps | boots, 1920x1080x16 reported |
| fins3 | svga_s3trio64, native S3 1280x720x8, DOS session first | desktop back after DOS; capture vs readback 57 px (caret area, as at 1280x720x16) |
| fin4k | svga_dosbox, 3840x2160x32 | boots, 3840x2160x32 reported |
| finDISPSTR | 1920x1080x32 | every stretch case 0 mismatching pixels |
| finDISPRLE, finrles3 | 1920x1080x32, S3 1280x720x8 | every RLE case 0 mismatching pixels (TEXTFILL_MEMORY now 0 on S3) |
| finDIBROWS, finDISPHUGE | 1920x1080x32 | complete, no failures |
| findd1, findd2 | DDPLAY on svga_s3 1920x1080x16 and svga_s3trio64 | all checks pass, FAILURES=0 |

The window captures of 1920x1080 and 3840x2160 desktops are cut off by the
host screen (the off-screen part captures black), so those runs are judged
by the reported mode and the probes.

## Release 1.0.0 contents

`S3WIDE.BIN`, `S3PREP.EXE`, `OEMSETUP.INF`, `DISPDIB.DLL`, `VBEMODES.EXE`,
`INSTALL.TXT`, `S3WIDE-GUIDE.PDF` (from `docs/guide/S3WIDE-GUIDE.md` by
`tools/make_guide_pdf.py`), `SHA256.TXT`. No Microsoft files; the guide's
screenshots are of the owner's Windows for Workgroups 3.11 desktop.
