# Full-screen Video for Windows at the film's own shape (DISPDIB.DLL)

## Problem

Video for Windows' full-screen playback (MCIAVI "play fullscreen", Media
Player's Device, Configure, Full Screen) does not draw through the display
driver. MCIAVI loads `DISPDIB.DLL` and calls `DisplayDibEx`: `BEGIN` takes
the screen from the driver (GDI `Death`), switches the card to a 256-colour
VGA mode (320x240, or 320x200 when "240 Line Fullscreen" is off), and each
frame is centred in that mode with square pixels; `END` restores text mode
and gives the screen back (`Resurrection`). Full-screen playback is fast
because a frame costs at most 76,800 bytes of video memory.

On a 16:9 display that 4:3 mode is stretched to 16:9: DOSBox-X full screen
with `aspect=false` does this, and so does a widescreen monitor. A 320x180
film letterboxed in 320x240 then shows at (16/9)·(16/9)/(4/3) ≈ 2.37:1, the
"about 21:9" the owner reported. Playing in a maximized window keeps the
shape (the driver stretches natively, see the release review), but at
1920x1080 every frame becomes megabytes of video memory, which is far too
slow for Indeo on an emulated 486/Pentium.

## Interface (from Microsoft's DISPDIB.DLL and MCIAVI.DRV, local only)

The owner's copies of `DISPDIB.DLL` and `MCIAVI.DRV` were read locally
(`output/vfw/`, ignored, never committed):

- `DISPDIB.DLL` exports `DISPLAYDIB` @1, `DISPLAYDIBEX` @2, `WEP` @99. MCIAVI
  calls `GetProcAddress` for `DisplayDibEx` and `DisplayDib` by name.
- `UINT FAR PASCAL DisplayDibEx(LPBITMAPINFOHEADER, int x, int y, LPSTR bits,
  WORD flags)`. Flags: `NOPALETTE` 0x10, `NOCENTER` 0x20, `NOWAIT` 0x40,
  `NOIMAGE` 0x80, `ZOOM2` 0x100, `DONTLOCKTASK` 0x200, `TEST` 0x400,
  `NOFLIP` 0x800, `END` 0x4000, `BEGIN` 0x8000, mode in the low nibble.
  Results: 0 ok, 1 not supported, 2 invalid DIB, 3 invalid format,
  4 invalid task.
- Begin in the original: check the DIB (40-byte header, ≤ 8 bpp, BI_RGB or
  BI_RLE8); return after the check if `TEST`; `SetErrorMode(0x8001)`;
  `Death(GetDC(0))`; set the mode; `LockCurrentTask(1)` unless
  `DONTLOCKTASK`; `SetCapture(GetActiveWindow())` unless `NOWAIT`; load the
  palette unless `NOPALETTE`. End: wait for a key or click unless `NOWAIT`;
  release capture; text mode; `LockCurrentTask(0)`;
  `Resurrection(hdc,0,0,0,0,0,0)`; `SetErrorMode(old)`;
  `InvalidateRect(0,0,0)`.
- MCIAVI only refuses a film that is bigger than the selected 320x240 or
  320x200 mode ("This AVI file is too big to be played in the selected VGA
  mode."); it passes frames at the film's own size.

## Replacement

`dispdib/dispdib.c` (built by `dispdib/build.sh` into `build/DISPDIB.DLL`,
7.5 KB, our own code) keeps the interface and the begin/end sequence above,
but:

- **Lays the frame out for the display's shape** (`[dispdib] Aspect`,
  default 16:9): a film of aspect F in a W x H mode shown on a display of
  aspect A fills the width when F ≥ A (height H·A/F) and the height
  otherwise (width W·F/A). A 16:9 film fills a 16:9 screen; a 4:3 film gets
  black side bars. `Aspect=0` keeps square mode pixels (what the original
  does, right for a display that shows each mode at its own shape).
