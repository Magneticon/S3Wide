# Recovery status (2026-10-09)

## Source recovery

The designated private `TheKernelDoc/S3` repository was inspected read-only.
Its visible `main` branch tip was
`d77dfee19aa9cbcd48d3d479a0b652f178121712`; the available tree contained
bootstrap documentation/scripts only. It did not contain the custom driver
source, the `DisplayResources`/USER stall trace, or build scripts. The earlier
native workspace paths were absent. This independent recovery tree does not
modify the private repository or Atalanta.

The 2026-10-09 read-only guest inventory confirms a Windows 3.11 guest: its
`WINVER.EXE` reports 3.11 and `WINDOWS\SYSTEM\WIN386.EXE` is present. Its
`SYSTEM.INI` currently selects `display=*vddvga`. Exact-name searches under
`C:\CODEX\DOS` and `TOOLS` found neither `VDDVGA30.386` nor
`VDDVGA30.38_`. No guest configuration change is authorized. The pending input
is the location of existing setup media containing the VDD or an authorized S3
vendor VDD; no VDD behavior or successful Windows startup is inferred from this
inventory.


CODEX reports that a byte-verified disposable copy booted to Program Manager
in Enhanced Mode with `VGA.DRV` and `*VDDVGA` at 640x480. The copied config had
`vmemsize=-1`; the resolved emulated VRAM capacity was not recorded. This is
the stock-VGA baseline only. It does not establish candidate-driver startup,
large-framebuffer access, or Windows GDI operation.

## Current Win16 source milestone

The injected bridge now has a concrete host-testable full VGA/S3 save/restore
adapter (`S3_VgaCaptureState`, `S3_VgaRestoreState`): it captures the same
sequencer, standard/extended CRTC, GC, AC, DAC mask, and palette banks as the
cold DOS harness. Bridge callbacks serialize the typed state into its reserved
1024-byte slot and verify complete restoration. A forced DAC write loss fails
readback and preserves retryability; the follow-up restore passes. This adapter
does not provide native port I/O and is not connected to `Enable`.

The local driver has Win16 ABI declarations for the 110-byte `GDIINFO`,
32-byte `PDEVICE`, 32-byte `PBITMAP`, 40-byte `BITMAPINFOHEADER`, and required
DDA exports. It implements narrow host-tested 8-bpp operations: `BitBlt`
SRCCOPY from a one-segment top-down PBITMAP, `Pixel` get/copy-pen write, and
ordinal-21 `SetDIBitsToDevice` for bottom-up, DWORD-padded 8-bpp `BI_RGB`
bands translated through the supplied palette table. Other formats, ROPs,
segmented PBITMAP sources, invalid translation tables, and DIB bands crossing
a far segment are declined. It now also exports ordinal-10 `RealizeObject`,
which consumes the 10-byte Win16 `LOGPEN` and returns a 12-byte driver-owned
pen format, and handles `Output(OS_POLYLINE)` for
solid one-pixel `R2_COPYPEN` lines. Segments include their first pixel and
exclude their endpoint; the DDA clips to framebuffer bounds and ignores
`lpClipRect` because `dpClip` is zero. The reusable line core also supports an
optional half-open clip rectangle. Failed bank mapping returns failure and may
leave a prefix drawn.
Other `Output` styles/ROP2 values request GDI simulation. GDI drawing capability
flags remain zero.

The host reference sends a synthetic 1280x720 DIB in 32-scan bands, checks all
921,600 pixels, tests palette translation, and exercises the 64-KiB bank and
segment boundaries. The polyline tests compare 256 clipped segments against a
flat Bresenham reference, check endpoint and ROP fallback semantics, verify
that `Output` ignores `lpClipRect` when `dpClip` is zero, and exercise a bank
failure during a span. These are software and ABI checks only.
The Win16 DLL cross-build passes, but it has not been loaded in Windows.
`EnableDevice` deliberately fails; no physical or software surface is attached
and no scanout/GDI acceptance is claimed.

