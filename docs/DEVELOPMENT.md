# Development environment

## Baseline

The initial target is Windows for Workgroups 3.11 in 386 Enhanced Mode under DOSBox-X using S3 Trio64 emulation.

DOSBox-X recommends its S3 Trio64 emulation for Windows 3.1x and identifies S3 Trio driver 1.70.04 (1997-09-22) for Trio32/Trio64/Trio64V+.

## Toolchain

The first build baseline should follow the Win16 DDK conventions rather than assuming a modern compiler can emit a compatible display driver.

The open-source `PluMGMK/vbesvga.drv` project documents a useful known-working pattern:
- Win16 DDK
- a contemporary Visual C++ installation
- DDK `286\TOOLS` in PATH
- DDK `286\INC` in INCLUDE
- EXE2BIN
- DOS-based automated build possible

Keep these local dependencies outside Git; see `external/README.md`.

## Research strategy

The preferred order is:

1. Reproduce a known-good stock/reference mode.
2. Locate or establish a buildable S3-capable source baseline.
3. Identify mode enumeration/configuration.
4. Identify S3 clock and CRTC programming.
5. Identify scanline pitch and framebuffer addressing.
6. Identify GDI capability reporting.
7. Add 1280x720x8.
8. Validate drawing and acceleration.

Do not begin by global-searching/replacing 1024 with 720 in binaries. S3 VGA timing values are encoded across VGA/S3 registers and mode structures.

## Useful upstream references

- DOSBox-X: https://github.com/joncampbell123/dosbox-x
- DOSBox-X Windows 3.1x guide: https://github.com/joncampbell123/dosbox-x/wiki/Guide%3AInstalling-Windows-3.1x
- DOSBox-X video-card guide: https://github.com/joncampbell123/dosbox-x/wiki/Guide%3AVideo-card-support-in-DOSBox%E2%80%90X
- VBESVGA: https://github.com/PluMGMK/vbesvga.drv

## Debugging

Prefer a layered loop:
1. static/source inspection
2. standalone mode/register experiments where feasible
3. disposable Windows guest
4. WDEB386/debug symbols if available and useful
5. DOSBox-X logging/source instrumentation for hardware-side visibility

The VBESVGA project documents use of WDEB386 with SYM files and is a useful reference for Win16 display-driver debugging.
