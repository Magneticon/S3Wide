# Codex instructions — S3Wide

## Mission

Develop and validate a Windows 3.1/3.11 S3 Trio64 display driver that supports native widescreen resolutions under DOSBox-X.

### First acceptance target

**1280x720x8 bpp**, genuine guest resolution (not host scaling), Windows 3.1 Enhanced Mode, DOSBox-X `svga_s3trio64`.

## Non-negotiable constraints

1. Preserve existing supported S3 modes unless a documented temporary experiment requires otherwise.
2. Preserve S3 acceleration where technically possible. Do not silently replace the project with a generic VESA framebuffer driver.
3. VESA/VBESVGA may be used as a reference, oracle, diagnostic path, or temporary control experiment.
4. Do not commit proprietary Microsoft/S3 binaries or documentation unless redistribution permission is established.
5. Keep the test VM disposable. A broken display driver is expected during development.
6. Every material experiment must leave a short record in `output/logs/` or a committed research note when the finding is durable.
7. Never claim a mode works from configuration data alone. Acceptance requires Windows to boot and render at the requested guest resolution.

## Milestones

- M0 — reproducible environment and dependency checker
- M1 — establish a buildable Win16 display-driver source baseline
- M2 — map mode tables, timing programming, pitch, framebuffer and GDI capability reporting
- M3 — expose/select 1280x720x8
- M4 — boot Windows and render a correct 1280x720 desktop
- M5 — validate cursor, palette, clipping, BitBlt, text, lines, brushes and window movement
- M6 — verify S3 acceleration is still active for applicable primitives
- M7 — 1280x720x16
- M8 — 1280x720x24/32 where architecture permits
- M9 — additional widescreen modes

## M0/M1 research priorities

Before substantial coding:

1. Inventory the exact Win16 DDK display-driver sample/source available locally.
2. Study `PluMGMK/vbesvga.drv`, especially its Win16 DDK build process and Video 7 ancestry.
3. Study DOSBox-X S3 emulation to identify accepted CRTC/extended-register behavior.
4. Inventory the stock S3 Trio 1.70.04 package locally and document filenames/version strings.
5. Determine whether a legally usable S3/compatible source base exists. If not, document the cleanest implementation strategy rather than blindly binary-patching.
6. Establish a known-good baseline test at a stock mode before introducing 1280x720.

## 1280x720x8 invariants

- Visible size: 1280 x 720
- Minimum visible framebuffer payload: 921600 bytes
- Expected simple packed 8-bpp pitch candidate: 1280 bytes, but verify hardware/driver alignment requirements rather than assuming.
- Mouse coordinates must reach (1279,719) without wrapping/clipping errors.
- Windows must report the desktop as 1280x720.
- No stretching of a 4:3 guest mode counts as success.

## Testing discipline

For each candidate build record:
- commit/build identifier
- driver binary hash
- DOSBox-X version
- DOSBox-X config
- Windows version/mode
- observed guest resolution/depth
- boot success/failure
- rendering defects
- acceleration evidence
- screenshot filename where available

Keep a known-good VGA or stock-S3 recovery path.

## Stop conditions

If a change makes the guest unbootable, restore the clean test image; do not spend time repairing a disposable Windows tree manually.

If a required dependency is missing, make `CHECKENV.BAT` report it clearly and continue with research/tasks that do not require it.
