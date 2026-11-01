#!/usr/bin/env python3
"""Verify DOSBox-X scanout captures against the full RGB332 test pattern."""
import argparse
from pathlib import Path
from PIL import Image

WIDTH, HEIGHT = 1280, 720

def dac_rgb(value: int, levels: int) -> int:
    expanded = (value * 255 + (levels - 1) // 2) // (levels - 1)
    six_bit = (expanded * 63 + 127) // 255
    return (six_bit << 2) | (six_bit >> 4)

def expected_rgb(x: int, y: int) -> tuple[int, int, int]:
    red = (x // 160) & 7
    green = (y // 90) & 7
    blue = (x // 320) & 3
    return dac_rgb(red, 8), dac_rgb(green, 8), dac_rgb(blue, 4)

def check(path: Path) -> None:
    image = Image.open(path).convert("RGB")
    if image.size != (WIDTH, HEIGHT):
        raise SystemExit(f"FAIL {path}: got {image.width}x{image.height}, expected {WIDTH}x{HEIGHT}")
    colors = image.getcolors(maxcolors=WIDTH * HEIGHT)
    if colors is None or len(colors) != 64:
        count = 0 if colors is None else len(colors)
        raise SystemExit(f"FAIL {path}: got {count} colors, expected 64")
    for y in range(HEIGHT):
        for x in range(WIDTH):
            want = expected_rgb(x, y)
            got = image.getpixel((x, y))
            if got != want:
                raise SystemExit(f"FAIL {path}: ({x},{y}) got {got}, expected {want}")
    print(f"PASS {path}: {WIDTH}x{HEIGHT}, all 64 colors, every pixel matches RGB332 pattern")

parser = argparse.ArgumentParser()
parser.add_argument("captures", nargs="+", type=Path, help="VBE reference and candidate PNGs")
args = parser.parse_args()
for capture in args.captures:
    check(capture)
