# S3Wide

Experimental Windows 3.1/3.11 display-driver project targeting native widescreen modes on the S3 Trio64 emulated by DOSBox-X.

## Primary milestone

Implement **1280x720 at 8 bpp (256 colours)** while preserving the existing Windows 3.1 S3 Trio64 modes and, where practical, S3 acceleration.

This project is deliberately **not** a generic VESA-driver project. VESA implementations such as `vbesvga.drv` are valuable references and diagnostic fallbacks, but the target is an S3-specific accelerated driver.

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
