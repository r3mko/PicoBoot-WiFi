#!/usr/bin/env python3

"""
An ordinary .dol as an IPL payload (design D43, docs/research/dol-loader.md): the loader stub
(src/dolstub/dolstub.bin, from dolstub.S), a table, and the .dol's sections, as one image at
0x81300000. wrap() returns that image as a .dol built for 0x81300000, so the IPL converters
(tools/process_ipl.py, src/web/dol.js, tools/picoboot-wifi.py) turn it into a payload as they do
gekkoboot. The page and the CLI carry copies of this (plan step R2).

The table, right after the stub (big-endian words): the entry point, the BSS address and size,
the section count, then each section's address, the offset of its data from the table, and its
size. The stub's header describes what it does with it.

    python3 tools/dolstub.py program.dol program-ipl.dol
"""

import argparse
import os
import struct
import sys

STUB_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "dolstub", "dolstub.bin")
LOAD = 0x81300000           # Where BS1 puts the payload image, and jumps to
LOW_END = 0x80003100        # Low memory, which the stub fills, ends here; libogc links programs here


def stub():
    with open(STUB_PATH, "rb") as f:
        return f.read()


def sections(dol):
    """The .dol's sections with data, as (number, address, file offset, size), its BSS as
    (address, size), and its entry point, after checking them against what the stub can load."""
    if len(dol) < 256:
        raise ValueError(f"File is {len(dol)} bytes, smaller than a DOL header (256 bytes)")
    h = struct.unpack(">64I", dol[:256])
    found = [(n, h[18 + n], h[n], h[36 + n]) for n in range(18) if h[36 + n]]
    bss, entry = (h[54], h[55]), h[56]
    if not found:
        raise ValueError("DOL contains no sections")
    for n, address, offset, size in found:
        name = f"Section {n}"
        if address == 0:
            raise ValueError(f"{name} has data but no load address")
        if offset + size > len(dol):
            raise ValueError(f"{name} ends at 0x{offset + size:X}, past the end of the file "
                             f"(0x{len(dol):X} bytes), file truncated?")
        _check_range(name, address, size)
    if bss[1]:
        _check_range("The BSS", *bss)
    if not any(n < 7 and address <= entry < address + size for n, address, _, size in found):
        raise ValueError(f"The entry point 0x{entry:08X} isn't in a text section")
    return found, bss, entry


def _check_range(name, address, size):
    end = address + size
    if address % 4 or size % 4:
        raise ValueError(f"{name} (0x{address:08X}, {size} bytes) isn't in whole words")
    if address < 0x80000000 or end > 0x81800000:
        raise ValueError(f"{name} (0x{address:08X}-0x{end:08X}) is outside main memory "
                         f"(0x80000000-0x81800000)")
    if address < LOW_END:
        raise ValueError(f"{name} (0x{address:08X}-0x{end:08X}) reaches low memory, below "
                         f"0x{LOW_END:08X}, which the loader stub fills")
    if end > LOAD:
        raise ValueError(f"{name} (0x{address:08X}-0x{end:08X}) reaches the payload image at "
                         f"0x{LOAD:08X}, where the loader stub runs")


def image(dol, stub_bytes=None):
    """The payload image of an ordinary .dol: the stub, the table and the sections."""
    found, (bss_address, bss_size), entry = sections(dol)
    table_size = 16 + 12 * len(found)
    table_size += -table_size % 32
    records, data = b"", b""
    for _, address, offset, size in found:
        records += struct.pack(">III", address, table_size + len(data), size)
        data += dol[offset:offset + size]
    table = (struct.pack(">IIII", entry, bss_address, bss_size, len(found)) + records).ljust(table_size, b"\0")
    return (stub() if stub_bytes is None else stub_bytes) + table + data


def wrap(dol, stub_bytes=None):
    """The image as a .dol built for 0x81300000: one text section, its entry at the start."""
    img = image(dol, stub_bytes)
    header = [0] * 64
    header[0], header[18], header[36], header[56] = 0x100, LOAD, len(img), LOAD
    return struct.pack(">64I", *header) + img


def main():
    p = argparse.ArgumentParser(description="Wrap an ordinary .dol in the D43 loader stub, as a .dol for 0x81300000")
    p.add_argument("dol", help="the .dol (linked anywhere from 0x80003100 to below 0x81300000)")
    p.add_argument("output", help="the .dol to write")
    args = p.parse_args()
    try:
        with open(args.dol, "rb") as f:
            out = wrap(f.read())
    except (OSError, ValueError) as e:
        print(f"{args.dol}: {e}", file=sys.stderr)
        return 1
    with open(args.output, "wb") as f:
        f.write(out)
    print(f"{args.output}: {len(out)} bytes, the image {len(out) - 256} bytes at 0x{LOAD:08X}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