The resource and lifecycle defects found in the acceptance audit are addressed
at source level. The driver now contains a Win16 `VERSIONINFO` RT_VERSION
resource ID 1 with fixed file metadata identifying a display driver. The
synthetic `OEMBIN` ID 3 and `FONTS` records, which both pointed at fake font
bytes, were removed. This driver supplies no device fonts: its `RealizeObject`
handles pens and returns zero for other object types so GDI can realize system
fonts. `Disable` is exported at DDA ordinal 6.

`S3_DeviceEnableWithOps` connects the established bridge to PDEV initialization
and the banked `S3_SURFACE`. A failed mode setup retains the bridge state when
rollback fails so `Disable` can retry it. A failed selector release leaves the
PDEV surface pointer detached and makes the cached surface window inert until
cleanup succeeds. Runtime initialization preserves live recovery ownership;
Disable retry does not dereference the saved PDEV, and the current Disable call
detaches its matching PDEV before attempting cleanup. Host tests cover success, rollback
retry, bank-callback routing, and failed free retry. This is an injectable
lifecycle implementation only: native
`Enable` still returns failure for device enable and does not call the helper
until a verified Windows mode/I/O provider exists. No unsupported drawing
capability is advertised.

On 2026-10-09, `WATCOM=/tmp/ow-linux-x64 OWBIN=binl64 ./build.sh` passed the
Win16 build, NE export/resource checks, and host checks. The inspector verifies
the required entrypoint ordinals including `Disable`, a sole numeric
RT_VERSION resource ID 1, its VS_FIXEDFILEINFO display-driver type, and the
absence of the synthetic font/OEMBIN resources. These are static/build and host
checks, not Windows guest validation.

The DOS mode helper is not wholly untested: the cold VGA/S3 DOSBox-X harness
has already called the production timing/PLL, palette, and bank helpers and
confirmed a visible 1280x720 RGB332 scanout, register rollback, and complete
banked-frame readback. The harness supplies the cold VGA SEQ/GC/AC initialization
around those helpers. The low-memory A000h aperture selector is supported by
the Microsoft Win16 contract and the installed Open Watcom `_win386.h`; its
selector implementation does not require proprietary DDK material. The actual
integration gap is Enhanced Mode VDD ownership and port-I/O coordination.
Microsoft SDK KB Q80901 describes a conditional compatibility limit: Windows
3.1's standard VGA VDD demand-pages video memory and is incompatible with an
SVGA display driver that must access more than 256 KiB. It names the adapter
vendor VDD or included `VDDVGA30.386` as supported alternatives. That applies
to the proposed 921,600-byte 1280x720x8 framebuffer path if it accesses that
memory under the standard VDD; it does not prevent a stock-VGA baseline boot
and does not prove every possible 8-bpp integration path unusable. The current
guest inventory locates neither alternative, so the documented >256-KiB VDD
route remains unresolved.
No VDD handshake, mode/bank provider, or port operation is connected to
`Enable`. A new injected full VGA/S3 state module captures and verifies the
DOS cold-start harness's SEQ/GC/AC/DAC/S3 state; host tests verify bridge
serialization, restore readback, and recovery after a dropped DAC write. It
must still be executed under a verified Windows VDD/port ownership protocol.
The guest must then boot through
USER/font initialization and pass real 8-bpp drawing before `Enable` can
safely attach the surface. The DOS result is separate evidence and is not
Windows GDI acceptance.

