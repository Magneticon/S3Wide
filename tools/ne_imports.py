"""List the imports and named exports of a 16-bit NE file.

Usage: python ne_imports.py FILE...

Reads the module-reference and imported-name tables, then walks every
segment's relocation records to report which (module, ordinal) pairs are
imported, and lists the resident and non-resident name tables (exported
names with ordinals). Used to compare project drivers with the stock drivers
in a guest (which are read locally and never committed).
"""
import struct
import sys


def ne_imports(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:2] != b"MZ":
        raise ValueError("not an MZ executable")
    ne = struct.unpack_from("<I", data, 0x3C)[0]
    if data[ne:ne + 2] != b"NE":
        raise ValueError("not an NE executable")
    (seg_count, mod_count) = struct.unpack_from("<HH", data, ne + 0x1C)
    (seg_table, _res_table, _resname, modref_table, impname_table) = \
        struct.unpack_from("<HHHHH", data, ne + 0x22)
    align = struct.unpack_from("<H", data, ne + 0x32)[0] or 9

    modules = []
    for i in range(mod_count):
        offset = struct.unpack_from("<H", data, ne + modref_table + 2 * i)[0]
        start = ne + impname_table + offset
        modules.append(data[start + 1: start + 1 + data[start]].decode("ascii", "replace"))

    imports = {}
    for i in range(seg_count):
        sector, length, flags, _ = struct.unpack_from("<HHHH", data, ne + seg_table + 8 * i)
        if not (flags & 0x0100) or sector == 0:  # no relocations
            continue
        seg_len = length or 0x10000
        reloc = (sector << align) + seg_len
        count = struct.unpack_from("<H", data, reloc)[0]
        for r in range(count):
            _src, kind, _off, a, b = struct.unpack_from("<BBHHH", data, reloc + 2 + 8 * r)
            target = kind & 0x03
            if target == 1:  # imported ordinal
                imports.setdefault(modules[a - 1], set()).add(b)
            elif target == 2:  # imported name
                start = ne + impname_table + b
                name = data[start + 1: start + 1 + data[start]].decode("ascii", "replace")
                imports.setdefault(modules[a - 1], set()).add(name)
    return modules, imports


def _name_table(data, start, end=None):
    names = []
    pos = start
    while (end is None or pos < end) and data[pos] != 0:
        length = data[pos]
        name = data[pos + 1: pos + 1 + length].decode("ascii", "replace")
        ordinal = struct.unpack_from("<H", data, pos + 1 + length)[0]
        names.append((ordinal, name))
        pos += 3 + length
    return names


def ne_exports(path):
    """Return (module name, [(ordinal, name)]) from both name tables."""
    with open(path, "rb") as f:
        data = f.read()
    ne = struct.unpack_from("<I", data, 0x3C)[0]
    resname = struct.unpack_from("<H", data, ne + 0x26)[0]
    nonres_len = struct.unpack_from("<H", data, ne + 0x20)[0]
    nonres_off = struct.unpack_from("<I", data, ne + 0x2C)[0]
    resident = _name_table(data, ne + resname)
    nonresident = _name_table(data, nonres_off, nonres_off + nonres_len) if nonres_len else []
    module = resident[0][1] if resident else ""
    exports = [e for e in resident[1:] + nonresident[1:]]
    return module, sorted(exports)


def ne_resources(path):
    """Return [(type, id, size_bytes, file_offset)] from the resource table."""
    with open(path, "rb") as f:
        data = f.read()
    ne = struct.unpack_from("<I", data, 0x3C)[0]
    table = ne + struct.unpack_from("<H", data, ne + 0x24)[0]
    shift = struct.unpack_from("<H", data, table)[0]

    def ident(value):
        if value & 0x8000:
            return value & 0x7FFF
        start = table + value
        return data[start + 1: start + 1 + data[start]].decode("ascii", "replace")

    resources = []
    pos = table + 2
    while True:
        type_id, count = struct.unpack_from("<HH", data, pos)
        if type_id == 0:
            break
        pos += 8
        for _ in range(count):
            offset, length, _flags, res_id = struct.unpack_from("<HHHH", data, pos)
            resources.append((ident(type_id), ident(res_id), length << shift, offset << shift))
            pos += 12
    return resources


if __name__ == "__main__":
    for path in sys.argv[1:]:
        modules, imports = ne_imports(path)
        module, exports = ne_exports(path)
        print(f"{path}: module={module} imports={modules}")
        for name in modules:
            items = sorted(imports.get(name, ()), key=lambda v: (isinstance(v, str), v))
            print(f"  import {name}: {items}")
        print("  exports: " + ", ".join(f"{o}={n}" for o, n in exports))
        for res_type, res_id, size, _ in ne_resources(path):
            print(f"  resource type={res_type} id={res_id} bytes<={size}")
