#!/usr/bin/env python3
"""Read-only sanity checks for Win16 NE header/export metadata."""
import struct
import sys
from pathlib import Path


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


REQUIRED_KERNEL_IMPORTS = {
    "ALLOCSELECTOR",
    "FREESELECTOR",
    "GETSELECTORBASE",
    "GETSELECTORLIMIT",
    "PRESTOCHANGOSELECTOR",
    "SETSELECTORBASE",
    "SETSELECTORLIMIT",
}


def parse_import_fixups(data: bytes, ne_offset: int) -> tuple[list[str], dict[str, set[int]]]:
    module_count = u16(data, ne_offset + 0x1E)
    module_table_rel = u16(data, ne_offset + 0x28)
    import_names_rel = u16(data, ne_offset + 0x2A)
    modules: list[str] = []
    for index in range(module_count):
        name_rel = u16(data, ne_offset + module_table_rel + 2 * index)
        name_at = ne_offset + import_names_rel + name_rel
        name_len = data[name_at]
        modules.append(data[name_at + 1 : name_at + 1 + name_len].decode("ascii").upper())

    segment_count = u16(data, ne_offset + 0x1C)
    segment_table_rel = u16(data, ne_offset + 0x22)
    alignment = u16(data, ne_offset + 0x32)
    ordinals: dict[str, set[int]] = {module: set() for module in modules}
    for index in range(segment_count):
        entry_at = ne_offset + segment_table_rel + 8 * index
        sector, length, flags, _min_alloc = struct.unpack_from("<4H", data, entry_at)
        if not flags & 0x0100:
            continue
        segment_at = sector << alignment
        segment_size = length or 65536
        reloc_at = segment_at + segment_size
        count = u16(data, reloc_at)
        for fixup_index in range(count):
            record_at = reloc_at + 2 + 8 * fixup_index
            _source_type, target_flags, _source_offset, target_module, target = struct.unpack_from(
                "<BBHHH", data, record_at
            )
            # NE target type 1 is an import by ordinal; target_module indexes
            # the module-reference table and target is the imported ordinal.
            if target_flags & 0x03 != 1 or target_module == 0:
                continue
            if target_module > len(modules):
                raise ValueError(f"invalid imported module reference {target_module}")
            module = modules[target_module - 1]
            ordinals.setdefault(module, set()).add(target)
    return modules, ordinals


def parse_linker_map(path: Path) -> set[str]:
    imports: set[str] = set()
    for line in path.read_text(errors="replace").splitlines():
        fields = line.split()
        if len(fields) == 2 and fields[1].upper() == "KERNEL":
            imports.add(fields[0].upper())
    missing = REQUIRED_KERNEL_IMPORTS - imports
    if missing:
        raise ValueError(f"linker map is missing KERNEL imports: {sorted(missing)}")
    return imports


