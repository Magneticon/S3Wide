# S3Wide

Experimental Windows 3.1/3.11 display-driver targeting native widescreen modes on the S3 Trio64 emulated by DOSBox-X.

**Use:** download the release zip and read the [S3Wide User Guide](docs/guide/S3WIDE-GUIDE.md) ([PDF](docs/guide/S3WIDE-GUIDE.pdf)).

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
