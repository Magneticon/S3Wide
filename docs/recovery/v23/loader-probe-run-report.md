# S3 Win16 loader-probe staging report

Date: 2026-10-09

## Package and payload verification

- Archive: `C:\CODEX\S3Win16Recovery-20261009-v23.zip`, 375,552 bytes, SHA-256 `9c4680c6db77c6f6365dcc5d16b824ceafc7bffeba8891a91eaa61578069268d` (matches expected).
- Extracted separately to this directory. The ZIP has 126 members; all paths were checked and none were unsafe.
- `build/S3PROBE.EXE`: 2,254 bytes, SHA-256 `b97ba5f335f27370fc0a1a5fdbe6fdfd72d97877a1f722a36b6958b49e0b4dad` (matches expected).
- `build/s3wide_driver.dll`, staged under the guest name `S3WIDE.DRV`: 21,742 bytes, SHA-256 `3902af42d12e36713914081e9f0ef193c86b1df8768e847930f662bf150c49fe` (matches expected).
- Reviewed `tests/win16/s3_loader_probe.c`: it uses ordinary Win16 `LoadLibrary` and `GetProcAddress`, checks DDA ordinals 5 and 450, checks `Enable(InquireInfo)` GDIINFO fields, `GetDriverResourceID`, and the fail-closed device `Enable` result. It has no S3 register or VRAM I/O path.

## Disposable guest mount and staging

- The running DOSBox-X window (PID 5504) is titled `CODEX Win3.11 stock VGA baseline (disposable copy)`. Its exact config is `S3Win16Recovery-v22/dosbox-windows-stock-baseline.conf`.
- That config maps guest `C:` to `C:\CODEX\2026-10-09\task-2\S3Win16Recovery-v22\guest-disposable\DOS`; the normalized mount path was checked against the disposable-copy path before staging.
- Exactly two files were copied into that mounted directory's `S3TEST` folder:
  - `C:\S3TEST\S3PROBE.EXE`: 2,254 bytes, SHA-256 `b97ba5f335f27370fc0a1a5fdbe6fdfd72d97877a1f722a36b6958b49e0b4dad`.
  - `C:\S3TEST\S3WIDE.DRV`: 21,742 bytes, SHA-256 `3902af42d12e36713914081e9f0ef193c86b1df8768e847930f662bf150c49fe`.
- `C:\CODEX\DOS\S3TEST` does not exist. The original guest was not modified. The driver was only staged; it was not installed or selected, and `SYSTEM.INI` was not changed.

## Execution status

- The probe was not launched, so there is no PASS/FAIL dialog, guest exit code, or probe runtime error to report.
- The available Windows Computer Use bridge returned `Trusted RPC service is not configured: sky`; its CUA inventory showed no apps/browsers, and its Windows `listWindows` method was unavailable. The computer-use skill requires its `@oai/sky` native window-control path and prohibits substituting direct PowerShell UI automation. Thus the dialog could not be opened or captured in this session.
- The disposable Windows 3.11 guest remains running, with the two verified probe files staged for the next experiment.
