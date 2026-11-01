# Windows-host build and stock VGA baseline (1 November 2026)

First runs on the Windows workstation after recovering the v23 source
(`recovery/20261101-v23-source`, commit `4d178fb`). This is M0 evidence: a
reproducible build and a known-good stock display baseline. It is not a
custom-driver or 1280x720 result.

## Build

Tools found on this machine:

| Tool | Location | Identity |
|---|---|---|
| Open Watcom C 16-bit | `C:\WATCOM\binnt64` | 2.0 beta, built Sep 28 2026 |
| Watcom Win16 import library | `C:\WATCOM\lib286\win\windows.lib` | SHA-256 `929c5d4e7d47c2c259e54a9ee3812f8db2dc58a5d2ff8cd5d78486db97fbb371` |
| Host C compiler | `C:\clang+llvm-18.1.8-x86_64-pc-windows-msvc` | clang 18.1.8 |
| Python | `python` on PATH | 3.11.9 |

This Watcom tree is not the Linux `ow-linux-x64` package used historically,
and it is not established to match the October 10 archive
(`f047f68a...`). `build.sh` now accepts `CC`, `PYTHON` and Windows-hosted
`OWBIN`, so it runs unchanged on both hosts:

```sh
WATCOM=C:/WATCOM OWBIN=binnt64 \
CC=/c/clang+llvm-18.1.8-x86_64-pc-windows-msvc/bin/clang.exe PYTHON=python \
sh ./build.sh
```

Results:

- The unmodified v23 source links to a 21,742-byte NE16 DLL whose
  `inspect_ne.py` report is identical to the historical v23 build: exports
  1, 2, 5, 6, 8, 9, 10, 21, 450; imports KERNEL
  `[3, 4, 175, 176, 177, 186, 187, 188, 189]` and USER.
- That DLL differs from the historical candidate
  (`3902af42...`) in 3 bytes, all inside the VERSIONINFO resource just
  before `StringFileInfo`. Code and data are byte-identical. The same bytes
  change between local rebuilds, so DLL hashes are not stable across builds.
- clang `-Wconstant-conversion` rejected `dpMHiWin = (33900, 19100)` in
  `src/s3_gdiinfo.c`: 33900 does not fit the signed 16-bit GDIINFO field and
  wraps to -31636. The pair is now `(16950, 9550)` / `(640, -360)`, exactly
  half, preserving the low-metric ratio. New host checks require positive
  window extents and the 10:1 metric high/low ratio; they fail against the
  old values.
- `build.sh` completes with `-Werror` and the full host suite passes.

## Stock VGA baseline run

Reproduce with `tests/win16/run-display-baseline.sh RUN_DIR` after building
`build/DISPBASE.EXE` (`tests/win16/build-display-baseline-probe.sh`).

| Field | Value |
|---|---|
| Source tree | this commit |
| Display driver | stock `vga.drv` / `vga.3gr` from the guest (not a project build) |
| Probe | `DISPBASE.EXE`, SHA-256 `6699264c0c4d24ca48300382b84f6b3a271a06d085ec600043eb84889641ccca` |
| DOSBox-X | 2026.08.31, commit 4f19017, Visual Studio SDL2 64-bit; `dosbox-x_x64_SDL2.exe` SHA-256 `f99fa845f0c3ec3c60ebfcd3671e023a8a06c289ac34a0de54991922061a8547`; SDL dummy video and audio |
| Config | [`baseline.conf`](baseline.conf): `svga_s3trio64`, 4 MiB VRAM, 32 MiB RAM |
| Guest | disposable copy of `C:\DOS` (277 files; identical to `C:\CODEX\DOS`) |
| Windows | Windows for Workgroups 3.11, Enhanced Mode (`GetWinFlags=0x0C29`) |
| Observed mode | 640x480, 4 planes x 1 bit, 16 colors (`GetDeviceCaps`, `GetSystemMetrics`) |
| Boot | success; probe ran, `ExitWindows` returned to DOS, emulator exit 0 in ~11 s |
| Rendering | gray desktop with minimized Program Manager; no defects seen |
| Acceleration | not applicable (stock VGA) |
| Screenshot | [`screen.png`](screen.png), GDI readback by the probe |

Two independent runs produced identical `RESULT.TXT` and byte-identical
screenshots. Both source guests (`C:\DOS`, `C:\CODEX\DOS`) were hashed
before and after and did not change. In the disposable copy Windows rewrote
`PROGMAN.INI` and removed `WIN386.SWP` ([`guest-changes.diff`](guest-changes.diff)).

[`dosbox.log`](dosbox.log) contains Windows' normal startup probes
(INT 10h `6F00` Video7 check, INT 5Ch NetBIOS, INT 68h debugger check) and
`SDA drive` messages during shutdown; none stopped the run.

## Limits

- The screenshot is read back through GDI and the active display driver. It
  is not an emulator-side scanout capture, and it does not prove what
  DOSBox-X displayed. The DOSBox-X integration device's VGA self-capture was
  removed upstream, so an emulator-side capture still needs another route.
- The guest has no stock S3 Windows driver; only VGA was tested.
- No custom driver was installed or loaded.
