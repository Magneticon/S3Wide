# S3 1280x720 Win16 driver recovery

This is a local recovery branch with an independently authored Win16 display
driver ABI and framebuffer milestone. It does not modify the private
`TheKernelDoc/S3` repository or the Atalanta workspace, and it has not been
published.

## Implemented in this milestone

* Exact Win16 `GDIINFO` layout: 110 bytes, with a compile-time size check.
  `Enable(InquireInfo)` fills a 1280x720, 8-bpp, one-plane description and
  returns 110. The Win16 `PDEVICE` is 32 bytes, has the required nonzero leading
  type word, and holds an optional surface-state far pointer.
* `ColorInfo` converts RGB to and from a deterministic fixed 3:3:2 palette;
  palette indices pass through unchanged.
* `GetDriverResourceID` preserves the requested ID for every resource type.
* Banked 8-bpp pixel and half-open rectangle-fill primitives have bounds,
  pitch, bank-window, and mapping-failure checks, plus a clipped rectangle
  fill, inclusive Bresenham line, and clipped source-copy primitive. Source
  copies handle horizontal and vertical self-overlap with memmove-style
  traversal and can span banks. Their bank mapper is an explicit callback so
  bounds, bank crossings, and mapping failures can be exercised on the host.
* A second, still-not-connected hardware adapter selects 64 KiB S3 bank
  numbers through the verified CR38/CR35/CR51 contract and loads the fixed
  3:3:2 palette through the standard six-bit VGA DAC ports. Register I/O is
  host-tested and exercised against the pinned DOSBox-X model. It also
  round-trips the candidate S3 PLL fields and 8-bpp CRTC pitch calculation.
  A separate candidate routine snapshots, programs, and restores the
  1280x720 timing/PLL register subset; it is not connected to device enable.
* The Windows 3.x NE DLL exports DDA entrypoints `BitBlt` at 1, `ColorInfo`
  at 2, `Enable` at 5, `Disable` at 6, `Output` at 8, `Pixel` at 9, `RealizeObject` at 10,
  `SetDIBitsToDevice` at 21, and `GetDriverResourceID` at 450. The DDA adapters
  execute only their narrow 8-bpp paths when a surface is attached: `BitBlt`
  accepts SRCCOPY from a top-down memory `PBITMAP`, `SetDIBitsToDevice` accepts
  banded bottom-up 8-bpp `BI_RGB` DIB scans with the provided palette-index
  translation table, and `Pixel` reads a pixel or writes with `R2_COPYPEN`.
  `Output` handles solid one-pixel `OS_POLYLINE` with `R2_COPYPEN`; each line
  segment includes its first pixel and excludes its endpoint. Unsupported
  styles and raster operations request GDI simulation. `RealizeObject` reads
  the 10-byte Win16 logical `LOGPEN`, returns a 12-byte driver-owned physical pen,
  and uses nominal one-pixel pens for line styles/widths that GDI must simulate.
  `LibMain` is exported by name to avoid colliding with DDA ordinal 1. The
  module carries a Win16 `VERSIONINFO` resource (RT_VERSION ID 1), marked as a
  display driver. It contains no synthetic `OEMBIN` or `FONTS` records. The
  driver has no device fonts; `RealizeObject` declines font realization so GDI
  uses its system fonts.

The second, device-enabling `Enable` call deliberately returns failure. A
separate injected-operations lifecycle helper connects bridge open, the banked
surface and PDEV attachment, preserves live cleanup ownership across repeated
runtime initialization, and retries cleanup without dereferencing the saved
PDEV; native
`Enable` does not call it until a verified Windows mode/I-O provider exists.
The mode routine is a host-testable timing/PLL candidate overlay, without an
active hardware I/O backend, memory mapping, or display-start setup. A separate
host-testable VGA/S3 snapshot captures and verifies the DOS-tested sequencer,
CRTC, GC, AC, and DAC state through injected callbacks; it is not connected to
native port I/O or device enable. `GDIINFO` reports zero drawing capabilities, so it does not claim
working GDI drawing, text, palette management, or fonts. The candidate timing,
PLL, packed 8-bpp mode sequence, scanout, and banked VRAM path pass in the
pinned DOSBox-X S3 model; they are not yet connected to Windows protected-mode
I/O/mapping or validated in a Windows guest or on physical hardware.

