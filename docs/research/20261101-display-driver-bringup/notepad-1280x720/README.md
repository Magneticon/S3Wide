# Applications at 1280x720: text, lines, frames (1 November 2026)

With DIB conversion, raster-font text, scanlines/polylines and FastBorder,
the probe's demo mode (`DEMO=1`: restore Program Manager, launch Calculator
and Notepad) runs on the S3Wide driver. Notepad's window spans the
1280x720 screen with title bar, menu (with mnemonic underlines), caret,
scroll bars and frame ([`notepad-emulator-window.png`](notepad-emulator-window.png)).
The emulator window and the driver's GDI readback differ only in 30 pixels
at the blinking caret ([`compare.txt`](compare.txt)).

[`icon-title-text-3x.png`](icon-title-text-3x.png) is an enlarged crop of
the first text drawn: the Program Manager icon title.

## The FastBorder fault

The first demo runs ended in a reboot loop. Logging the strings USER
measured decoded the error box: *PROGMAN caused a General Protection Fault
in module GDI.EXE at 0001:42B1*. That address is inside GDI's exported
`FASTWINDOWFRAME`. Disassembling it (LLVM, from the guest's GDI.EXE) showed
that when `dpRaster` has `RC_GDI20_OUTPUT` it far-calls the driver's
ordinal 17, `FastBorder`, which was not exported, and gave the exact Pascal
argument order. `FastBorder` is now implemented through the raster engine.
The stock VGA driver ran the same demo without error.

## Not done

No mouse cursor is drawn. Colours are the nearest 3:3:2 values.
