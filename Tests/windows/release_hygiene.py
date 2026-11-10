#!/usr/bin/env python3
"""Release-only checks for the Windows binary."""
from pathlib import Path
import struct
import sys

FORBIDDEN = [
    "--e2e-test",
    "--render-test",
    "--toolbar-test",
    "--settings-test",
    "--settings-ui-test",
    "--welcome-test",
]
ALLOWED_DLLS = {
    "advapi32.dll", "bcrypt.dll", "comctl32.dll", "comdlg32.dll", "gdi32.dll", "gdiplus.dll",
    "kernel32.dll", "msvcrt.dll", "ole32.dll", "shell32.dll",
    "shlwapi.dll", "user32.dll", "uuid.dll", "winmm.dll", "winhttp.dll",
}


def imports(data: bytes) -> set[str]:
    if data[:2] != b"MZ":
        raise AssertionError("not a PE file")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise AssertionError("bad PE signature")
    machine, sections, _, _, _, opt_size, _ = struct.unpack_from("<HHIIIHH", data, pe + 4)
    assert machine == 0x8664, f"expected x64 PE, got machine {machine:#x}"
    opt = pe + 24
    magic = struct.unpack_from("<H", data, opt)[0]
    assert magic == 0x20B, "expected PE32+"
    import_rva, import_size = struct.unpack_from("<II", data, opt + 120)
    section_table = opt + opt_size
    section_rows = []
    for i in range(sections):
        off = section_table + i * 40
        name, vsize, va, raw_size, raw = struct.unpack_from("<8sIIII", data, off)[:5]
        section_rows.append((va, max(vsize, raw_size), raw))

    def file_offset(rva: int) -> int:
        for va, size, raw in section_rows:
            if va <= rva < va + size:
                return raw + rva - va
        raise AssertionError(f"RVA {rva:#x} is outside sections")

    found = set()
    if import_rva:
        off = file_offset(import_rva)
        end = off + import_size
        while off + 20 <= end:
            original, stamp, chain, name_rva, first = struct.unpack_from("<IIIII", data, off)
            if not any((original, stamp, chain, name_rva, first)):
                break
            n = file_offset(name_rva)
            z = data.index(0, n)
            found.add(data[n:z].decode("ascii").lower())
            off += 20
    return found


def main() -> None:
    path = Path(sys.argv[1])
    data = path.read_bytes()
    leaked = [flag for flag in FORBIDDEN if flag.encode("utf-16le") in data or flag.encode() in data]
    assert not leaked, f"production binary contains test entry points: {', '.join(leaked)}"
    dlls = imports(data)
    unexpected = dlls - ALLOWED_DLLS
    assert not unexpected, f"unexpected DLL imports: {', '.join(sorted(unexpected))}"
    print(f"PASS release hygiene: x64, no test switches; imports={', '.join(sorted(dlls))}")


if __name__ == "__main__":
    main()
