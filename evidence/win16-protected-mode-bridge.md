# Win16 protected-mode bridge boundary

## Contracts verified

The S3 CPU aperture is 64 KiB at physical A0000h, entirely below 1 MiB. The
Microsoft Windows 3.1 SDK knowledge-base material describes making a linear
address below 1 MiB from `segment * 16 + offset`, then building a far pointer
with a copied code selector converted to a data selector. It describes
`AllocSelector`, `PrestoChangoSelector`, `SetSelectorBase`, and
`SetSelectorLimit`, says allocated selectors must be freed, and defines the
selector limit as the last byte offset. For this aperture, that limit is
FFFFh. The DPMI 0800 physical-to-linear service is specifically used for
physical addresses at or above 1 MiB; it is not the mapping call for A0000h.
See Microsoft's [Q131426](https://jeffpar.github.io/kbarchive/kb/131/Q131426/)
and [Q132005](https://jeffpar.github.io/kbarchive/kb/132/Q132005/).

Open Watcom's installed Win16 `win16.h` declares the KERNEL APIs
`AllocSelector`, `PrestoChangoSelector`, `SetSelectorBase`,
`SetSelectorLimit`, `GetSelectorBase`, `GetSelectorLimit`, and `FreeSelector`.
The installed Win16 `windows.lib` import index contains those names. The public
declarations verify prototypes and available imports; they do not
prove that linear A0000h is accessible in this specific Windows guest.
Microsoft's Q89560 SDK article shows that a nonzero `FreeSelector` return
means the selector was not freed, so the adapter reports success only when
`FreeSelector(selector) == 0`.

## Implemented isolated bridge

`S3_Win16BridgeOpen` accepts only a one-selector, 64-KiB window and requires
injected operations for code-selector acquisition, selector allocate/convert,
base/limit configuration and readback, far-window creation, mode capture and
programming, and bank 0 selection. It checks that selector base and limit read
back as requested before capturing or changing mode. The bank entrypoint
rejects banks above 14 and propagates the callback's failure.

`S3_Win16BridgeClose` restores the saved opaque mode state before releasing the
selector. If restore fails, it retains both the selector and snapshot for a
retry. If selector release fails, it retains the selector for a retry after
mode restore has succeeded. Startup failures follow the same rollback path;
failed cleanup state remains available through the bridge object. Host tests
inject selector setup errors, bank errors, mode-program and mode-restore
errors, and selector cleanup errors. If the selector-window callback partially
writes its output and then fails, the bridge clears that pointer before cleanup.
If setup cleanup cannot free the selector, it retains ownership and the free
callback for a `Close` retry while clearing the failed aperture pointer and
base. A failed mode restore retains its snapshot and selector for retry.

`S3_Win16NativeSelectorOps` supplies the native code-selector, allocation,
code/data conversion, selector base/limit, far-window, and release callbacks.
It leaves `ops.context` and all mode/bank callbacks untouched. Its allocator
rejects source code selectors larger than 64 KiB and records the allocated
selector's original limit. The bridge rejects limits above FFFFh. The free
callback restores and reads back that exact limit before calling
`FreeSelector`; Q132005 explains that `FreeSelector` uses the selector limit to
determine how many tiled selectors to release and requires the original limit.
Base and limit setters are verified by API readback rather than assuming a
setter return convention. The provider is linked into the DLL but is not
called by `Enable` or any GDI entrypoint.

The build inspector checks the DLL's NE module-reference/import-fixup tables
for KERNEL imports and at least seven distinct KERNEL ordinals, and checks the
linker map for each of the seven named selector imports. On the current build,
the DLL imports KERNEL and USER and has nine distinct KERNEL import ordinals;
the linker map includes all seven selector API names.

The selector path is not the remaining conceptual blocker. Microsoft SDK
Q131426 documents kernel-exported selectors including A000h and a selector
synthesis path for physical memory. This aperture is below 1 MiB, so a selector
base of A0000h and limit FFFFh is the documented low-memory form; DPMI
INT 31h/0800h is for physical-to-linear mapping above 1 MiB. The installed
Open Watcom `h/win/_win386.h` declares the exported `__A000` selector as well.
Thus the existing native KERNEL selector operations have a public contract,
and an exported-selector alternative is available without proprietary DDK
headers. The selector still does not establish I/O privilege, mode ownership,
or that an Enhanced Mode VDD will leave video memory unpaged.

The unresolved contract is Enhanced Mode video-device coordination. Microsoft
SDK Q80901 states that the standard Windows 3.1 VGA VDD demand-pages video
memory and is incompatible with Super VGA drivers that need over 256 KiB; it
prescribes either the vendor VDD or Windows' included `VDDVGA30.386`. The
1280x720x8 framebuffer is 921,600 bytes, so the target guest must use and test
one of those compatible VDD paths. The relevant VDD-facing interface includes
the INT 2Fh 40xx display-driver services and save/restore behavior. Publicly
archived documentation names the 4000h/4007h VM-assisted save/restore controls
and 4005h/4006h save/restore callbacks, but does not by itself establish the
correct callback sequence and S3 aperture ownership for this mode. The actual
target guest or an authorized DDK VGA/VDD sample is needed to close that
specific gap.

Open Watcom's `conio.h` `inp`/`outp` declarations and runtime implementation
only establish that the compiler emits `in al,dx` and `out dx,al`. They do not
authorize those instructions or coordinate the Windows VDD's port traps. No
port operation is connected to `Enable`.

The timing/PLL experiment retains its narrow `S3_MODE_SNAPSHOT`. Separately,
`S3_VgaCaptureState` and `S3_VgaRestoreState` now implement the cold DOS
harness's broader state contract: Misc Output, sequencer indices 00-04/08/10-
13/15, CRTC 00-18, S3 CRTC 30-6A, GC 00-08, AC 00-14, DAC mask and all 768 DAC
components. Capture restores the temporary SR08/CR38 unlock and attribute
display-enable state. Restore blanks while programming, restores CR38 last
among extended CRTC registers, reinstates CR11's protection bit after the
timing fields, reads palette data through the VGA DAC read-index port, and
captures again to verify exact state. The host mock keeps DAC read and write
indices separate and tests a saved CR11 with its protection bit set.
`S3_VgaBridgeCapture` and `S3_VgaBridgeRestore` adapt this typed snapshot to the
bridge's 1024-byte opaque slot; failed readback returns failure so Close retains
the snapshot for retry. Host tests exercise the adapter inside bridge Close,
including an injected DAC write failure followed by a successful retry. This
is still injected I/O:
there is no native VGA port provider connected to `Enable`, and a Windows
provider must perform the operation under a verified VDD ownership protocol.
`Enable` remains fail-closed pending the guest VDD/port contract.

## DDA lifecycle integration boundary

`src/s3_device.c` provides the injectable `S3_DeviceEnableWithOps` and
`S3_DeviceDisableWithOps` lifecycle. After bridge open succeeds, it initializes
the PDEV and attaches a banked `S3_SURFACE` whose bank callback routes through
`S3_Win16BridgeSetBank`. On failed Open, mode programming, or bank-0 setup, a
bridge handle/snapshot that could not be rolled back remains owned by the
caller runtime and can be retried through Disable. During Disable, the surface
window is invalidated before restore/free so a failed cleanup cannot leave GDI
writing through the cached aperture. Runtime initialization preserves active
or pending cleanup ownership. Disable retries never dereference the saved PDEV;
the current Disable call detaches its matching PDEV before teardown, and a
retry can finish cleanup even if that PDEV has since been released. The
exported DDA `Disable` is ordinal 6.

The DLL's normal `Enable` does not invoke this helper; there is still no
verified mode/port-I/O provider. The real `VERSIONINFO` RT_VERSION resource is
ID 1 and carries a fixed file type/subtype for a display driver. The prior
synthetic OEMBIN and FONTS declarations were removed; no font payload is
claimed. The static NE inspector validates the VERSIONINFO record and required
entrypoint ordinals. Host lifecycle failure checks do not substitute for a
Windows load/USER/font/GDI run.

## Current verification

On 2026-10-09, `WATCOM=/tmp/ow-linux-x64 OWBIN=binl64 ./build.sh` completed
after lifecycle review hardening. The DLL is NE16 and contains all expected DDA ordinals. The NE
import fixup and linker-map checks pass for the named KERNEL selector APIs; the
host suite passes the injectable selector/bank/mode lifecycle paths, including
one-selector limit rejection, original-limit retention, free retry, PDEV/surface
attachment, runtime-init idempotency while active/pending, and cleanup retry
without dereferencing the old PDEV. These are build and host checks, not
Windows guest validation.

## Verification sources

- Installed Open Watcom 2.0 Win16 headers: `h/win/win16.h`.
- Installed Win16 API import index: `lib286/win/windows.lib`, inspected with
  Open Watcom `wlib -t`.
- Microsoft's archived SDK 3.1 physical-memory example in
  [Q131426](https://jeffpar.github.io/kbarchive/kb/131/Q131426/) and selector
  lifecycle correction in
  [Q132005](https://jeffpar.github.io/kbarchive/kb/132/Q132005/), plus the
  `FreeSelector` failure convention in
  [Q89560](https://jeffpar.github.io/kbarchive/kb/089/Q89560/).
- Open Watcom's published [DPMI 0800H documentation](https://open-watcom.github.io/open-watcom-v2-wikidocs/cpguide.html)
  describes physical-address mapping and its linear-address result.
- Microsoft SDK [Q131426](https://jeffpar.github.io/kbarchive/kb/131/Q131426/)
  documents the low-memory A000h selector path, and [Q80901](https://jeffpar.github.io/kbarchive/kb/080/Q80901/)
  documents the Windows 3.1 SVGA/VDD memory limit and VDDVGA30 alternative.
- The runnable guest acceptance sequence and the bounded remaining VDD
  question are recorded in
  [`windows-guest-acceptance-plan.md`](windows-guest-acceptance-plan.md).