The `S3_SurfaceBlitDIB8` software path is host-testable without device enable.
It accepts an explicit 8-bpp DIB view with checked byte length, row stride, and
top-down/bottom-up orientation; it clips against both rectangles and supports
only `SRCCOPY`. The Win16 `BitBlt` adapter accepts only a matching 8-bpp,
top-down `PBITMAP` source and rejects data spans that exceed a single 64-KiB far-pointer
segment. `Pixel` supports reads and copy-pen writes when the PDEV has an
attached surface. The current `Enable` leaves that pointer null, so these paths
are tested independently and are not active in a Windows guest. The polyline
core uses integer Bresenham rounding and can clip to a half-open rectangle.
The DDA adapter clips to framebuffer bounds and ignores `lpClipRect` because
`dpClip` remains zero. A failed bank mapping can leave a drawn prefix, so
drawing is not atomic.

The DDA `PBITMAP` memory-source path is not a general 1280x720 DIB upload: a
full image needs 921,600 bytes, beyond a single Win16 far-pointer segment. The
separate `SetDIBitsToDevice` DDI now handles the bounded banded case. It accepts
only a positive-height, 8-bpp, uncompressed Windows 3.x DIB with the complete
256-entry color table and translation array. Each band contains `cScans`
bottom-up scanlines starting at `iScan`; its DWORD-padded byte span and the
far-pointer offset must fit one 64-KiB segment. The code maps each source scan
to `Y + biHeight - 1 - (iScan + row)`, applies GDI's `lpTranslate` index map,
clips against the supplied rectangle and surface, and declines RLE, other bit
depths, invalid palette maps, and segment-crossing bands. `dpRaster` remains
zero, so this DDI is not advertised, `EnableDevice` still fails closed, and
guest acceptance remains blocked.

## Build and checks

Use Open Watcom V2 for the Win16 module and a host GCC for deterministic
surface/ABI checks:

```sh
WATCOM=/path/to/open-watcom-v2 OWBIN=binl64 ./build.sh
```

The build creates `build/s3wide_driver.dll`, `build/inspection.txt`, and runs
`tests/host_test.c`. The Win16 artifact is an NE module; the
inspector checks the NE format, required entrypoint ordinals, the VERSIONINFO
resource and display-driver type, and absence of placeholder font/OEMBIN
resources. Host
checks validate the 110-byte `GDIINFO`, key field offsets, 32-byte Win16
PDEVICE contents, 32-byte Win16 `PBITMAP` layout (including the 14-byte
prefix through `bmBits`), 40-byte `BITMAPINFOHEADER`,
DIB SRCCOPY clipping, 1280x720 banded DIB scan placement/translation across
all framebuffer banks, Pixel read/copy-pen behavior, far-pointer span limits,
inquire/failure behavior,
all 256 3:3:2 color-index round trips, resource-ID
identity, physical-pen realization, DDA polyline half-open endpoints and ROP
fallback, 256 deterministic clipped-line differential cases, bank-crossing
and clipped raster/line/copy writes, deterministic
copy comparisons against a flat reference, S3 bank register
lock/value preservation, all 768 six-bit DAC palette components, the candidate
PLL register round trip, mocked CRTC/SEQ candidate writes, CR35/CR11/SR08 lock
sequencing, PLL pulse/readback rollback, captured-register restoration, and
1280-byte pitch/address arithmetic. The full VGA/S3 state snapshot captures
sequencer, CRTC and S3 extensions, graphics and attribute controllers, DAC mask,
and all 768 DAC components through injected callbacks; restore readback and
retry after an injected DAC write loss are checked. The isolated protected-mode
bridge uses injected selector, mode, and bank operations; host checks cover selector
allocation/configuration/free, bank bounds/failure, mode rollback, and retryable
restore/cleanup failures. The device lifecycle checks surface attachment,
bank callback routing, rollback retry, and inert drawing state after a failed
selector release. It has no native Windows provider and is not wired to
`Enable`.

`tests/dosbox-x/` contains a separate DOS-only VBE baseline program, exact
DOSBox-X config, and rebuild/run instructions. It checks VBE mode `0x23E`, then
writes and verifies a full 1280x720 frame through the banked window before
leaving a color-bar image visible for capture. It was run in DOSBox-X built
from the pinned upstream commit with SDL dummy video: the guest reports
1280x720x8 and passes complete banked write/readback. The emulator log and DOS
result are preserved in `evidence/dosbox-x-vbe-headless.log` and
`tests/dosbox-x/VBERES.TXT`. Build provenance and hashes are recorded in
`evidence/dosbox-x-build.txt`.
The separate `raw_s3_smoke.c` harness is compiled with `src/s3_hw.c` and run
under the same pinned emulator. It checks the driver's timing/PLL register
readback, CR35/CR51 bank selection for banks 0–14, full-frame pattern
write/readback, and exact register rollback after an injected readback fault.
The VBE reference caught and corrected the candidate's CR51 pitch extension
bits; the raw mode now matches the reference's CR13=`0xA0` and CR51[5:4]=`00`
for 1280-byte pitch. Results and config are in
[`evidence/recovery-status.md`](evidence/recovery-status.md) and
[`tests/dosbox-x/README.md`](tests/dosbox-x/README.md).

