# DOSBox-X S3 contract and source provenance

## Read-only source snapshot

The upstream repository was cloned read-only to `/tmp/dosbox-x-ref` from
`https://github.com/joncampbell123/dosbox-x`. The inspected revision is
`b60936a716c72e0978e940f813bab2cd40d43868` (`master` at retrieval). The clone
remained clean. Only this provenance note and independently authored adapter
code are retained in this recovery project; no DOSBox-X implementation source
was copied. The upstream tree carries GPL-2.0-or-later in `COPYING`.

SHA-256 of the inspected upstream files:

| File | SHA-256 |
| --- | --- |
| `src/hardware/vga_s3.cpp` | `367647009700b9bcf4d1a9d0320ff2cc057074a7060254f36460f84262e48dd2` |
| `src/hardware/vga_dac.cpp` | `b439808b4974bb9ceca12b8de39c3f44cd0bef7269f823aff32765ce71ed8a2a` |

A second read-only reference is the X.Org `xf86-video-s3` repository at
`https://gitlab.freedesktop.org/xorg/driver/xf86-video-s3`, commit
`ac42cac5bf0c6d6a37c3c4bf509de5a7636afe9e`. It is MIT licensed (`COPYING`).
The inspected Trio PLL and mode code is in
[`s3_Trio64DAC.c`](https://gitlab.freedesktop.org/xorg/driver/xf86-video-s3/-/blob/ac42cac5bf0c6d6a37c3c4bf509de5a7636afe9e/src/s3_Trio64DAC.c)
and [`s3_driver.c`](https://gitlab.freedesktop.org/xorg/driver/xf86-video-s3/-/blob/ac42cac5bf0c6d6a37c3c4bf509de5a7636afe9e/src/s3_driver.c).
The cloned tree remained clean; no implementation was copied. SHA-256:

| File | SHA-256 |
| --- | --- |
| `src/s3_Trio64DAC.c` | `0d67e2c50476d656829cb1c43dce034660313a50f012b0c2dd190eb7442e38b0` |
| `src/s3_driver.c` | `47dd2e8a425a119455fd5730bcd6f78788dc9c14dbbf685e51ceb0fd7655dbe3` |
| `COPYING` | `ec03712fa324f7df1a3a4b673abec3431e4c0f5df638251976023a1db8598e29` |

## Verified register and DAC behavior

* DOSBox-X's S3 CRTC handler accepts a write to CR35 only when its CR38 lock-1
  value is `0x48`. CR35 bits 0–3 select the low four bits of the 64 KiB bank;
  the handler preserves bits 4–7. See the pinned
  [CR35 handler](https://github.com/joncampbell123/dosbox-x/blob/b60936a716c72e0978e940f813bab2cd40d43868/src/hardware/vga_s3.cpp#L69-L81).
* CR51 bits 2–3 supply bank bits 4–5 in the common 80x/928 path. The emulator
  updates read and write bank selection together. See the
  [CR51 handler and field comments](https://github.com/joncampbell123/dosbox-x/blob/b60936a716c72e0978e940f813bab2cd40d43868/src/hardware/vga_s3.cpp#L190-L212).
  A 1280x720x8 surface at 1280-byte pitch occupies 921,600 bytes and addresses
  banks 0 through 14, so the target fits within the low CR35 nibble. The
  adapter supports bank numbers 0–63, matching CR35 plus CR51 field width.
* VGA DAC programming starts by writing the palette index to 3C8h, then writes
  red, green, and blue bytes to 3C9h; three bytes advance to the next index.
  The documented data path is six bits per component. See the
  [DAC port contract](https://github.com/joncampbell123/dosbox-x/blob/b60936a716c72e0978e940f813bab2cd40d43868/src/hardware/vga_dac.cpp#L48-L59)
  and [six-bit masking/default mode](https://github.com/joncampbell123/dosbox-x/blob/b60936a716c72e0978e940f813bab2cd40d43868/src/hardware/vga_dac.cpp#L312-L318),
  [default DAC mode](https://github.com/joncampbell123/dosbox-x/blob/b60936a716c72e0978e940f813bab2cd40d43868/src/hardware/vga.cpp#L188-L190).

`src/s3_hw.c` implements only these narrow behaviors behind injectable I/O
callbacks: selecting a 64 KiB bank with CR38 unlock/restore and preserving
unrelated CR35/CR51 bits, and loading the driver's 3:3:2 palette into a
six-bit DAC. Host tests verify register ordering, bank values through 63,
lock restoration, invalid inputs, and all 768 palette component writes.
These functions are not called by `Enable`; they are testable primitives, not
a claim that a physical or emulated display is enabled.

## Mode-setting boundary

The emulator's S3 CRTC handler applies the guest's standard and extended
register writes. For the intended 8-bpp path, the source identifies CR67 pixel
format value 0 as 8-bit, and its CR42 write comment labels bits 0–3 as a DCLK
selector when Miscellaneous Output bits 2–3 select clock 3. That CR42 comment
conflicts with the original Trio32/64 register documentation and is not used
by the emulator's clock function. CR43 bit 2 and CR51 bits 4–5 are
extended CRTC-offset bits, CR5D as horizontal timing overflow,
and CR5E as vertical timing overflow. The standard CRTC horizontal/vertical
timings and offset register still have to be supplied. See the pinned
[CR42/CR43 definitions](https://github.com/joncampbell123/dosbox-x/blob/b60936a716c72e0978e940f813bab2cd40d43868/src/hardware/vga_s3.cpp#L104-L128),
[CR5D/CR5E overflow definitions](https://github.com/joncampbell123/dosbox-x/blob/b60936a716c72e0978e940f813bab2cd40d43868/src/hardware/vga_s3.cpp#L300-L340),
and [CR67 pixel format definitions](https://github.com/joncampbell123/dosbox-x/blob/b60936a716c72e0978e940f813bab2cd40d43868/src/hardware/vga_s3.cpp#L391-L406).

The inspected source did not provide a complete register image or validated
Trio64 clock tuple. DOSBox-X does include an 8-bpp 1280x720 VBE mode (`0x23E`)
with 1280-pixel width and 160 offset units; this confirms the emulator's
framebuffer geometry but is not a physical timing table. See the pinned
[VBE mode entry](https://github.com/joncampbell123/dosbox-x/blob/b60936a716c72e0978e940f813bab2cd40d43868/src/ints/int10_modes.cpp#L309-L315).
The CRTC offset register is documented in the same source as scanline bytes
divided by 8 in double-word mode; 1280 bytes therefore encodes as `0xA0` in
CR13h (the target's high offset bits are zero), see the pinned
[offset handler](https://github.com/joncampbell123/dosbox-x/blob/b60936a716c72e0978e940f813bab2cd40d43868/src/hardware/vga_crtc.cpp#L325-L342).

## Unvalidated GTF/PLL candidate

The X.Org `gtf` utility from `xserver-xorg-core 2:21.1.16-1.3+deb13u3`
calculates VESA GTF modes; VESA lists GTF 1.1 among its
[freely available standards](https://vesa.org/vesa-standards/). The 1280x720@60
calculation (`gtf 1280 720 60`) produces:

```text
74.48 MHz  1280 1336 1472 1664  720 721 724 746  -HSync +VSync
```

All horizontal edges are divisible by 8 pixels, so they map to integral VGA
character clocks. Frame rate from the rounded modeline is approximately
60.00 Hz (`74.48 MHz / (1664 * 746)`). This is a standards-derived candidate,
not an existing S3 or DOSBox-X mode preset.

DOSBox-X pins the S3 PLL reference at 14,318 kHz and models clock output as
`14318 * (M + 2) / ((N + 2) * 2^R)` kHz. Its clock-search helper chooses `R`
for a 180–360 MHz VCO and searches `N=1..31`, `M=0..127`; see the pinned
[PLL model and search](https://github.com/joncampbell123/dosbox-x/blob/b60936a716c72e0978e940f813bab2cd40d43868/include/vga.h#L84-L88)
and [clock selection code](https://github.com/joncampbell123/dosbox-x/blob/b60936a716c72e0978e940f813bab2cd40d43868/src/hardware/vga_s3.cpp#L800-L827).
This emulator heuristic selects `M=102, N=3, R=2`, encoded as SR12=`0x43`,
SR13=`0x66` (74,453 kHz, VCO about 297.8 MHz). This is useful for describing
the emulator model, but it is not the strongest hardware-oriented PLL
candidate.

The pinned X.Org Trio driver uses a 14.31818 MHz reference, a 135–270 MHz VCO
range for ordinary Trio64, and accepts output up to 135 MHz at 8 bpp. Its
`S3TrioCalcClock` search for 74,480 kHz produces `M=50, N1=3, N2=1`, encoded
as SR12=`0x23`, SR13=`0x32`; the modeled result is 74,453 kHz and the VCO is
about 148.9 MHz. Its `S3TrioSetPLL` selects Misc Output clock 3, unlocks
sequencer extended registers with SR08=`0x06`, writes SR12/SR13, toggles SR15
bit 5 through 0→1→0 to apply the new tuple, then writes SR08=`0x00`. The
source is a reference for the register behavior and search constraints, not
proof for the particular card or DOSBox-X configuration. The manufacturer's
Trio64 overview gives a 135 MHz maximum output pixel data rate. See [S3's
product overview](https://vgamuseum.info/images/doc/s3/s3_trio64.pdf).

X.Org also has Trio-specific sequencer DAC save/restore callbacks at
[`S3Trio64DAC_Save/Restore`](https://gitlab.freedesktop.org/xorg/driver/xf86-video-s3/-/blob/ac42cac5bf0c6d6a37c3c4bf509de5a7636afe9e/src/s3_Trio64DAC.c#L57).
They save Misc Output and selected SR registers and restore the PLL with an
SR15 bit-5 toggle. This is not a complete VGA/S3 mode snapshot. The restore
code around SR15 appears to write its pulse values to the sequencer index port
`0x3C4` rather than data port `0x3C5`; it is evidence of the intended sequence
only, and must not be copied as a validated restore implementation.

The GTF horizontal edges become character counts `(160, 167, 184, 208)`.
The timing-only standard CRTC candidate is: CR00=`0xCB` (total minus 5),
CR01=`0x9F` (display minus 1), CR02=`0xA0` (blank start), CR03 low five bits
`0x10` (blank end low field), CR04=`0xA7` (sync start), CR05 low five bits
`0x18` (sync end), CR06=`0xE8` (vertical total minus 2 low byte), CR07=`0xF0`
(vertical overflow), CR09=`0x60` (vertical blank/line-compare overflow;
zero row repeat and doublescan), CR10=`0xD1` (vertical sync start), CR11 low
four bits=`0x04` (sync end; lock bit is handled during mode programming),
CR12=`0xCF` (720-line display-end low byte), CR15=`0xD0` (vertical blank
start low byte), and CR16=`0xFF` (vertical blank end chosen for the pinned
DOSBox-X wrap model). CR09[5] extends blank start to line 720. With CR16=`0x6A`,
the emulator calculated the wrapped blank end at line 875 and skipped 107
initial lines. With CR16=`0xFF`, the end is line 768 and its modeled wrap skip
is zero. Extended
overflow candidates are CR5D=`0x00` (horizontal values fit in standard
fields) and CR5E=`0x00` (all vertical timing values fit through bit 9).
CR13=`0xA0` is the 1280-byte pitch in dword mode. These are derived fields,
not a complete mode image.

The source now includes `S3_Program720p8bppCandidate` and a matching snapshot /
restore path. The host-mocked routine blanks with SR01, unlocks SR08 and CR38,
clears CR35 horizontal/vertical timing locks while preserving its bank fields,
unprotects CR00–07 with CR11, programs the candidate CRTC timing and PLL,
pulses SR15 bit 5, and reads back the programmed fields. A readback mismatch
triggers snapshot restoration. The snapshot retains Misc Output,
SR01/SR08/SR12/SR13/SR15, CR00–18, CR35/CR38, and the candidate's S3 extension
registers; restore writes original values under the matching protection
sequence. The test models the CRTC, CR35, and sequencer protection rules,
verifies programming order and PLL pulse, perturbs every captured register,
and checks exact restoration. This is a narrow timing/PLL overlay, disconnected
from `Enable`, rather than a complete device mode set.

The 8-bpp width fields have an independent X.Org mode reference: it selects
CR50 width code `0xC0` for 1280 pixels. CR51 bits 4–5 extend the CRTC offset
register; for a 1280-byte pitch, CR13 is `1280/8 = 160 = 0xA0`, so those
extension bits are zero. The emulator's own VBE 0x023E mode info and register
readback confirm CR13=`0xA0` and CR51[5:4]=`00`. The raw candidate now matches
that reference while preserving unrelated CR51 fields. It
sets CR43 to zero for 8 bpp, CR67 to zero in the Trio DAC path, Misc Output
clock selector to 3, and writes CR42=`0x00` per the CR42 register reference.
See the pinned [X.Org mode setup
code](https://gitlab.freedesktop.org/xorg/driver/xf86-video-s3/-/blob/ac42cac5bf0c6d6a37c3c4bf509de5a7636afe9e/src/s3_driver.c#L1374)
and [Trio DAC setup](https://gitlab.freedesktop.org/xorg/driver/xf86-video-s3/-/blob/ac42cac5bf0c6d6a37c3c4bf509de5a7636afe9e/src/s3_Trio64DAC.c#L330).

DOSBox-X documents CR35 bit 4 as vertical-timing lock and bit 5 as
horizontal-timing lock. The candidate clears those bits only while programming
and restores their original values before leaving the mode overlay. See the
[CR35 timing-lock definition](https://github.com/joncampbell123/dosbox-x/blob/b60936a716c72e0978e940f813bab2cd40d43868/src/hardware/vga_s3.cpp#L75-L88).
DOSBox-X documents CR31 bit 0 as enabling CPU bank operation and bit 3 as
enhanced memory mapping for access above 256 KiB. The banked candidate sets
those two bits and preserves the rest of CR31, including the model's bus-width
setting, while clearing CR31 bits 4–5 to reset display-start bits 16–17.
For the Trio/928+ layout, CR0C/CR0D supply start-address bits 8–15 and 0–7,
while CR51 bits 0–1 supply bits 18–19; the candidate zeros these fields to
start at framebuffer offset 0. CR51 bits 4–5 extend CR13's offset value; the
1280-byte pitch code is CR13=`0xA0` and CR51[5:4]=`00`, with bits 2–3 retained
for bank selection.
See [CR31 memory configuration](https://github.com/joncampbell123/dosbox-x/blob/b60936a716c72e0978e940f813bab2cd40d43868/src/hardware/vga_s3.cpp#L45-L70),
[CR51 fields](https://github.com/joncampbell123/dosbox-x/blob/b60936a716c72e0978e940f813bab2cd40d43868/src/hardware/vga_s3.cpp#L188-L211),
and [standard display-start fields](https://github.com/joncampbell123/dosbox-x/blob/b60936a716c72e0978e940f813bab2cd40d43868/src/hardware/vga_crtc.cpp#L245-L260).
DOSBox-X documents CR58 bits 0–1 as linear-window size (`00` means
64 KiB) and bit 4 as linear-framebuffer enable. The candidate clears those
fields to select a 64-KiB banked aperture and disable LFB. These decisions
match the CR35/CR51 64-KiB bank adapter. See [CR31 and CR58
definitions](https://github.com/joncampbell123/dosbox-x/blob/b60936a716c72e0978e940f813bab2cd40d43868/src/hardware/vga_s3.cpp#L45-L70)
and [CR58 behavior](https://github.com/joncampbell123/dosbox-x/blob/b60936a716c72e0978e940f813bab2cd40d43868/src/hardware/vga_s3.cpp#L255-L274).
The pinned X.Org driver uses CR31=`0x8D` and CR58 values with linear
addressing enabled for its framebuffer setup; that is a different memory
path, so its full values are not copied into this banked candidate.

Still deliberately unresolved: physical CPU aperture base/mapping and the
exact CRTC access/protection rules for target hardware. The cold test explicitly
programs packed 8-bpp SEQ/GC/AC state, DAC palette, pitch, and bank mapping; its
candidate now produces a visible RGB332 pattern. The original all-black capture
was caused by the snapshot's AC reads clearing display enable. CR09's row
repeat count and CR3A[4] were also missing from the initial cold candidate;
source-backed tests corrected both.

`src/s3_hw.c` has round-trip field encoding/decoding and integer clock
calculation for the modeled SR12/SR13 layout. The host test checks the X.Org
hardware-oriented candidate tuple and its refresh tolerance, plus a base 8-bpp
pitch encoder/decoder for offsets through 255. Host tests assert the candidate
bytes, 74,453 kHz result, refresh tolerance, 1280-byte pitch / CR13=`0xA0`
round trip, last-pixel offset 921,599, bank 14, mocked candidate programming,
readback rollback, and exact state restoration. The I/O implementation remains injected; there is
no physical or emulator register write backend in the DLL.

The clock path is Misc Output bits 2–3 selecting clock 3 plus the programmed
S3 PLL tuple; X.Org's Trio code follows that path and does not program a CR42
DCLK selector. A follow-on cold-mode test found the emulator's CR42 read
handler returns constant `0x0D`, while `SVGA_S3_GetClock()` ignores CR42. The
original March 1995 S3 Trio32/64 datasheet confirms CR42 bit 5
is interlace and bits 4–0 are reserved (printed page 16-2, PDF page 195);
Table 11-3 also describes bit 5 as non-interlaced/interlaced mode selection
(PDF page 93). The [S3 datasheet PDF](https://bitsavers.org/components/s3/DB014-B_Trio32_Trio64_Graphics_Accelerators_Mar1995.pdf)
is hosted by the Bitsavers S3 archive; its SHA-256 is
`7528eefe26846da697541ccab575610dad3eaa7ab7de0ac7147d02bd2ddf4277`. This
manufacturer register description conflicts with the emulator source comment.
The candidate writes CR42 zero, leaving interlace disabled and reserved bits
zero; no CR42 readback check is used because the emulator returns a constant.

The new `raw_s3_cold_smoke.c` starts from BIOS mode 3 and, without VBE setup,
programs the candidate plus explicit packed 8-bpp sequencer, graphics, and
attribute state. It verifies exact tracked-register rollback (including CR3A
and CR16), pitch, S3 width/PLL fields, and full 921,600-byte banked readback.
The final VBE reference, VBE-seeded candidate, and cold candidate captures are
all 1280×720, contain all 64 expected colors, and are pixel-identical. The
host-side `tests/dosbox-x/check_s3_scanout.py` checks every pixel against the
expected RGB332 formula. The readable text baseline, original all-black
diagnostic image, pre-CR16 613-row capture, full register dumps, and emulator
logs are retained in `evidence/`. The VBE-seeded timing/pitch assertion, bank
check, and forced rollback pass; the cold rollback and bank/readback gate also
pass. Windows GDI remains untested; `Enable` continues to reject the
device-enable call.
