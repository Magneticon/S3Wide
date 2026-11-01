# Windows 3.11 guest acceptance gate

This plan distinguishes the implemented Win16 selector path from the still
unverified Windows 3.11 Enhanced Mode video-device protocol. The DOSBox-X
RGB332 screenshots and banked framebuffer checks are a separate DOS proof;
they do not validate a Windows DDA or VDD.

## What the current evidence establishes

* Microsoft SDK KB Q131426 documents the kernel-exported low-memory selectors,
  including A000h, and the selector-synthesis alternative. A0000h is below
  1 MiB, so the documented linear base is A0000h with a 64-KiB last-byte limit
  of FFFFh. DPMI INT 31h/0800h is for physical-to-linear mapping above 1 MiB;
  it is not the missing A000h mapping API. The installed Open Watcom
  `h/win/_win386.h` also declares `__A000`. The existing native adapter's
  `AllocSelector` / `PrestoChangoSelector` / `SetSelectorBase` /
  `SetSelectorLimit` path is therefore supported by a public Win16 contract;
  a proprietary DDK is not needed just to create the aperture selector.
* A selector only supplies a pointer to the existing aperture. It does not
  select the S3 mode, grant port access, arbitrate register access with the
  VDD, or ensure that the VDD has not paged/virtualized video memory.
* The Microsoft Windows 3.1 SDK compatibility note Q80901 says the standard
  VGA VDD demand-pages video memory and is incompatible with Super VGA drivers
  that access more than 256 KiB. It directs those drivers to use either the
  adapter vendor's VDD or the included `VDDVGA30.386`. This matters to the
  proposed 1280x720x8 route if it accesses its 921,600-byte framebuffer under
  the standard VGA VDD; it does not establish that every possible 8-bpp path
  requires that VDD, and it does not prevent a stock-VGA boot.
* The native file currently implements selector callbacks only. Bridge mode
  capture/program/restore and bank callbacks are injected; none is wired to
  Windows `Enable`. Open Watcom's `inp`/`outp` implementation emits x86 `in`
  and `out` instructions. That instruction sequence is not a Windows VDD
  ownership or I/O-trap contract.
* The injected `S3_VgaCaptureState`/`S3_VgaRestoreState` path now captures the
  DOS cold-start harness's broader VGA/S3 indexed-register and DAC-palette
  snapshot, verifies restore by readback, and connects through adapters to the
  bridge's retryable Close path. It has host tests, including a dropped DAC
  write. The missing evidence is that a Win3.11 System VM can access the
  registers and the A000h aperture while maintaining VDD/VM switch behavior.

## Exact unresolved interface

The remaining Windows-specific question is how this display driver must
coordinate with the Enhanced Mode VDD while it owns S3 I/O ports and video
memory:

1. Which VDD is configured for the S3 DDA (`VDDVGA30.386` or a vendor VDD),
   and how that VDD handles the S3 banked A000h aperture for the System VM.
2. Whether the display driver must use the INT 2Fh video-driver/VDD 40xx
   handshake (including the documented 4000h/4007h VM-assisted save/restore
   controls), register foreground/background callbacks, or call a VDD-specific
   service. The historical DDK topic lists register save/restore callbacks
   4005h/4006h, but public material alone does not settle the correct callback
   sequence and ABI for this S3 mode. The VGA VDD can virtualize or page the
   same registers and aperture the DDA wants to use.
3. The full saved-state boundary for this particular transition. The mode
   candidate changes Misc Output, sequencer, CRTC/S3 extended registers and
   PLL; the cold-start path also changes GC, AC, DAC mask/palette and packed
   memory mode. The complete before/after state must be captured and restored
   even on a partial programming failure.

The regular Windows DDA entrypoints and the KERNEL selector declarations do
not answer these VDD questions. The exact 40xx behavior can be researched from
an authorized Windows 3.1 DDK copy if available, but the whole DDK is not a
prerequisite for the already documented selector work, register snapshot
implementation, or host tests. A guest run with VDD tracing can resolve the
runtime questions without acquiring proprietary build tools. If a custom VDD
or undocumented callback is required, the minimal DDK input is the authorized
VDD/VGA display-driver sample or its applicable interface notes, not the
entire SDK/compiler archive.