The separate `raw_s3_cold_smoke.c` harness starts from VGA text mode 3 without
VBE setup. It verifies tracked-state rollback after an injected failure,
programs packed 8-bpp SEQ/GC/AC state, checks the 1280-byte pitch and S3 width,
and reads back all 921,600 bytes through banks 0–14. An initial all-black
capture came from the test's AC-index reads leaving display enable off; that
original image remains preserved at
[`evidence/dosbox-x-s3-cold-black-scanout.png`](evidence/dosbox-x-s3-cold-black-scanout.png).
The harness now restores AC display enable after snapshots. Controlled tests
showed CR09 row repeat and missing CR3A[4] were the cold-mode scanout issues.
The remaining 613-line/top-band defect came from CR15/CR16 vertical blank
wrap: with CR15=`0xD0`, CR09=`0x60`, and CR16=`0x6A`, DOSBox-X's pinned VGA
renderer skipped 107 initial lines. CR16=`0xFF` makes that renderer's wrap
skip zero while CR07/CR12 encode 720 active lines. Cold rollback, register
fields, all 15 banks, and full-frame readback pass. The VBE-seeded and cold
candidate captures are 1280×720 with 64 colors and pixel-identical to the
VBE reference; `tests/dosbox-x/check_s3_scanout.py` verifies every pixel
against the expected RGB332 pattern. The pre-CR16 613-line captures remain
preserved as diagnostic evidence. No Windows GDI result is claimed. See
[`evidence/recovery-status.md`](evidence/recovery-status.md) for exact output
and register-dump evidence.

Host checks do not load or execute the Win16 DLL. Open Watcom package
provenance, its SHA-256, and its Sybase Open Watcom Public License are recorded
in `toolchain/`.

## ABI references

The field layout and contracts are based on the public Windows 3.1 Device
Driver Adaptation Guide pages linked in [`evidence/abi-and-raster.md`](evidence/abi-and-raster.md).
The code here is independently written. The separate public VBESVGA reference
tree has no license file and contains Microsoft-copyrighted sample material;
no code from it is included or copied into this recovery branch.

## Remaining work and recovery inputs

The cold candidate now produces a complete 1280×720 RGB332 scanout. The first
black result came from the diagnostic snapshot clearing AC display enable.
CR09 row-repeat settings and CR3A[4] (the datasheet 8/16/24/32-bpp mode
selector) explained the repeated rows and 640-pixel cold width. The remaining
613-line image was traced to CR16 vertical blank end: the candidate's `0x6A`
caused the pinned DOSBox-X renderer to wrap blanking and skip 107 initial lines.
Setting CR16=`0xFF` results in a zero-line wrap skip. The VBE-seeded candidate
and cold candidate captures now each match the 1280×720 VBE reference pixel for
pixel, and a checker confirms all expected 64 colors. Rollback covers CR3A and
the timing fields.
The March 1995 S3 datasheet confirms CR42 bit 5 controls interlace and bits
4–0 are reserved; the cold candidate writes CR42 zero. In pinned DOSBox-X, the
CRTC handler returns hardcoded CR42=`0x0D` while `SVGA_S3_GetClock()` uses Misc
Output clock selector 3 and the SR12/SR13 PLL. The VBE-seeded raw timing/pitch,
bank, and forced-rollback checks pass. The cold path verifies banked memory and
tracked register rollback; it resets display start, uses CR31 bank/enhanced-map
bits, and sets a 64-KiB CR58 aperture with LFB disabled.
The DOSBox-X mode helper is already exercised: its cold VGA harness confirmed
visible 1280x720 RGB332 scanout, timing-register rollback, and full banked-frame
readback. Windows integration still needs a verified protected-mode port-I/O
adapter, physical-aperture selector mapping and bank callback, and complete
mode save/restore. Those must be connected to `Enable` and tested through USER,
font loading, and real GDI drawing in a Windows 3.11 guest. See
[`evidence/dosbox-x-s3-contract.md`](evidence/dosbox-x-s3-contract.md) for the
pinned upstream source and supported hardware facts.

The earlier custom driver source and USER/font-stall trace, the matching guest
image/configuration, and the authorized DDK/build assets are still absent. The
The DOS bitmap remains a separate proof, and the DOS scanout screenshot does
not establish usable-GDI acceptance.
