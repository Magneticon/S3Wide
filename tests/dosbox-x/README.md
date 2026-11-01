# DOSBox-X DOS display and S3 raw-helper proofs

These are DOS-only, no-media checks of the emulator's VBE 1280x720x8 mode,
banked memory, and the recovery driver's raw S3 timing/PLL and bank helpers.
They do not load the S3 Windows DLL or test GDI behavior.

The pinned DOSBox-X source tree defines VBE mode `0x23E` as 1280x720, 8-bpp,
with a 64-KiB A0000h aperture. See the mode entry in
[`int10_modes.cpp`](https://github.com/joncampbell123/dosbox-x/blob/b60936a716c72e0978e940f813bab2cd40d43868/src/ints/int10_modes.cpp#L311)
and its VBE bank-window implementation in
[`int10_vesa.cpp`](https://github.com/joncampbell123/dosbox-x/blob/b60936a716c72e0978e940f813bab2cd40d43868/src/ints/int10_vesa.cpp#L560-L585).

## Build

The checked-in `VBECHK.EXE` was built for DOS with Open Watcom 2. To rebuild:

```sh
cd tests/dosbox-x
WATCOM=/path/to/open-watcom-v2 OWBIN=binl64 ./build-smoke.sh
```

The program asks VBE for mode info, verifies 1280x720, packed 8-bpp, readable
and writable bank A, scanline pitch, granularity, and window size, then sets
mode `0x23E` without the linear-framebuffer flag. It writes a full-screen
RGB332 bar pattern through VBE function `4F05h`, reads back every framebuffer
byte through the bank window, and leaves the pattern visible after successful
readback. On failure it prints the failing byte and restores text mode.

## Run with DOSBox-X

From this directory, use the DOSBox-X executable matching the pinned
`b60936a716c72e0978e940f813bab2cd40d43868` S3 model implementation:

```sh
/path/to/dosbox-x -conf dosbox-x-vbe.conf
```

The config selects `svga_s3trio64`, uses DOSBox-X's built-in DOS shell, mounts
only this test directory as C:, and launches `VBECHK.EXE`; no Windows or DOS
media image is required. On Linux the default screenshot shortcut is
`F12+P`; the PNG is saved under `capture/`. Press a key in the guest after
capturing to restore text mode. Retain the screenshot and DOS output as the
DOS VBE baseline evidence.

### Headless run

The saved environment has no display server. The checked-in
`dosbox-x-vbe-headless.conf` runs the test in batch mode and records DOS
console output in `capture/vbe-headless.log`. The emulator was built from the
pinned upstream source commit `b60936a716c72e0978e940f813bab2cd40d43868`
(DOSBox-X 2026.10.01, SDL2) and run with SDL's dummy video/audio backends:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software \
LD_LIBRARY_PATH=/tmp/dosbox-local/root/usr/lib/x86_64-linux-gnu \
/tmp/dosbox-local/install/bin/dosbox-x \
  -conf dosbox-x-vbe-headless.conf -log-con -time-limit 25
```

The run completed with exit status 0. DOSBox-X logged mode info
`1280x720x8`, pitch 1280, 921,600 framebuffer bytes, and a 64-KiB bank window
with 64-KiB granularity. `VBERES.TXT` contains the guest-written PASS result.
The archived emulator log is at
[`evidence/dosbox-x-vbe-headless.log`](../../evidence/dosbox-x-vbe-headless.log).
Build provenance and hashes are in
[`evidence/dosbox-x-build.txt`](../../evidence/dosbox-x-build.txt). The dummy
backend provides no visible display, so this run has no screenshot and does
not verify rendered pixels at a host display surface.

This baseline establishes DOSBox-X's VBE mode and banked-memory behavior
independently of Windows; a display-backed run is still needed to verify
visible scanout.

## Raw S3 helper gate

`raw_s3_smoke.c` is a separate DOS harness compiled together with the
repository's actual `src/s3_hw.c`. Build it with `WATCOM=/path/to/open-watcom-v2
./build-raw-s3-smoke.sh`, then run with
`dosbox-x-s3-raw-headless.conf` on the same pinned `svga_s3trio64` emulator.
It sets VBE 0x023E as a known reference, records the BIOS mode pitch and S3
register snapshot, validates a full 921,600-byte RGB332 pattern through BIOS
banks 0–14, then calls the driver's palette, mode candidate, and raw S3 bank
selection routines. It checks each raw bank field and reads back the entire
pattern through banks 0–14. A one-shot CRTC readback fault deliberately
exercises the production rollback path; the captured register set is compared
afterward. Raw mode pitch is decoded from CR13 and CR51[5:4] and
compared to the VBE register reference.

The successful run is preserved in
[`evidence/dosbox-x-s3-raw-headless.log`](../../evidence/dosbox-x-s3-raw-headless.log)
and the guest result file is [`S3RAW.TXT`](S3RAW.TXT). The raw register
candidate is intentionally only a timing/PLL overlay: it relies on BIOS-set
VGA state and does not prove a complete cold-start mode set or Windows GDI.
The guest requests a DOSBox-X screenshot after restoring AC display enable;
the VBE-seeded candidate and delayed repeat match the VBE reference's full
1280×720 RGB332 pattern pixel-for-pixel. The emulator's VBE reference showed CR13=`0xA0` and
CR51[5:4]=`00` for 1280-byte pitch. This caught and corrected an earlier raw
candidate value of `0x20` in CR51[5:4]. `EnableDevice` remains fail-closed.

## Cold VGA/S3 gate and emulator screenshot

`raw_s3_cold_smoke.c` starts from BIOS VGA text mode 3 without making any VBE
calls. It captures the tracked Misc Output, sequencer indexes 00–04, 08,
10–13, and 15, CRTC 00–18, unlocked S3 CRTC 30–6A, GC 00–08, AC 00–14,
DAC mask, and all 768 DAC component bytes. It injects a one-shot CR00
readback fault and compares the tracked pre/post state after the production
rollback path. It then programs the timing/PLL candidate, explicitly installs
packed 8-bpp sequencer/GC/attribute-controller state and the driver's DAC
palette, checks 1280-byte pitch and the 1280-pixel S3 width fields, and
validates banks 0–14 plus a 921,600-byte pattern readback. The register
snapshots and guest result are in [`S3COLD.TXT`](S3COLD.TXT); the emulator
log is [`evidence/dosbox-x-s3-cold-headless.log`](../../evidence/dosbox-x-s3-cold-headless.log).

The headless config enables DOSBox-X's integration device so the guest can
request screenshots through the emulator's own capture route. The original
all-black capture is retained as
[`evidence/dosbox-x-s3-cold-black-scanout.png`](../../evidence/dosbox-x-s3-cold-black-scanout.png).
It was caused by the snapshot routine's AC-index reads leaving attribute
controller display enable clear; restoring AC display enable changed the
visible result. The capture route therefore exposed a harness side effect,
not a black framebuffer.

Controlled register changes then resolved two actual cold-mode mismatches.
The candidate now clears CR09[4:0] to avoid repeating each graphics row,
sets CR3A[4] for 8-bpp mode, programs the vertical blank extension needed by
720 lines, and sets the 720p Misc Output polarity/clock bits. CR17[6] remains
preserved from the snapshot because changing it did not restore row stepping.
The corrected cold capture and its delayed repeat are
[`evidence/dosbox-x-s3-cold-visible-pattern.png`](../../evidence/dosbox-x-s3-cold-visible-pattern.png)
and [`evidence/dosbox-x-s3-cold-visible-pattern-confirmation.png`](../../evidence/dosbox-x-s3-cold-visible-pattern-confirmation.png):
1280×720 with the complete RGB332 pattern. CRTC CR07/CR12 encode 720 active
lines. CR09=`0x60` sets the line-compare and vertical-blank-start extensions,
while CR16=`0xFF` ends the blank interval so DOSBox-X's wrap model does not
skip active lines. Before the CR16 fix, CR15=`0xD0` plus CR09[5] placed blank
start at line 720; CR16=`0x6A` made the emulator compute a 107-line wrapped
skip, producing the preserved 1280×613 intermediate image.

The register and memory gate still passes from cold text mode: injected
rollback restores the tracked snapshot including CR3A, banks 0–14 map through
CR35/CR51, and all 921,600 bytes read back. The captured full-state dump is
[`S3COLD.TXT`](S3COLD.TXT). In the VBE-seeded candidate run, DOSBox-X captured
The VBE-seeded candidate and VBE baseline are also 1280×720 with 64 colors.
`check_s3_scanout.py` verifies each pixel against the expected 3:3:2 pattern;
the three PNGs are pixel-identical. The raw timing/pitch assertion, bank check,
forced rollback, cold rollback, and bank/readback checks pass.
The original March 1995 S3 datasheet confirms CR42 bit 5 controls interlace
and bits 4–0 are reserved; in the pinned emulator CR42 reads as hardcoded
`0x0D`, and `SVGA_S3_GetClock()` chooses clock 3 from Misc Output and calculates
that PLL while ignoring CR42's low nibble.
The RBIL S3 register transcription describes CR42 bit 5 as interlace and
bits 4–0 as reserved, which conflicts with the emulator's write comment; the
candidate now clears CR42 to zero and does not treat the emulator read value
as validation. See the [CR42 register transcription](https://fd.lod.bz/rbil/ports/video/p03d403d5.html#P0730).
The original March 1995 S3 Trio32/64 datasheet confirms the same CR42 fields
(printed page 16-2, PDF page 195); its [Bitsavers copy](https://bitsavers.org/components/s3/DB014-B_Trio32_Trio64_Graphics_Accelerators_Mar1995.pdf)
has SHA-256 `7528eefe26846da697541ccab575610dad3eaa7ab7de0ac7147d02bd2ddf4277`.
CR42 does not explain the earlier black capture. Cold-start and VBE-seeded
DOSBox-X captures now show the complete 1280×720 pattern. Windows GDI remains
untested. `EnableDevice` remains fail-closed.

## Run status in the saved environment

The pinned DOSBox-X source build and headless DOS tests succeeded. They verify
BIOS mode info, cold-start candidate setup from VGA text mode, driver
timing/PLL readback, CR35/CR51 bank mapping, full-frame pattern readback, and
rollback of the tracked VGA/S3/DAC snapshot after forced readback failure.
The emulator screenshot route also works under SDL dummy: it captured readable
text mode and the cold-start RGB332 pattern. All five current captures are
1280×720 and pixel-identical to the expected 64-color pattern. The preserved
pre-CR16 capture records the 1280×613/56-color failure. Windows GDI remains untested. A separate Windows 3.11 guest is present in
the CODEX environment, but these DOS-only checks do not boot it or validate
GDI/VDD behavior.