## Smallest concrete dependency

Use the existing Windows 3.11 guest in a disposable DOSBox-X
`svga_s3trio64` VM snapshot, with the S3 model configured for at least 1 MiB
of VRAM. Read-only inventory confirms `WIN386.EXE` is present and
`SYSTEM.INI` selects `display=*vddvga`; exact-name searches under
`C:\CODEX\DOS` and `TOOLS` found neither `VDDVGA30.386` nor
`VDDVGA30.38_`. The pending input is the location of existing Windows 3.1
setup media containing that file, or an authorized S3 vendor VDD and its
interface note. This is a pending prerequisite for validating the documented
large-aperture SVGA path, not a prerequisite for booting Windows with stock
VGA. The currently configured `*vddvga` must not be assumed safe for the
candidate's >256-KiB access. No guest configuration change or candidate-driver
install is authorized.

The current pinned DOSBox-X build and DOS-only tests are useful emulator
evidence, but they do not establish Windows GDI acceptance or resolve VDD
ownership. The Windows guest is present; the required compatible VDD or setup
media remains unlocated. No Windows media is included in this project.

CODEX reports that a byte-verified disposable copy booted to Program Manager
in Enhanced Mode using `VGA.DRV` and `*VDDVGA` at 640x480. Its config had
`vmemsize=-1`, but the resolved emulated VRAM capacity was not recorded. This
confirms only the stock-VGA baseline; it is not a custom-driver or GDI result.

## Runnable acceptance sequence

Run the authorized baseline only in a disposable copy of the existing guest.
Leave the original image and its configuration unchanged; do not install the
candidate driver or alter `SYSTEM.INI` during this baseline.

1. **Baseline the existing guest.** Boot Windows 3.11 with its current stock
   VGA setup (`display=*vddvga`) and observe whether it reaches the desktop.
   Record the reported Windows version and Enhanced Mode status, the actual
   stock display driver and displayed resolution/color depth, the exact DOSBox-X
   machine/S3/`vmemsize` settings, and the `[boot]` `display.drv`/`386Grabber`
   plus `[386Enh]` `display` entries from the copied guest's `SYSTEM.INI`.
   Record presence/version of the active VDD if observable, and retain a
   baseline screenshot and boot log. A successful stock-VGA boot establishes
   only that this existing guest baseline works; it does not test the custom
   DDA, >256-KiB access, or VDD port/memory ownership.
2. **Assess the candidate VDD route without changing the guest.** The current
   inventory reports neither `VDDVGA30.386` nor a vendor VDD. Confirm the exact
   active VDD from the guest configuration and any available read-only runtime
   evidence. Q80901's limit matters if the candidate accesses more than 256 KiB
   while the standard VGA VDD is active; the 1280x720x8 frame is 921,600 bytes.
   If that is the access model, keep the candidate hardware path blocked until
   an authorized compatible VDD route and its interface are established. This
   does not block the stock-VGA baseline or prove that all 8-bpp paths are
   impossible.
3. **Run a loader-only probe in the existing stock-VGA desktop.** From the
   project root, build with
   `WATCOM=/path/to/open-watcom-v2 OWBIN=binl64 ./tests/win16/build-loader-probe.sh`.
   Stage `build/s3wide_driver.dll` as `S3WIDE.DRV` and `build/S3PROBE.EXE` in
   a folder mounted as `D:` in the disposable DOSBox-X guest. Confirm that
   mount in the VM config before running these commands; do not infer a host
   path or alter `SYSTEM.INI`:

   ```dos
   MD C:\S3TEST
   COPY D:\S3WIDE.DRV C:\S3TEST\S3WIDE.DRV
   COPY D:\S3PROBE.EXE C:\S3TEST\S3PROBE.EXE
   C:\S3TEST\S3PROBE.EXE
   ```

   The probe loads the file as an ordinary Win16 module, resolves DDA ordinals
   5 and 450, checks the 110-byte `Enable(InquireInfo)` result and 1280x720x8
   fields, checks resource ID 7 is preserved, and calls device `Enable` with a
   dummy PDEV expecting 0. It never selects the driver in Windows, initializes
   the native S3 backend, or performs I/O. Expected result: a PASS dialog with
   those four checks; record the exact DLL/probe hashes and any Windows error.
   On any loader error, crash, protection fault, unexpected GDIINFO, missing
   ordinal, or nonzero device-Enable return, stop and discard the disposable
   copy. Remove `C:\S3TEST` afterward or discard the whole copy. A pass proves
   only module loading and the current fail-closed ABI behavior; it does not
   retest the earlier `DisplayResources`/USER/font startup stall, attach a GDI
   surface, or establish graphics output. The user-mode probe source has been
   cross-built, but it has not been run in Windows.