def parse(path: Path, map_path: Path | None = None,
          allow_imported_resources: bool = False) -> list[str]:
    data = path.read_bytes()
    if data[:2] != b"MZ":
        raise ValueError("missing MZ signature")
    ne_offset = u32(data, 0x3C)
    if data[ne_offset : ne_offset + 2] != b"NE":
        raise ValueError("missing NE signature")
    # NE header offsets are relative to the NE header.
    enttab_rel = u16(data, ne_offset + 0x04)
    enttab_size = u16(data, ne_offset + 0x06)
    resource_rel = u16(data, ne_offset + 0x24)
    resident_rel = u16(data, ne_offset + 0x26)
    module_rel = u16(data, ne_offset + 0x28)
    nonresident_abs = struct.unpack_from("<I", data, ne_offset + 0x2C)[0]
    enttab = ne_offset + enttab_rel
    resident = ne_offset + resident_rel
    modules = ne_offset + module_rel
    end = enttab + enttab_size
    names: dict[int, str] = {}
    cursor = resident
    while cursor < modules:
        length = data[cursor]
        if length == 0:
            break
        name = data[cursor + 1 : cursor + 1 + length].decode("ascii", "replace")
        ordinal = u16(data, cursor + 1 + length)
        names[ordinal] = name
        cursor += length + 3
    # Ordinal 0 is the module name in the resident table but the description
    # in the non-resident table; keep the module name.
    module_name = names.get(0, "")
    cursor = nonresident_abs
    while cursor < len(data):
        length = data[cursor]
        if length == 0:
            break
        name = data[cursor + 1 : cursor + 1 + length].decode("ascii", "replace")
        ordinal = u16(data, cursor + 1 + length)
        names[ordinal] = name
        cursor += length + 3

    # Entry table: bundles start with count and type; bundle type 0 ends it.
    ordinals: set[int] = set()
    cursor = enttab
    ordinal = 1
    while cursor < end:
        count = data[cursor]
        cursor += 1
        if count == 0:
            break
        bundle_type = data[cursor]
        cursor += 1
        if bundle_type == 0:
            # Null bundle slots represent ordinal gaps and have no entry data.
            ordinal += count
            continue
        if bundle_type == 0xFF:
            # movable entry: flags, int3f, segment word, offset word
            entry_size = 6
        else:
            # fixed entry: flags and offset word
            entry_size = 3
        for _ in range(count):
            ordinals.add(ordinal)
            cursor += entry_size
            ordinal += 1

    required = {
        1: "BitBlt",
        2: "ColorInfo",
        4: "Disable",
        5: "Enable",
        8: "Output",
        9: "Pixel",
        10: "RealizeObject",
        21: "SetDIBitsToDevice",
    }
    resources = parse_resources(data, ne_offset, resource_rel)
    lines = [f"format=NE16", f"file_bytes={len(data)}", f"entry_ordinals={len(ordinals)}"]
    for number, expected_name in required.items():
        actual_name = names.get(number, "<unnamed>")
        present = number in ordinals
        lines.append(f"ordinal_{number}={actual_name};entry={'present' if present else 'missing'}")
        if not present or actual_name.lower() != expected_name.lower():
            raise ValueError(f"expected {expected_name} as entry ordinal {number}")
    # Ordinal 450 is not exported: like the stock VGA.DRV, GDI then looks up
    # the driver's resources by their own IDs.
    if 450 in ordinals:
        raise ValueError("GetDriverResourceID (ordinal 450) must not be exported")
    if allow_imported_resources:
        # Local bring-up build with resources copied from a licensed VGA.DRV.
        if ("16", 1) not in resources or ("OEMBIN", 1) not in resources:
            raise ValueError(f"expected VERSIONINFO and imported OEMBIN resources; found {resources!r}")
    elif resources != [("16", 1)]:
        raise ValueError(
            "expected only numeric RT_VERSION resource ID 1; "
            f"found {resources!r}"
        )
    version_info = find_resource_payload(data, ne_offset, resource_rel, "16", 1)
    if version_info is None:
        raise ValueError("missing RT_VERSION resource payload")
    fixed_info = version_info.find(b"\xbd\x04\xef\xfe")
    if fixed_info < 0 or len(version_info) < fixed_info + 44:
        raise ValueError("VERSIONINFO has no complete VS_FIXEDFILEINFO")
    if u32(version_info, fixed_info + 36) != 3 or \
       u32(version_info, fixed_info + 40) != 4:
        raise ValueError("VERSIONINFO is not typed as a display driver")
    # Win16 VERSIONINFO string data uses the ANSI representation; the resource
    # compiler sample explicitly terminates these strings with NUL bytes.
    if b"S3 widescreen display driver\x00" not in version_info:
        raise ValueError("VERSIONINFO is missing the driver description")
    imported_modules, imported_ordinals = parse_import_fixups(data, ne_offset)
    # Display drivers load before GDI and USER, so KERNEL must be the only
    # imported module (a stray USER import makes Windows reject the driver).
    if imported_modules != ["KERNEL"]:
        raise ValueError(f"expected only the KERNEL import module; found {imported_modules!r}")
    if module_name.upper() != "DISPLAY":
        raise ValueError(f"expected module name DISPLAY; found {module_name!r}")
    kernel_ordinals = imported_ordinals.get("KERNEL", set())
    if len(kernel_ordinals) < len(REQUIRED_KERNEL_IMPORTS):
        raise ValueError(
            f"expected at least {len(REQUIRED_KERNEL_IMPORTS)} distinct KERNEL import ordinals; "
            f"found {sorted(kernel_ordinals)}"
        )
    mapped_kernel_imports = parse_linker_map(map_path) if map_path is not None else None
    if (mapped_kernel_imports is not None and
            len(kernel_ordinals) != len(mapped_kernel_imports)):
        raise ValueError(
            "KERNEL linker-map imports do not match distinct NE import ordinals: "
            f"{len(mapped_kernel_imports)} names vs {sorted(kernel_ordinals)}"
        )
    lines.append(f"module_name={module_name}")
    lines.append(f"import_modules={imported_modules!r}")
    lines.append(f"kernel_import_ordinals={sorted(kernel_ordinals)!r}")
    lines.append(f"resource_records={resources!r}")
    return lines


