# S3Wide

**Users:** download the release zip and read the [S3Wide User Guide](docs/guide/S3WIDE-GUIDE.md) ([PDF](docs/guide/S3WIDE-GUIDE.pdf)). Unpack it into one folder and install with Windows Setup (Options, Change System Settings, Display, "Other display"), pointing it at that folder. Building needs your own `VGA.DRV` (`VGA_DRV=`), because a display driver carries the Windows cursors, icons and window bitmaps.

Experimental Windows 3.1/3.11 display-driver project targeting native widescreen modes on the S3 Trio64 emulated by DOSBox-X.

## Primary milestone

Implement **1280x720 at 8 bpp (256 colours)** while preserving the existing Windows 3.1 S3 Trio64 modes and, where practical, S3 acceleration.

The S3-specific path remains the primary target on S3 hardware. On 2026-11-01 the owner decided to add a second backend inside the same driver: a VESA linear-framebuffer path for resolutions and colour depths the S3 cannot reach (up to 3840x2160 at 16M colours on DOSBox-X's own `svga_dosbox` card). See [the VESA backend notes](docs/research/20261101-vbe-backend/README.md).

## Choosing a mode

`S3WIDE.DRV` reads its mode from `SYSTEM.INI`:

```ini
[boot]
display.drv=s3wide.drv

[s3wide.drv]
Width=3840
Height=2160
Depth=32
```

`Depth` is 8, 16 (5:6:5) or 32. Without a `[s3wide.drv]` section the driver uses 1280x720x8. On an S3 Trio64 that native mode is programmed directly (S3 registers, hardware cursor); every other request is served through the VESA BIOS and needs an exact linear-framebuffer mode of that size and depth. If none exists, the driver keeps 1280x720x8 on S3 hardware and otherwise refuses to start (restore `display.drv=vga.drv` from DOS).

DOSBox-X settings for the VESA backend:

```ini
[dosbox]
machine=svga_dosbox

[video]
# 3840x2160x32 needs 33 MB; 1920x1080x32 fits in 8 MB
vmemsize=64
vesa modelist width limit=0
vesa modelist height limit=0
allow high definition vesa modes=true
```

The emulated S3 cards (`svga_s3*`) report at most 16 MB and list modes up to 1920x1440, so they cannot provide 4K.

The VESA path draws with the CPU. Full-screen fills take about 0.1 to 0.5 s of emulated time at 3840x2160x32 (at a fixed 100,000 cycles); [the speed notes](docs/research/20261102-4k-speed/README.md) have the measurements and the `DISPBNCH.EXE` benchmark. Install steps, recovery and known limits for testing on a real Windows 3.11 installation are in [package/INSTALL.TXT](package/INSTALL.TXT) (Windows Setup can install it from `package/OEMSETUP.INF`); the review and release checks are in [the release notes](docs/research/20261102-release-review/README.md). The driver requires a 386 or later. On a slow host, DOSBox-X's `frameskip` (under `[render]`) leaves more time for emulation at 4K.

## Target environment

- Windows for Workgroups 3.11 / Windows 3.1 Enhanced Mode
- DOSBox-X, `machine=svga_s3trio64`
- S3 Trio Windows 3.1 driver 1.70.04 as behavioral/reference baseline
- 4 MiB emulated VRAM baseline
- Win16 DDK display-driver toolchain
- First target: 1280x720x8

## Repository layout

- `docs/` — architecture, development and test notes
- `external/` — instructions for non-redistributable/local dependencies
- `src/` — project source once a source baseline is selected
- `test/` — DOSBox-X configuration and guest-side tests
- `tools/` — environment/build/deploy/test helpers
- `output/` — generated logs/build products; not committed

Start with [AGENTS.md](AGENTS.md) and [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).

## Reference projects

- DOSBox-X Windows 3.1x installation/video documentation
- DOSBox-X source code (S3 Trio64 emulation)
- PluMGMK/vbesvga.drv — modern open-source Windows 3.1 VBE display driver derived from the Win16 DDK Video 7 sample

Do not commit proprietary Microsoft DDK/compiler files, original S3 binaries, or S3 documentation unless redistribution rights are verified.
