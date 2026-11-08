#!/usr/bin/env python3

"""
Builds the image of a Wi-Fi area (design D14): the CYW43439 firmware and CLM blob from the SDK's
cyw43-driver header, behind a small header, to store at LAYOUT_WIFI_A_OFFSET or _B:

    python3 tools/mkwifiblob.py <cyw43-driver>/firmware/w43439A0_7_95_49_00_combined.h wifi-blob.bin

Header (little-endian, 64 bytes), then the data exactly as the SDK header's array holds it
(firmware, padding to 512 bytes, CLM), which the firmware hands to the driver as fw_data:

    0   magic       "W43B"
    4   version     1
    8   fw_len      firmware bytes (CYW43_WIFI_FW_LEN)
    12  clm_len     CLM bytes (CYW43_CLM_LEN)
    16  data_len    bytes after the header
    20  reserved    12 bytes of zero
    32  sha256      of the data, to tell blobs apart (design D14)
"""

import argparse
import hashlib
import os
import re
import struct
import sys

MAGIC = b"W43B"
VERSION = 1
HEADER_SIZE = 64
HEADER = struct.Struct("<4sIIII12s32s")

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LAYOUT_H = os.path.join(REPO, "src", "common", "layout.h")


def parse_sdk_header(text):
    """(fw_len, clm_len, data) from a cyw43-driver firmware header."""
    fw = re.search(r"#define CYW43_WIFI_FW_LEN \((\d+)\)", text)
    clm = re.search(r"#define CYW43_CLM_LEN \((\d+)\)", text)
    array = re.search(r"\[\]\s*(?:CYW43_RESOURCE_ATTRIBUTE\s*)?=\s*\{(.*?)\};", text, re.S)
    if not (fw and clm and array):
        raise ValueError("not a cyw43-driver firmware header (no CYW43_WIFI_FW_LEN, CYW43_CLM_LEN or array)")
    data = bytes(int(b, 16) for b in re.findall(r"0x([0-9a-fA-F]{2})", array.group(1)))
    fw_len, clm_len = int(fw.group(1)), int(clm.group(1))
    clm_offset = (fw_len + 511) // 512 * 512
    if clm_offset + clm_len > len(data):
        raise ValueError(f"the array ({len(data)} bytes) is shorter than the firmware and CLM it should hold")
    return fw_len, clm_len, data


def build_image(fw_len, clm_len, data):
    header = HEADER.pack(MAGIC, VERSION, fw_len, clm_len, len(data), bytes(12), hashlib.sha256(data).digest())
    assert len(header) == HEADER_SIZE
    return header + data


def wifi_area_size():
    with open(LAYOUT_H) as f:
        return int(re.search(r"#define LAYOUT_WIFI_SIZE\s+(0x[0-9A-Fa-f]+)", f.read()).group(1), 16)


def main(argv):
    parser = argparse.ArgumentParser(description="Build a Wi-Fi area image from the SDK's CYW43 firmware header.")
    parser.add_argument("header", help="cyw43-driver firmware header, e.g. w43439A0_7_95_49_00_combined.h")
    parser.add_argument("output", help="image to write")
    args = parser.parse_args(argv[1:])
    try:
        with open(args.header) as f:
            fw_len, clm_len, data = parse_sdk_header(f.read())
    except (OSError, ValueError) as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1
    image = build_image(fw_len, clm_len, data)
    limit = wifi_area_size()
    if len(image) > limit:
        print(f"Error: the image is {len(image)} bytes, more than a Wi-Fi area ({limit})", file=sys.stderr)
        return 1
    with open(args.output, "wb") as f:
        f.write(image)
    print(f"{args.output}: firmware {fw_len} + CLM {clm_len} bytes, image {len(image)} of {limit} bytes, "
          f"sha256 {hashlib.sha256(data).hexdigest()[:16]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