The public Win16 selector contract is now cross-checked against the installed
Open Watcom `win16.h`/`windows.lib` and the archived Microsoft SDK 3.1 KB
selector and physical-memory notes. The 64-KiB A0000h window is below 1 MiB, so
the documented low-memory path sets a selector's linear base directly to
A0000h and its last-byte limit to FFFFh; DPMI physical-to-linear mapping is
reserved for device addresses above 1 MiB. A fail-closed selector/mode/bank
bridge is implemented behind injected operations. Host tests cover selector
setup/validation, bank callback failures, mode restore, and retryable cleanup
failures. The native KERNEL selector adapter is linked and its imports are
checked, but it is not called by the driver. No port-I/O, mode, or bank
provider is connected. The documented Windows 3.1 compatibility note calls for
`VDDVGA30.386` or a vendor VDD when an SVGA driver accesses over 256 KiB under
the standard VGA VDD; neither has been located in the known inputs. A stock
VGA baseline can still be run under the current `*vddvga` setting. VDD/port
ownership for the candidate remains unverified;
see [`win16-protected-mode-bridge.md`](win16-protected-mode-bridge.md) and the
[`Windows guest acceptance plan`](windows-guest-acceptance-plan.md).

## DOSBox-X route and result

The saved environment had a read-only DOSBox-X source clone at
`/tmp/dosbox-x-ref`, commit
`b60936a716c72e0978e940f813bab2cd40d43868`. The clone's origin is the
official `https://github.com/joncampbell123/dosbox-x.git`; its working tree
remains clean. The latest official upstream release tag is
`dosbox-x-v2026.10.01`; its published assets did not include a Linux binary.
I built that pinned upstream source in a separate `/tmp/dosbox-x-build`
checkout, leaving the original clone untouched. The source-built executable
reports DOSBox-X 2026.10.01 SDL2. It is SHA-256
`838898af8d189e3bc4b9000793005477e7e9a0a88d8e4a6227007c98fef6b306`.

Build tools and development/runtime packages came from official Debian
Trixie main/security repositories with apt metadata signature checking. They
were downloaded and unpacked under `/tmp/dosbox-local`; no system package
installation, privilege escalation, display-server install, or security
setting change was used. Build parallelism was limited to `make -j2`. The
release build configuration and run command are recorded in
[`tests/dosbox-x/README.md`](../tests/dosbox-x/README.md).

The environment still has no graphical display server. There is no bundled
copy of licensed Windows media, and none was sought or fetched.

The pinned source defines VBE mode `0x23E` as 1280x720x8 with a 64-KiB A0000h
window. The standalone `VBECHK.EXE` was cross-built with Open Watcom, then run
against the source-built emulator in a bounded SDL dummy video/audio session.
The exact config is
[`dosbox-x-vbe-headless.conf`](../tests/dosbox-x/dosbox-x-vbe-headless.conf).
DOSBox-X used its built-in DOS shell; no Windows or DOS disk image was needed.

The emulator completed with exit status 0. DOS console output reports mode
`1280x720x8`, pitch 1280, a 921,600-byte frame, and a 64-KiB bank window with
64-KiB granularity, followed by a full banked write/readback PASS. The guest
also wrote [`VBERES.TXT`](../tests/dosbox-x/VBERES.TXT). The complete emulator
log is preserved at
[`dosbox-x-vbe-headless.log`](dosbox-x-vbe-headless.log). Source, build, and
binary provenance are in [`dosbox-x-build.txt`](dosbox-x-build.txt). SDL dummy
video has no visible display, so this run produced no screenshot and does not
verify visible pixels at a host display surface.

That test establishes DOSBox-X's VBE BIOS mode info, mode selection, and
banked framebuffer memory baseline. It does not establish visible scanout or
by itself validate the driver's raw S3 timing/PLL candidate.

