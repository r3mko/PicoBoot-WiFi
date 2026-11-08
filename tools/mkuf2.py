#!/usr/bin/env python3

"""
Builds the USB install UF2 of PicoBoot-WiFi (design D24): one file that writes the loader, blank boot
control and settings, the Wi-Fi blob into Wi-Fi A and the firmware into slot A, all at absolute
addresses (rp2350-arm-s family; no partition table, so the boot ROM writes them where they say).

    python3 tools/mkuf2.py --loader loader.bin --firmware firmware.bin --wifi wifi-blob.bin \\
        --out picoboot-wifi-install.uf2

It leaves both payload areas alone, so an installed payload survives, and it leaves out picotool's
RP2350-E10 block, which would land in the last flash sector (design D37).
"""

import argparse
import os
import re
import struct
import sys

UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_FLAG_FAMILY_ID = 0x00002000
FAMILY_RP2350_ARM_S = 0xE48BFF59
PAGE = 256
XIP_BASE = 0x10000000

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LAYOUT_H = os.path.join(REPO, "src", "common", "layout.h")


def read_layout(path=LAYOUT_H):
    with open(path) as f:
        return {name: int(value, 16)
                for name, value in re.findall(r"^#define LAYOUT_(\w+)\s+(0x[0-9A-Fa-f]+)\b", f.read(), re.M)}


def install_regions(layout, loader, firmware, wifi):
    """(name, flash offset, data, region size) for everything the install writes."""
    return [
        ("loader", layout["LOADER_OFFSET"], loader, layout["LOADER_SIZE"]),
        ("boot control", layout["BOOTCTL_OFFSET"], b"\xff" * layout["BOOTCTL_SIZE"], layout["BOOTCTL_SIZE"]),
        ("settings", layout["SETTINGS_OFFSET"], b"\xff" * layout["SETTINGS_SIZE"], layout["SETTINGS_SIZE"]),
        ("Wi-Fi A", layout["WIFI_A_OFFSET"], wifi, layout["WIFI_SIZE"]),
        ("slot A", layout["SLOT_A_OFFSET"], firmware, layout["SLOT_SIZE"]),
    ]


def build_uf2(regions, family=FAMILY_RP2350_ARM_S):
    """UF2 blocks of 256 bytes for each (name, offset, data, size); data padded with 0xFF to a page."""
    pages = []
    for name, offset, data, size in regions:
        if len(data) > size:
            raise ValueError(f"{name} is {len(data)} bytes, more than its region ({size})")
        if not data:
            raise ValueError(f"{name} is empty")
        padded = data + b"\xff" * (-len(data) % PAGE)
        for i in range(0, len(padded), PAGE):
            pages.append((XIP_BASE + offset + i, padded[i:i + PAGE]))
    blocks = []
    for n, (address, chunk) in enumerate(pages):
        blocks.append(struct.pack("<8I476sI", UF2_MAGIC_START0, UF2_MAGIC_START1, UF2_FLAG_FAMILY_ID,
                                  address, PAGE, n, len(pages), family, chunk, UF2_MAGIC_END))
    return b"".join(blocks)


def read_uf2(data):
    """[(address, payload, family, block_no, num_blocks, flags)] for each block of a UF2 file."""
    blocks = []
    for off in range(0, len(data), 512):
        m0, m1, flags, address, size, n, total, family = struct.unpack("<8I", data[off:off + 32])
        end = struct.unpack("<I", data[off + 508:off + 512])[0]
        if (m0, m1, end) != (UF2_MAGIC_START0, UF2_MAGIC_START1, UF2_MAGIC_END):
            raise ValueError(f"block at file offset {off} has bad magic")
        blocks.append((address, data[off + 32:off + 32 + size], family, n, total, flags))
    return blocks


def main(argv):
    parser = argparse.ArgumentParser(description="Build the PicoBoot-WiFi USB install UF2.")
    parser.add_argument("--loader", required=True, help="loader .bin")
    parser.add_argument("--firmware", required=True, help="firmware .bin")
    parser.add_argument("--wifi", required=True, help="Wi-Fi area image (tools/mkwifiblob.py)")
    parser.add_argument("--out", required=True, help="UF2 to write")
    args = parser.parse_args(argv[1:])

    try:
        files = {}
        for name in ("loader", "firmware", "wifi"):
            with open(getattr(args, name), "rb") as f:
                files[name] = f.read()
        regions = install_regions(read_layout(), files["loader"], files["firmware"], files["wifi"])
        uf2 = build_uf2(regions)
    except (OSError, ValueError) as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1

    with open(args.out, "wb") as f:
        f.write(uf2)
    print(f"{args.out}: {len(uf2) // 512} blocks; " +
          ", ".join(f"{name} {len(data)} B at 0x{XIP_BASE + offset:08X}" for name, offset, data, _ in regions))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
