#!/usr/bin/env python3

"""
Checks the flash layout in src/common/layout.h (design D6, D37, D39), and optionally that built
ELFs fit their regions (run by CI after the build):

    python3 tests/check_layout.py
    python3 tests/check_layout.py --loader build/src/loader/loader.elf --firmware build/src/firmware/firmware.elf

The ELF check uses arm-none-eabi-nm, or the nm given in the NM environment variable.
"""

import argparse
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LAYOUT_H = os.path.join(REPO, "src", "common", "layout.h")

XIP_BASE = 0x10000000
PICOBOOT_PAYLOAD_OFFSET = 0x80000       # Where PicoBoot payload UF2s land (design C3)
PICOBOOT_PAYLOAD_SIZE = 0x180000        # 1.5 MB (design C4)
# .bss is zeroed before main(), on the clock the boot ROM left running, before the injection: about
# 0.13 ms of the margin per KB (D10; phase 4, step 4.8). Large buffers that are set up when first used
# go in .uninitialized_data instead.
BSS_MAX = {"loader": 8 * 1024, "firmware": 24 * 1024}

# (name, offset define, size define), in flash order
REGIONS = [
    ("loader", "LOADER_OFFSET", "LOADER_SIZE"),
    ("boot log", "BOOTLOG_OFFSET", "BOOTLOG_SIZE"),
    ("boot control", "BOOTCTL_OFFSET", "BOOTCTL_SIZE"),
    ("settings", "SETTINGS_OFFSET", "SETTINGS_SIZE"),
    ("Wi-Fi A", "WIFI_A_OFFSET", "WIFI_SIZE"),
    ("Wi-Fi B", "WIFI_B_OFFSET", "WIFI_SIZE"),
    ("payload 1", "PAYLOAD1_OFFSET", "PAYLOAD1_SIZE"),
    ("slot A", "SLOT_A_OFFSET", "SLOT_SIZE"),
    ("slot B", "SLOT_B_OFFSET", "SLOT_SIZE"),
    ("payload 2", "PAYLOAD2_OFFSET", "PAYLOAD2_SIZE"),
    ("reserved", "RESERVED_OFFSET", "RESERVED_SIZE"),
]


def parse_layout(text):
    """The LAYOUT_* defines of layout.h, without the prefix: {"SLOT_SIZE": 0x40000, ...}."""
    return {name: int(value, 16)
            for name, value in re.findall(r"^#define LAYOUT_(\w+)\s+(0x[0-9A-Fa-f]+)\b", text, re.M)}


def check_layout(layout):
    """List of problems with the layout; empty if it is correct."""
    needed = {"FLASH_SIZE", "SECTOR_SIZE"} | {d for _, o, s in REGIONS for d in (o, s)}
    missing = sorted(needed - layout.keys())
    if missing:
        return [f"LAYOUT_{name} not defined" for name in missing]

    problems = []
    sector = layout["SECTOR_SIZE"]
    for name, off, size in REGIONS:
        for define in (off, size):
            if layout[define] % sector:
                problems.append(f"{name}: LAYOUT_{define} = 0x{layout[define]:X} is not a multiple of the sector size")

    end = 0
    for name, off, size in REGIONS:
        start = layout[off]
        if start != end:
            kind = "overlaps the region before" if start < end else "leaves a gap after the region before"
            problems.append(f"{name} starts at 0x{start:06X}, which {kind} (ending at 0x{end:06X})")
        end = start + layout[size]
    if end != layout["FLASH_SIZE"]:
        problems.append(f"the regions end at 0x{end:06X}, not at the end of flash (0x{layout['FLASH_SIZE']:06X})")

    if layout["PAYLOAD1_OFFSET"] != PICOBOOT_PAYLOAD_OFFSET or layout["PAYLOAD1_SIZE"] != PICOBOOT_PAYLOAD_SIZE:
        problems.append("payload 1 must be 1.5 MB at 0x080000, where PicoBoot payload UF2s land (design C3, C4)")
    if layout["RESERVED_OFFSET"] + layout["RESERVED_SIZE"] != layout["FLASH_SIZE"] or layout["RESERVED_SIZE"] < sector:
        problems.append("the last sector of flash must stay reserved (design D37)")
    for name in ("BOOTCTL_SIZE", "SETTINGS_SIZE"):
        if layout[name] != 2 * sector:
            problems.append(f"LAYOUT_{name} must be two sectors (design D23)")
    if layout["BOOTLOG_SIZE"] < 2 * sector:
        problems.append("LAYOUT_BOOTLOG_SIZE must be at least two sectors (design D39)")
    return problems


def parse_nm(text):
    """Symbol values from `nm` output ("<hex value> <type> <name>" per line)."""
    values = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) == 3:
            values[parts[2]] = int(parts[0], 16)
    return values


def check_elf(kind, values, layout):
    """Problems with a built loader or firmware ELF, from its nm symbols."""
    limit = layout["LOADER_SIZE" if kind == "loader" else "SLOT_SIZE"]
    if "__flash_binary_end" not in values:
        return [f"{kind}: symbol not found: __flash_binary_end"]
    end = values["__flash_binary_end"]
    if not XIP_BASE <= end <= XIP_BASE + limit:
        return [f"{kind}: ends at 0x{end:08X}, outside 0x{XIP_BASE:08X}-0x{XIP_BASE + limit:08X}"]
    for symbol in ("__bss_start__", "__bss_end__"):
        if symbol not in values:
            return [f"{kind}: symbol not found: {symbol}"]
    bss = values["__bss_end__"] - values["__bss_start__"]
    if bss > BSS_MAX[kind]:
        return [f"{kind}: .bss is {bss} bytes, more than {BSS_MAX[kind]}: it's zeroed before the injection "
                "and costs margin (D10); keep large buffers in .uninitialized_data"]
    return []


def main(argv):
    parser = argparse.ArgumentParser(description="Check the flash layout and the built ELFs.")
    parser.add_argument("--loader", help="loader ELF")
    parser.add_argument("--firmware", help="firmware ELF")
    args = parser.parse_args(argv[1:])

    with open(LAYOUT_H) as f:
        layout = parse_layout(f.read())
    problems = check_layout(layout)

    nm = os.environ.get("NM", "arm-none-eabi-nm")
    sizes = []
    for kind, path in (("loader", args.loader), ("firmware", args.firmware)):
        if not path or problems:
            continue
        result = subprocess.run([nm, path], capture_output=True, text=True)
        if result.returncode != 0:
            problems.append(f"{nm} failed on {path}: {result.stderr.strip()}")
            continue
        values = parse_nm(result.stdout)
        problems += check_elf(kind, values, layout)
        if "__flash_binary_end" in values:
            limit = layout["LOADER_SIZE" if kind == "loader" else "SLOT_SIZE"]
            bss = values.get("__bss_end__", 0) - values.get("__bss_start__", 0)
            sizes.append(f"{kind} {values['__flash_binary_end'] - XIP_BASE} of {limit} bytes "
                         f"(.bss {bss} of {BSS_MAX[kind]})")

    for problem in problems:
        print(f"Error: {problem}", file=sys.stderr)
    if problems:
        return 1
    print("Layout OK" + (": " + ", ".join(sizes) if sizes else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
