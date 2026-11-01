# DIB conversion and a pixel-exact scanout check (1 November 2026)

`src/s3_dib.c` converts uncompressed 1/4/8/24-bpp DIBs to and from memory
bitmaps (`DeviceBitmapBits`) and draws them on the device
(`SetDIBitsToDevice` without a translate table). Icons are now drawn
correctly, and the probe's own 1280x720 `GetDIBits` readback succeeds
(`CAPTURE_STAGE=0`, [`probe-RESULT.txt`](probe-RESULT.txt)).

After mapping GDI's logical colours to the DAC colours DOSBox-X shows, the
emulator window and the driver's GDI readback are identical in all
1280x720 pixels ([`compare.txt`](compare.txt)): what the driver believes it
drew is exactly what the emulated S3 scans out. No cursor is drawn yet, so
there is no cursor difference.

## A capture artifact, not a driver bug

Earlier captures showed the bottom 32 rows black. DOSBox-X reported a
1280x720 surface; the window had been placed partly below the 978-pixel-high
host desktop, and DOSBox-X's GDI surface output never paints the off-screen
part. The runner now pins the window at `windowposition=0,0`.

While investigating, CR16 (vertical blank end) was changed from `0xFF` to
`0xE7`, ending blanking one line before the vertical total. `0xFF` was not
the cause on DOSBox-X 2026.08.31, but it relied on a blanking-wrap model;
`0xE7` is the conventional value. Not re-verified on DOSBox-X 2026.10.01.