The next bounded gate compiled `tests/dosbox-x/raw_s3_smoke.c` with the actual
`src/s3_hw.c` and ran it in the same pinned emulator. It set the BIOS VBE mode
as a register/memory reference, captured S3 state, and verified full-frame
RGB332 write/readback through BIOS banks 0–14. The harness then called the
driver's raw palette, `S3_Program720p8bppCandidate`, and
`S3_Select64KBank` routines. The candidate read back the expected raw timing
bytes and PLL tuple; the 1280-byte pitch matched VBE at CR13=`0xA0`,
CR51[5:4]=`00`. (This emulator comparison caught and corrected a previous
candidate bug that wrote `0x20` into CR51[5:4].) The bank helper selected
banks 0–14 with matching CR35/CR51 fields and completed 921,600-byte pattern
write/readback. Bank 64 was rejected. A deliberately injected one-shot CR00
readback fault caused the production candidate to fail closed and restore the
entire captured register snapshot exactly.

The run log is preserved at
[`dosbox-x-s3-raw-headless.log`](dosbox-x-s3-raw-headless.log), the guest
summary at [`S3RAW.TXT`](../tests/dosbox-x/S3RAW.TXT), and the executable,
configuration, and harness source under `tests/dosbox-x/`. This is a
bounded raw-helper result: that initial harness begins with BIOS mode setup
and does not validate cold graphics/attribute-controller state or Windows
GDI. The later cold-mode candidate now writes CR42=`0x00`, as documented by
the original Trio32/64 datasheet. The rebuilt VBE-seeded raw test passes its
timing/pitch, bank, and rollback gates. Its corrected candidate capture and a
delayed repeat are 1280x720 and pixel-identical to the VBE reference and cold
candidate; keep `EnableDevice` fail-closed pending Windows guest/GDI
acceptance.

### Cold VGA/S3 candidate gate

`raw_s3_cold_smoke.c` starts from BIOS VGA text mode 3 and makes no VBE calls.
It records Misc Output, sequencer indexes 00–04/08/10–13/15, CRTC 00–18,
unlocked S3 CRTC 30–6A, GC 00–08, AC 00–14, DAC mask, and all 768 DAC
components. A one-shot CR00 readback fault exercises candidate rollback;
the tracked pre/post register snapshots match. It then installs packed 8-bpp
SEQ/GC/AC state and the driver's 6-bit palette, confirms CR13=`0xA0`,
CR51[5:4]=`00`, CR50=`0xC0`, Misc clock selector 3 and SR12/SR13=`0x23/0x32`,
and validates the driver bank fields plus full 921,600-byte VRAM write/readback
across banks 0–14. DOSBox-X exited with status 0 and guest output reports
`RESULT PASS` for register/memory proof. The full tracked dump is
[`dosbox-x-s3-cold-register-dump.txt`](dosbox-x-s3-cold-register-dump.txt),
the emulator run log is
[`dosbox-x-s3-cold-headless.log`](dosbox-x-s3-cold-headless.log), and the
screenshots include the readable
[`text-mode baseline`](dosbox-x-s3-cold-text-baseline.png), preserved original
[`all-black capture`](dosbox-x-s3-cold-black-scanout.png), and latest
[`visible RGB332 pattern`](dosbox-x-s3-cold-visible-pattern.png).

