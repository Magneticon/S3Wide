"""Copy the resources of a licensed stock display driver into a Win16 .res.

Usage: python import_vga_resources.py VGA.DRV OUT.res

LOCAL BRING-UP ONLY. The output contains Microsoft's cursors, icons, OEM
bitmaps and OEMBIN tables from the user's own Windows installation. It is
written under build/ (ignored by git) and must never be committed or
distributed; a release needs original resources.

Every resource except VERSIONINFO (type 16, supplied by the driver) is
copied byte for byte, keeping its type, name and memory flags, in the 16-bit
.res record format that wlink's `option resource` accepts.
"""
import struct
import sys

RT_VERSION = 16


def read_ne_resources(path):
    with open(path, "rb") as f:
        data = f.read()
    ne = struct.unpack_from("<I", data, 0x3C)[0]
    if data[ne:ne + 2] != b"NE":
        raise ValueError(f"{path}: not an NE file")
    table = ne + struct.unpack_from("<H", data, ne + 0x24)[0]
    shift = struct.unpack_from("<H", data, table)[0]

    def ident(value):
        if value & 0x8000:
            return value & 0x7FFF
        start = table + value
        return data[start + 1: start + 1 + data[start]].decode("ascii")

    pos = table + 2
    while True:
        type_id, count = struct.unpack_from("<HH", data, pos)
        if type_id == 0:
            return
        pos += 8
        for _ in range(count):
            offset, length, flags, res_id = struct.unpack_from("<HHHH", data, pos)
            start = offset << shift
            yield ident(type_id), ident(res_id), flags, data[start:start + (length << shift)]
            pos += 12


def res_ident(value):
    if isinstance(value, int):
        return b"\xFF" + struct.pack("<H", value)
    return value.upper().encode("ascii") + b"\x00"


def main(source, output):
    records = []
    for res_type, res_name, flags, payload in read_ne_resources(source):
        if res_type == RT_VERSION:
            continue
        # NE resource flags use the same MOVEABLE/PURE/PRELOAD/DISCARDABLE
        # bits as 16-bit .res memory flags; drop the NE-only priority bits.
        memory_flags = flags & 0x1070
        records.append(res_ident(res_type) + res_ident(res_name) +
                       struct.pack("<HI", memory_flags, len(payload)) + payload)
    with open(output, "wb") as f:
        f.write(b"".join(records))
    print(f"{output}: {len(records)} resources imported from {source} (local only)")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
