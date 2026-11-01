"""Compare two 8-bit RGB/RGBA PNG images pixel by pixel (alpha ignored).

Prints the number of differing pixels and their bounding box, and optionally
writes a mask PNG (white = differs). With --map-colors, each colour in the
first image is first mapped to the colour it most often becomes in the
second, so a consistent palette difference (GDI logical colours vs the DAC
values an emulator displays) is reported separately from content
differences. Standard library only.
"""
from collections import Counter
import struct
import sys
import zlib


def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def read_png(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    pos, idat = 8, b""
    while pos < len(data):
        length, kind = struct.unpack_from(">I4s", data, pos)
        body = data[pos + 8: pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    if depth != 8 or color not in (2, 6) or interlace:
        raise ValueError(f"{path}: need 8-bit non-interlaced RGB/RGBA, got depth {depth} color {color}")
    bpp = 3 if color == 2 else 4
    stride = width * bpp
    raw = zlib.decompress(idat)
    prev = bytearray(stride)
    pixels = []
    for y in range(height):
        kind = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1: (y + 1) * (stride + 1)])
        for i in range(stride):
            left = line[i - bpp] if i >= bpp else 0
            up = prev[i]
            upleft = prev[i - bpp] if i >= bpp else 0
            if kind == 1:
                line[i] = (line[i] + left) & 0xFF
            elif kind == 2:
                line[i] = (line[i] + up) & 0xFF
            elif kind == 3:
                line[i] = (line[i] + ((left + up) >> 1)) & 0xFF
            elif kind == 4:
                line[i] = (line[i] + _paeth(left, up, upleft)) & 0xFF
        pixels.append([tuple(line[x * bpp: x * bpp + 3]) for x in range(width)])
        prev = line
    return width, height, pixels


def main(args):
    map_colors = "--map-colors" in args
    args = [arg for arg in args if arg != "--map-colors"]
    a_path, b_path = args[0], args[1]
    mask_path = args[2] if len(args) > 2 else None
    wa, ha, a = read_png(a_path)
    wb, hb, b = read_png(b_path)
    if (wa, ha) != (wb, hb):
        print(f"SIZE_MISMATCH {wa}x{ha} vs {wb}x{hb}")
        return 1
    if map_colors:
        pairs = Counter((a[y][x], b[y][x]) for y in range(ha) for x in range(wa))
        mapping = {}
        for (src, dst), _ in pairs.most_common():
            mapping.setdefault(src, dst)
        for src in sorted(mapping):
            print(f"map {src} -> {mapping[src]}")
        a = [[mapping[p] for p in row] for row in a]
    diff = [[a[y][x] != b[y][x] for x in range(wa)] for y in range(ha)]
    points = [(x, y) for y in range(ha) for x in range(wa) if diff[y][x]]
    print(f"size={wa}x{ha} differing_pixels={len(points)}")
    if points:
        xs = [p[0] for p in points]
        ys = [p[1] for p in points]
        print(f"bbox=({min(xs)},{min(ys)})-({max(xs)},{max(ys)})")
    if mask_path:
        sys.path.insert(0, __file__.rsplit("/", 1)[0].rsplit("\\", 1)[0])
        from bmp24_to_png import write_png
        rows = [bytes(v for x in range(wa) for v in ((255,) * 3 if diff[y][x] else (0,) * 3))
                for y in range(ha)]
        write_png(mask_path, wa, ha, rows)
    return 0 if not points else 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