4. **Repeat USER/resource loading without touching hardware.** A later
   diagnostic-driver boot still requires explicit authorization to install a
   diagnostic variant in a disposable copy. Its device `Enable` must remain
   fail-closed. Capture DDA `Enable` style calls, `GetDriverResourceID`,
   `RealizeObject` requests, and USER/font progress. Confirm USER falls back
   to system fonts and reaches the desktop. Restore stock VGA if startup
   stalls. This is loader/resource evidence, not display-mode acceptance.
5. **Resolve the VDD/port handshake in the guest.** In the disposable VM,
   instrument the exact documented 40xx request/callbacks before connecting
   register writes. Log carry/return values and callback order. Verify a
   reversible register read and selector setup, then a reversible single
   aperture write/read while the desktop is quiesced. Never treat successful
   `in`/`out` instruction execution by itself as proof of correct VDD
   ownership. Do not continue if a request is unsupported, returns an
   ambiguous result, or the VM reports a protection fault.
6. **Verify mode rollback before enabling GDI.** Using a test build still
   restricted to the disposable VM, record the complete VGA/S3 register state,
   program 1280x720x8, validate 1280-byte pitch and bank selection 0–14, then
   force a controlled error. Compare the restored state with the baseline and
   reboot. Repeat successful Disable/restore and a Windows foreground switch
   before attaching the GDI surface.
7. **Run real 8-bpp GDI checks.** After the VDD and rollback gates pass, boot
   the driver and verify 1280x720 at 256 colors. Exercise solid fills, clipped
   lines, `Pixel`, top-down `BitBlt`, translated banded `SetDIBitsToDevice`,
   palette/color conversion, system text rendering, and bank crossings. Save
   a guest screenshot and compare its pixels with the expected RGB332 test
   pattern; retain USER/font logs and the guest's mode report. Then test a
   DOS window, background VM switch, and return to the Windows desktop.
8. **Treat 16M-color as a later gate.** Separately implement and test the
   required 24-bpp/16M-color mode, GDIINFO/color contract, pitch, palette
   behavior, and drawing paths. Do not infer this result from the 8-bpp test.

The DOSBox-X DOS bitmap remains separate evidence throughout. No Windows GDI
acceptance is complete until the guest log, mode report, pixel-checked Windows
screenshot, rollback result, and USER/font startup result are saved together.

## References

* Microsoft SDK 3.0/3.1 KB [Q131426, How to Access Physical Memory in 16-bit
  Windows-Based Apps](https://jeffpar.github.io/kbarchive/kb/131/Q131426/).
* Microsoft SDK 3.1 KB [Q80901, Windows 3.1 Application Compatibility
  (part 6 of 7)](https://jeffpar.github.io/kbarchive/kb/080/Q80901/).
* Microsoft SDK KB [Q132005, AllocSelector and FreeSelector
  semantics](https://jeffpar.github.io/kbarchive/kb/132/Q132005/), and
  [Q89560, FreeSelector failure return](https://jeffpar.github.io/kbarchive/kb/089/Q89560/).
* The Open Watcom Win16 declarations installed with this build are in
  `h/win/win16.h` and `h/win/_win386.h`; the latter declares `__A000`.
* The Windows 3.1 DDA documentation is the archived [Windows Drivers
  Overview](https://dos-help.soulsphere.org/ddag31qh.hlp/Windows_Drivers_Overview.html).
* The [OS/2 Museum analysis of Windows 3.x VDDVGA](https://www.os2museum.com/wp/windows-3-x-vddvga/)
  is useful secondary context on the incompletely documented VDD behavior;
  it is not treated as the normative API definition.