def parse_resources(data: bytes, ne_offset: int, resource_rel: int) -> list[tuple[str, int | str]]:
    base = ne_offset + resource_rel
    shift = u16(data, base)
    cursor = base + 2

    def id_value(raw: int) -> int | str:
        if raw & 0x8000:
            return raw & 0x7FFF
        string_offset = base + raw
        length = data[string_offset]
        return data[string_offset + 1 : string_offset + 1 + length].decode("ascii", "replace")

    records: list[tuple[str, int | str]] = []
    while True:
        type_id = u16(data, cursor)
        cursor += 2
        if type_id == 0:
            break
        count = u16(data, cursor)
        cursor += 6  # count and reserved DWORD
        resource_type = id_value(type_id)
        for _ in range(count):
            resource_offset, resource_length, flags, name_id, handle, usage = struct.unpack_from("<6H", data, cursor)
            del flags
            del handle, usage
            cursor += 12
            _ = resource_offset << shift, resource_length << shift
            records.append((str(resource_type), id_value(name_id)))
    return records


def find_resource_payload(data: bytes, ne_offset: int, resource_rel: int,
                          wanted_type: str, wanted_name: int) -> bytes | None:
    base = ne_offset + resource_rel
    shift = u16(data, base)
    cursor = base + 2

    def id_value(raw: int) -> int | str:
        if raw & 0x8000:
            return raw & 0x7FFF
        string_offset = base + raw
        length = data[string_offset]
        return data[string_offset + 1 : string_offset + 1 + length].decode("ascii", "replace")

    while True:
        type_id = u16(data, cursor)
        cursor += 2
        if type_id == 0:
            return None
        count = u16(data, cursor)
        cursor += 2
        cursor += 4  # reserved DWORD
        resource_type = str(id_value(type_id))
        for _ in range(count):
            resource_offset, resource_length, _flags, name_id, _handle, _usage = struct.unpack_from(
                "<6H", data, cursor
            )
            cursor += 12
            if resource_type == wanted_type and id_value(name_id) == wanted_name:
                start = resource_offset << shift
                length = resource_length << shift
                end = start + length
                if end > len(data):
                    raise ValueError("resource payload extends past the NE file")
                return data[start:end]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


if __name__ == "__main__":
    args = sys.argv[1:]
    allow_imported = "--allow-imported-resources" in args
    args = [arg for arg in args if arg != "--allow-imported-resources"]
    try:
        for line in parse(Path(args[0]), Path(args[1]) if len(args) > 1 else None,
                          allow_imported):
            print(line)
    except (OSError, IndexError, struct.error, ValueError) as exc:
        print(f"inspection failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
