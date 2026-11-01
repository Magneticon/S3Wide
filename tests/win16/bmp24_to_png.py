"""Convert an uncompressed bottom-up 24-bpp BMP (as written by DISPBASE.EXE) to PNG."""
import struct
import sys
import zlib


def read_bmp24(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:2] != b"BM":
        raise ValueError("not a BMP file")
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bits = struct.unpack_from("<H", data, 28)[0]
    if bits != 24 or height <= 0:
        raise ValueError(f"expected bottom-up 24-bpp BMP, got {bits} bpp, height {height}")
    stride = (width * 3 + 3) & ~3
    rows = []
    for y in range(height - 1, -1, -1):
        bgr = data[offset + y * stride: offset + y * stride + width * 3]
        rgb = bytearray(len(bgr))
        rgb[0::3] = bgr[2::3]
        rgb[1::3] = bgr[1::3]
        rgb[2::3] = bgr[0::3]
        rows.append(bytes(rgb))
    return width, height, rows


def write_png(path, width, height, rows):
    def chunk(kind, body):
        return (struct.pack(">I", len(body)) + kind + body +
                struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF))

    raw = b"".join(b"\x00" + row for row in rows)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


if __name__ == "__main__":
    w, h, r = read_bmp24(sys.argv[1])
    write_png(sys.argv[2], w, h, r)
    print(f"{sys.argv[2]}: {w}x{h}")