- **Uses the smallest 8-bpp linear-frame-buffer VESA mode that holds the
  film at full resolution** after that fit (or `Width`/`Height` when set).
  On DOSBox-X's `svga_s3` and `svga_s3trio64` BIOS that is 320x200 (mode
  153h) for a 320x180 film. Those BIOSes list no 848x480 or 640x360
  256-colour mode; their smallest 16:9 one is 1280x720, about 14 times the
  bytes per frame.
- Scales by nearest pixel with a column map built once per frame, each
  source row scaled once, and writes rows into the LFB through one
  selector moved per 64 KiB band (DPMI 0800h/0007h). RLE8 frames, deltas
  included, are decoded into a persistent frame. The palette goes to the
  DAC only when it changes.
- A call without `BEGIN` while inactive does an implicit begin and end
  (the single `DisplayDib` case); `END` never waits (MCIAVI sends it with
  the film stopped).
- `[dispdib] Log=1` appends the calls and the BIOS's 8-bpp modes to
  `C:\DISPDIB.LOG`.

No GDI or driver code runs between `Death` and `Resurrection`. The S3WIDE
driver change in the same commit makes the hardware cursor stay away
meanwhile: `MoveCursor` and `SetCursor` leave the S3 cursor registers alone
while the driver is disabled (`S3_DisplayIsEnabled`) or in the
background, and enabling restores the S3 cursor state.

## Test

`tests/win16/dispdib_probe.c` (`build/DDPLAY.EXE`) loads the DLL like
MCIAVI, plays generated 8-bpp frames (`BEGIN|NOWAIT`, frames with `NOWAIT`,
`END`), and reads the VESA frame buffer back (VBE 4F03/4F01, its own LFB
mapping) to compare every pixel with the frame fitted by the same rule. It
checks a 16:9 frame (320x180, white circle), the same picture as RLE8, a
4:3 frame (240x180), the 16:9 frame again (borders cleared), the DAC
palette, 120 frames timed, the desktop after `END` (GDI `PatBlt` and
`GetPixel` through S3WIDE), and a single `DisplayDib` call.

DOSBox-X 2026.08.31, Windows for Workgroups 3.11, disposable guest copies
(`source_guest=unchanged`), S3WIDE built from this commit with local VGA.DRV
resources, `cycles=max`, headless unless noted.
`DISPDIB.DLL` SHA-256 `8f5fca68ab282345737088b2f9210795f3d78c1a440fc9e4a2937d4d5ec160b3`.

| Run | Machine, desktop | `[dispdib]` | Mode used | Result |
|---|---|---|---|---|
| dd3 | svga_s3, HD VESA on, 1280x720x16 | default (16:9) | 153h 320x200 | all pass; 16:9 frame fills 320x200, 4:3 frame 240x200 centred |
| dd4 | same | `Aspect=0`, 1280x720 | 23Eh 1280x720 | all pass; 4:3 frame 960x720 |
| dd5 | same, visible window | default | 153h 320x200 | all pass; capture below |
| dd6 | svga_s3trio64 4 MB, native S3 1280x720x8 | default | 153h 320x200 | all pass |

Frame cost (120 frames minus the time to generate them, same run):
320x200 about 10 ms per frame; 1280x720 about 90 to 140 ms per frame,
which is too slow for 12 fps, hence the smallest-mode choice.

The visible run's capture (`output/runs/dd5/emulator-window.png`, 640x400:
the 320x200 mode doubled) shows the circle taller than wide; stretched to
16:9 as a full-screen 16:9 display does (`as-16x9.png`) it is round, with
the colour bars and frame edges at the screen edges.

Owner's test (2026-11-02, live WfW 3.11 with Video for Windows and the
Indeo 4 codec, DOSBox-X `machine=svga_s3`, full screen 1920x1080,
`aspect=false`): Media Player's Full Screen through this DLL shows a
320x180 (16:9) Indeo 4.1 film filling the screen, and a 320x144 (2.22:1)
film at 1920x864 with 108-pixel bars at the top and bottom, the shape the
film has. With Microsoft's DISPDIB.DLL the same film came out about 2.96:1.

Not tested: the cursor guard with the mouse moving during playback; real
hardware.
