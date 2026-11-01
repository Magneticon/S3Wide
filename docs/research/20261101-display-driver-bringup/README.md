# Display-driver bring-up: from load failure to the GDI resource gate (1 November 2026)

Follows [the stock VGA baseline](../20261101-stock-vga-baseline/README.md).
All guest runs used `tests/win16/run-display-baseline.sh` on disposable
copies of `C:\DOS`; the source guest was hashed before and after every run
and never changed. No Windows GDI acceptance is claimed.

## 1. Loader probe on the current build

`S3AUTO.EXE` (from `tests/win16/s3_loader_probe_autonomous.c`, adapted from
the 9 October harness) loads `C:\S3TEST\S3WIDE.DRV` with `LoadLibrary` in
Enhanced Mode. All checks pass on the current driver, including a new
in-guest check that the corrected `dpMHiWin` extents are positive
([`loader-probe-RESULT.TXT`](loader-probe-RESULT.TXT)). Device Enable still
returns 0 by design.

## 2. Emulator-side capture

DOSBox-X has no command-line or guest-triggered screenshot (the integration
device's VGA self-capture was removed upstream). With `VISIBLE=1` the runner
now opens a real emulator window and `tests/win16/capture-window.ps1`
captures its client area with `PrintWindow` once `DISPBASE.EXE` signals
`READY.TXT`. Both processes are DPI aware (`dpi aware=true`; this host runs
at 150 %), so the capture is 1:1 with the guest mode.

On the stock VGA desktop, after mapping each GDI logical colour to the DAC
colour DOSBox-X displays, the GDI readback and the emulator window differ in
exactly 126 pixels, all in (320,240)-(331,259): the mouse cursor, which the
emulator shows and GDI readback excludes
([`stock-vga-compare.txt`](stock-vga-compare.txt),
[`stock-vga-diff-mask.png`](stock-vga-diff-mask.png)). The capture route is
therefore usable for acceptance screenshots.

## 3. `display.drv=s3wide.drv` boot attempts

Overlay: `WINDOWS\SYSTEM\S3WIDE.DRV` plus `SYSTEM.INI` with only
`[boot] display.drv` changed; Windows started with `win /b`.

### 3a. Driver rejected: stray USER import

[`bootlog-s3wide-display-boot.txt`](bootlog-s3wide-display-boot.txt):

```
LoadStart = s3wide.drv
LoadStart = USER.DLL
LoadFail = USER.DLL Failure code is 02
LoadFail = s3wide.drv Failure code is 02
```

Display drivers load right after `mouse.drv`, before GDI and USER. The
driver imported `USER.1` (`MESSAGEBOX`), so the loader looked for a
`USER.DLL` file and failed. The import came from Open Watcom stack probes:
every function called `__STK`, whose overflow path reaches
`__fatal_runtime_error` and `MessageBox`. With `-zu` the driver runs on the
caller's stack, so those probes were also checking the wrong stack. The
driver is now compiled with `-s`; it imports KERNEL only, as the stock
`VGA.DRV` imports only KERNEL and KEYBOARD.

This corrects the handoff's "target raw normal-driver import" set: USER
ordinal 1 was a build artifact, not a requirement. `inspect_ne.py` now fails
the build if any module other than KERNEL is imported.

### 3b. Driver loads; GDI fails without calling Enable

[`bootlog-s3wide-display-boot-r2.txt`](bootlog-s3wide-display-boot-r2.txt):
`LoadSuccess = s3wide.drv`, then sound, comm and fonts load, then
`LoadFail = GDI.EXE Failure code is 00`.

A trace build (`S3_TRACE=1 ./build.sh`, `src/s3_trace.c`) appends every DDA
entry to `C:\S3TRACE.TXT` with KERNEL file calls only. It showed GDI never
called Enable: only `LibMain` and `WEP`
([`s3trace-s3wide-trace-boot.txt`](s3trace-s3wide-trace-boot.txt)).

Comparing with the stock driver's export table on this guest
([`stock-vga-drv-interface.txt`](stock-vga-drv-interface.txt)) showed the
link layout was wrong:

| | stock `VGA.DRV` | S3WIDE before |
|---|---|---|
| module name | `DISPLAY` | `S3WIDE_D` |
| ordinal 3 | Control | WEP |
| ordinal 4 | Disable | LibMain |
| ordinal 6 | EnumDFonts | Disable |

`src/driver_smoke.lnk` now sets `modname=DISPLAY`, a stock-style
`DISPLAY : 100, 96, 96 : ...` description, Disable at 4, and no WEP or
LibMain exports. Loading by path in the loader probe still works.

### 3c. Current gate: GDI asks for driver resources

With the corrected module name GDI binds the driver and calls
`GetDriverResourceID(3, "oembin")`, then fails
([`s3trace-s3wide-trace-boot-r3.txt`](s3trace-s3wide-trace-boot-r3.txt);
`6F65 6D62` is `"oemb"`). The driver has only a VERSIONINFO resource.

The stock driver carries (types and IDs only, from the table above):
`OEMBIN` 1 (112 bytes), `OEMBIN` 2 (208 bytes), `OEMBIN` `FONTS`
(80 bytes), 11 cursors and their group entries, 5 icons and their groups,
about 40 OEM bitmaps (`RT_BITMAP` 32734-32767), and `STRINGS`. It does not
export `GetDriverResourceID`, so GDI presumably uses defaults for it; ours
returns the requested ID unchanged, and `oembin` 3 does not exist.

## Next steps

1. Define the `OEMBIN` layouts (GDI configuration, colour table, font
   mapping) from public Windows 3.1 DDK documentation and author our own
   resources. Microsoft's resource data must not be copied into the
   repository.
2. Author original cursors, icons and OEM bitmaps, or decide on a local-only
   build that takes them from the user's licensed `VGA.DRV` for testing.
3. Add the remaining DDA exports (Control, EnumDFonts, EnumObj, StrBlt,
   ScanLR, DeviceMode, ExtTextOut, GetCharWidth, DeviceBitmap, FastBorder,
   SetAttribute, DeviceBitmapBits, CreateBitmap, Inquire, SetCursor,
   MoveCursor, CheckCursor) with verified Pascal signatures, traced and
   fail-closed.
4. Only then revisit device Enable and the Enhanced Mode VDD gate.

## Not attempted

- VBESVGA as a 1280x720 oracle: the local checkout has source only; building
  needs the Windows 3.1 DDK and MASM toolchain, which are not on this
  machine. Its reuse rights are unestablished, so no release binary was
  downloaded.
