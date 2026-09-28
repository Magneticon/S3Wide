# Test plan

## Acceptance test: 1280x720x8

A build passes the first milestone only when all of the following are true:

1. Windows 3.1/3.11 boots in 386 Enhanced Mode.
2. The guest desktop is actually 1280x720 at 8 bpp.
3. The complete desktop is visible without guest-side corruption.
4. Mouse movement covers the full desktop.
5. Program Manager windows can move and resize across all edges.
6. Palette changes do not corrupt the desktop.
7. Text, solid fills, lines, BitBlt/scrolling and common brushes render correctly.
8. Modal dialogs and menus render and clip correctly.
9. Repeated mode startup is stable.
10. Applicable S3 acceleration is demonstrated or any lost acceleration is explicitly documented as a failing criterion.

## Regression modes

At minimum retain/test known-good stock modes:
- 640x480x8
- 800x600x8
- 1024x768x8
- any additional modes supported by the selected baseline

## Evidence

Store transient evidence under:
- `output/logs/`
- `output/screenshots/`
- `output/builds/`

Durable discoveries belong in `docs/`.

## Recovery

Always retain a clean Windows directory/image using VGA or known-good stock S3 configuration. The test environment must be restorable without manually booting the broken Windows configuration.