Pinned DOSBox-X source resolves CR42 behavior for this emulator: the CR42
write comment labels bits 0–3 as DCLK select when Misc Output selects clock 3,
but the read handler returns hardcoded `0x0D`, and `SVGA_S3_GetClock()` selects
the PLL from Misc Output bits 2–3 and ignores CR42. The exercised PLL path is
therefore Misc clock 3 and the SR12/SR13 tuple, not a validated CR42 nibble.
The original S3 Trio32/64 datasheet confirms CR42 bit 5 is interlace and bits
4–0 are reserved (printed page 16-2, PDF page 195); Table 11-3 also specifies
bit 5 as non-interlaced/interlaced mode selection (PDF page 93). The PDF was
retrieved from the [Bitsavers S3 component archive](https://bitsavers.org/components/s3/DB014-B_Trio32_Trio64_Graphics_Accelerators_Mar1995.pdf)
and its SHA-256 is `7528eefe26846da697541ccab575610dad3eaa7ab7de0ac7147d02bd2ddf4277`.
This manufacturer register description conflicts with the DOSBox-X write
comment. The candidate now writes CR42 zero (interlace off and reserved bits
zero) and no longer validates readback, since the emulator hardcodes that
value. Hardware CR42 semantics are resolved.

The screenshot path first exposed a diagnostic side effect: AC palette-index
reads clear attribute-controller display enable. The harness now writes AC
index `0x20` after each register snapshot and before requesting the screenshot.
The all-black frame is preserved. Separate controlled tests found that CR09
low bits `0x0F` repeated graphics rows and cold VGA text state lacked CR3A[4],
the S3 selector for 8/16/24/32-bpp modes. Those are fixed: CR09=`0x60`, CR3A[4]
set.

The last visible defect was 613 rows and a missing top green band. In the
pinned DOSBox-X `vga_draw.cpp`, vertical blank start is CR15 plus CR09[5]
shifted into bit 9. Candidate CR15=`0xD0` and CR09=`0x60` therefore start blank
at line 720. CR16=`0x6A` made the emulator's end-blank calculation wrap to line
875 against VTotal 746, yielding `vblank_skip=875 & 0x7F = 107`; its renderer
then reduced VDE 720 to 613. CR16=`0xFF` ends at line 768 and gives
`vblank_skip=768 & 0x7F = 0`. CR07/CR12 still encode VDE 720; CR0C/CR0D and
CR51[1:0] leave display start at zero; CR07/CR09/CR18 set line compare to
1023, beyond active display; CR10/CR11 place vertical retrace after VDE. The
capture now uses the whole 1280×720 surface.

`CAPTURE_IMAGE` is triggered through DOSBox-X's integration screenshot
register after pattern write and bank checks; the guest polls until capture
status clears. The renderer calls `CAPTURE_AddImage` at `RENDER_EndUpdate` with
the full source dimensions. AC display enable is restored before this trigger.
VBE baseline, VBE-seeded candidate, and cold candidate captures are all
1280×720, contain all 64 expected colors, and match every pixel of the RGB332
pattern. Their current SHA-256 is
`459a55e93a9a27135041bbee602266fbc67ac43585dc27572dc0b1a0c5622846`. The
host-side pixel checker is `tests/dosbox-x/check_s3_scanout.py`; image details
are in [`dosbox-x-s3-scanout-image-checks.txt`](dosbox-x-s3-scanout-image-checks.txt).
Pre-CR16 613-row captures are retained for diagnosis.

Cold rollback including CR3A and CR16, register checks, all 15 banks, and
921,600-byte framebuffer readback pass. The VBE-seeded timing/pitch assertion,
bank check, and forced rollback pass as well. `EnableDevice` remains
fail-closed pending Windows guest/GDI acceptance.

## Blocked acceptance and next inputs

The Windows-dependent unit remains blocked. No Windows 3.11 guest, authorized
DDK assets from the lost build, or original USER/font trace are available, so
the DLL cannot be tested through USER, font loading, or actual GDI. No Windows
media was obtained. The next finite gates are:

1. Reconcile the VBE-seeded candidate timing assertion and its aspect-adjusted
   screenshot with the cold-start 720-line scanout.
2. With an authorized Windows 3.11 guest, run the documented driver-load,
   USER/font, 1280x720 scanout, 8-bit GDI drawing, and eventual 16M-colour
   GDI checks. The DOS bitmap/VBE screenshot remains a separate proof.

The DLL and host test suite are source-level progress, not guest acceptance.
No S3 repository push or publication has occurred.

The isolated Win16 bridge cleanup audit and current host build are recorded in
[`win16-protected-mode-bridge.md`](win16-protected-mode-bridge.md). The linked
selector adapter now maps the verified `FreeSelector` failure convention and
restores the allocation-time selector limit before freeing. The Windows 3.1
display-driver port-I/O mediation contract remains unverified; therefore no
native I/O wiring or Windows guest acceptance is claimed.
