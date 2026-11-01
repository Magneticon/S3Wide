# DOSBox-X video memory and VESA mode limits (2026-11-01)

Question: can DOSBox-X give a Windows 3.11 display driver up to 3840x2160
(4K) and 16M colours, and on which emulated card?

## Method

`tests/dosbox-x/vbe_modes.c` (DOS, Open Watcom `-bt=dos -ms`) calls VBE
4F00/4F01 and prints the reported video memory and every 8 bpp or deeper
graphics mode. Given a mode number it sets the mode (banked), writes and
reads back the first byte and the last byte of the frame through 4F05, and
restores text mode. With a second argument (`hold`) it also fills a 32 bpp
mode with a gradient and a 16-pixel white frame and holds it for 20 s.

DOSBox-X 2026.08.31, plain DOS (no Windows), `memsize=64`,
`integration device=false`, and the VESA list widened with
`vesa modelist width limit=0`, `vesa modelist height limit=0`,
`allow high definition vesa modes=true`, `allow unusual vesa modes=true`,
`allow explicit 24bpp vesa modes=true`. Run outputs are local in
`output/runs/vbe-survey/`.

## Reported memory and largest modes

| machine | vmemsize 8 | 16 | 32 | 64 | 128 |
|---|---|---|---|---|---|
| svga_s3trio64, svga_s3virgevx, vesa_vbe3, vesa_nolfb | 8 MB | 16 MB | 16 MB | 16 MB | 16 MB |
| svga_dosbox, svga_dosbox_vbe2 ("DOSBox Integrated Video") | 8 MB | 16 MB | 32 MB | 64 MB | 128 MB |

- S3 and the `vesa_*` types cap at 16 MB. Largest listed mode:
  1920x1440, up to 32 bpp; the 1920x1440x32 mode is LFB-only (no bank
  window). There are no modes above 1920 pixels wide.
- `svga_dosbox` lists up to 4096x3072. 3840x2160 is offered at 8 bpp from
  8 MB, and at 15/16/24/32 bpp from 32 MB.
  Widescreen sizes listed: 1280x720, 1280x800, 1440x900, 1920x1080,
  1920x1200, 2560x1600, 3840x2160, 4096x2160 (no 2560x1440 without a
  custom mode).

## 4K mode tests on svga_dosbox

| mode | vmemsize | result |
|---|---|---|
| 0x391 3840x2160x8 | 32 | set ok; last byte in bank 126 written and read back |
| 0x395 3840x2160x24 | 32 | set ok; bank 379 readback ok |
| 0x394 3840x2160x32 | 32 and 64 | set ok; bank 506 readback ok (more than 256 banks works) |

Visible check for 0x394 with `output=surface`: the emulator window
client area measured 3840x2160 (GetClientRect). The log shows
`Aspect ratio: 3840 x 2160`. The host screen is smaller, so a window
capture shows only the on-screen upper-left part. That part shows the
expected left half of a full-width gradient: red reaches about 0x80 at
x=1920.

Practical notes:
- `output=opengl` scaled to 1152x648 slowed emulation enough that the
  33 MB fill did not finish in 400 s.
- Ctrl+F5 and a mapper file did not trigger DOSBox-X screenshots when
  the keys were sent with WScript SendKeys.
- A `-conf` path typo makes DOSBox-X fall back to the user's
  `%LOCALAPPDATA%\DOSBox-X\dosbox-x-2026.08.31.conf`. That file has an
  empty autoexec, so the run sits at a prompt until the timeout. No
  guest was mounted.

## Conclusion

4K with 16M colours (32 bpp, 33,177,600-byte frame) is available only on
DOSBox-X's own integrated video card (`machine=svga_dosbox`,
`vmemsize>=32`). It is not available on any emulated S3. The S3 path
tops out at 1920x1440 / 16 MB.

None of this has run under Windows yet: AGENTS.md rule 7 still applies to
any driver mode built on these results. A driver for `svga_dosbox` would be
a VESA/integrated-video driver rather than the S3 driver (AGENTS.md rule 2),
so it needs an explicit project decision.
