#!/usr/bin/env python3

"""
Builds the over-the-air update file (docs/update-format.md, design D11): a 256-byte header, the slot
image (firmware.bin), then the Wi-Fi blob (wifi-blob.bin).

    python3 tools/mkupdate.py --firmware build/src/firmware/firmware.bin --blob build/wifi-blob.bin \\
        --version 0.3.0-dev+1a2b3c4 --board pico2_w -o build/picoboot-wifi-update.bin
    python3 tools/mkupdate.py --info build/picoboot-wifi-update.bin

--no-blob leaves the blob out (flag bit 0 clear, blob_size 0); blob_sha256 still names the given blob.
The layout version, the area sizes and the loader version come from src/common/layout.h and bootinfo.h.
"""

import argparse
import hashlib
import os
import re
import struct
import sys

MAGIC = b"PBWU"
UF2_MAGIC = b"UF2\n"
FORMAT = 1
HEADER_SIZE = 256
FLAG_BLOB = 1
BLOB_MAGIC = b"W43B"
BLOB_HEADER_SIZE = 64
BOARD = "pico2_w"
BOARD_FIELD = 16
VERSION_FIELD = 32

# magic, format, header_size, flags, layout, loader, image_size, blob_size, reserved, board, version,
# image_sha256, blob_sha256, reserved (80 bytes); header_sha256 (32 bytes) follows at 224
HEADER = struct.Struct("<4sHHIIIII4x16s32s32s32s80s")
HASHED = HEADER.size
assert HASHED == 224 and HASHED + 32 == HEADER_SIZE

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LAYOUT_H = os.path.join(REPO, "src", "common", "layout.h")
BOOTINFO_H = os.path.join(REPO, "src", "common", "bootinfo.h")


class UpdateError(ValueError):
    """A problem with an update file; `code` is the name the Pico refuses it with."""

    def __init__(self, code, message):
        super().__init__(f"{code}: {message}")
        self.code = code


def read_define(path, name):
    with open(path) as f:
        m = re.search(rf"^#define\s+{name}\s+(0[xX][0-9A-Fa-f]+|\d+)", f.read(), re.M)
    if not m:
        raise ValueError(f"no #define {name} in {path}")
    return int(m.group(1), 0)


def read_constants(layout_h=LAYOUT_H, bootinfo_h=BOOTINFO_H):
    """Dict with layout, slot_size, wifi_size and loader, from the C headers."""
    return {
        "layout": read_define(layout_h, "LAYOUT_VERSION"),
        "slot_size": read_define(layout_h, "LAYOUT_SLOT_SIZE"),
        "wifi_size": read_define(layout_h, "LAYOUT_WIFI_SIZE"),
        "loader": read_define(bootinfo_h, "BOOTINFO_LOADER_NEEDED"),
    }


def check_text(name, value, max_chars):
    if not value or not all(0x20 <= ord(c) < 0x7F for c in value):
        raise ValueError(f"{name} must be 1 or more printable ASCII characters: {value!r}")
    if len(value) > max_chars:
        raise ValueError(f"{name} is {len(value)} characters, at most {max_chars} fit: {value!r}")


def check_blob(blob, wifi_size):
    if len(blob) < BLOB_HEADER_SIZE:
        raise ValueError(f"the Wi-Fi blob is {len(blob)} bytes, less than its {BLOB_HEADER_SIZE}-byte header")
    if blob[:4] != BLOB_MAGIC:
        raise ValueError("the Wi-Fi blob does not start with W43B (not a tools/mkwifiblob.py image)")
    if len(blob) > wifi_size:
        raise ValueError(f"the Wi-Fi blob is {len(blob)} bytes, more than a Wi-Fi area ({wifi_size})")


def build_update(image, blob, version, board, with_blob=True, constants=None):
    c = constants or read_constants()
    if not image:
        raise ValueError("the firmware image is empty")
    if len(image) > c["slot_size"]:
        raise ValueError(f"the firmware image is {len(image)} bytes, more than a slot ({c['slot_size']})")
    check_blob(blob, c["wifi_size"])
    check_text("version", version, VERSION_FIELD - 1)
    check_text("board", board, BOARD_FIELD - 1)
    header = HEADER.pack(
        MAGIC, FORMAT, HEADER_SIZE, FLAG_BLOB if with_blob else 0, c["layout"], c["loader"],
        len(image), len(blob) if with_blob else 0, board.encode(), version.encode(),
        hashlib.sha256(image).digest(), hashlib.sha256(blob).digest(), bytes(80),
    )
    header += hashlib.sha256(header).digest()
    return header + image + (blob if with_blob else b"")


def text_field(name, raw):
    """A zero-padded ASCII string field, which the Pico reads as a C string."""
    text, nul, rest = raw.partition(b"\0")
    if not nul or any(rest) or not text or not all(0x20 <= b < 0x7F for b in text):
        raise UpdateError("bad_header", f"{name} is not a zero-padded printable ASCII string")
    return text.decode()


def parse_update(data, constants=None):
    """
    Checks a whole update file as far as the file alone allows (the checks of "What the Pico checks"
    except the installed loader's version, which only the Pico knows). Returns a dict of the header
    fields plus `image` and `blob` (b"" if not included); raises UpdateError.
    """
    c = constants or read_constants()
    if data[:4] == UF2_MAGIC:
        raise UpdateError("not_an_update", "this is a UF2 file, the USB install file, not an update file")
    if data[:4] != MAGIC:
        raise UpdateError("not_an_update", "the file does not start with PBWU")
    if len(data) < HEADER_SIZE:
        raise UpdateError("bad_header", f"the file is {len(data)} bytes, shorter than the {HEADER_SIZE}-byte header")
    (_, fmt, header_size, flags, layout, loader, image_size, blob_size, board, version,
     image_sha, blob_sha, reserved2) = HEADER.unpack_from(data)
    reserved = data[28:32]
    if fmt != FORMAT or header_size != HEADER_SIZE:
        raise UpdateError("unsupported_format", f"format {fmt}, header size {header_size}; expected {FORMAT}, {HEADER_SIZE}")
    if hashlib.sha256(data[:HASHED]).digest() != data[HASHED:HEADER_SIZE]:
        raise UpdateError("bad_header", "the header's SHA-256 does not match")
    board_text = text_field("board", board)
    if board_text != BOARD:
        raise UpdateError("wrong_board", f"the file is for {board_text}, not {BOARD}")
    if layout != c["layout"]:
        raise UpdateError("needs_usb_install", f"flash layout {layout}, this tool's is {c['layout']}")
    version_text = text_field("version", version)
    if flags & ~FLAG_BLOB:
        raise UpdateError("bad_header", f"unknown flag bits 0x{flags:08X}")
    if any(reserved) or any(reserved2):
        raise UpdateError("bad_header", "reserved bytes are not 0")
    if not 1 <= image_size <= c["slot_size"]:
        raise UpdateError("bad_header", f"image_size {image_size} is not 1 to {c['slot_size']}")
    if flags & FLAG_BLOB:
        if not BLOB_HEADER_SIZE <= blob_size <= c["wifi_size"]:
            raise UpdateError("bad_header", f"blob_size {blob_size} is not {BLOB_HEADER_SIZE} to {c['wifi_size']}")
    elif blob_size:
        raise UpdateError("bad_header", f"blob_size is {blob_size} but the blob flag is clear")
    expected = HEADER_SIZE + image_size + blob_size
    if len(data) != expected:
        raise UpdateError("bad_size", f"the file is {len(data)} bytes, the header says {expected}")
    image = data[HEADER_SIZE:HEADER_SIZE + image_size]
    blob = data[HEADER_SIZE + image_size:]
    if hashlib.sha256(image).digest() != image_sha:
        raise UpdateError("bad_image", "the image's SHA-256 does not match")
    if blob and hashlib.sha256(blob).digest() != blob_sha:
        raise UpdateError("bad_blob", "the Wi-Fi blob's SHA-256 does not match")
    return {
        "format": fmt, "flags": flags, "layout": layout, "loader": loader, "image_size": image_size,
        "blob_size": blob_size, "board": board_text, "version": version_text,
        "image_sha256": image_sha.hex(), "blob_sha256": blob_sha.hex(), "image": image, "blob": blob,
    }


def read_file(path):
    with open(path, "rb") as f:
        return f.read()


def main(argv):
    p = argparse.ArgumentParser(description="Build or inspect a PicoBoot-WiFi over-the-air update file.")
    p.add_argument("--info", metavar="FILE", help="check and describe an update file instead of building one")
    p.add_argument("--firmware", help="slot image (firmware.bin)")
    p.add_argument("--blob", help="Wi-Fi blob (wifi-blob.bin); always needed, for blob_sha256")
    p.add_argument("--version", help="firmware version and build, at most 31 characters")
    p.add_argument("--board", help="board name, at most 15 characters")
    p.add_argument("--no-blob", action="store_true", help="leave the blob out of the file")
    p.add_argument("-o", "--output", help="update file to write")
    args = p.parse_args(argv[1:])
    try:
        if args.info:
            try:
                info = parse_update(read_file(args.info))
            except UpdateError as e:
                print(f"{args.info}: {e}", file=sys.stderr)
                return 1
            for key, value in info.items():
                if key not in ("image", "blob"):
                    print(f"{key:12} {value}")
            print("OK")
            return 0
        missing = [o for o in ("firmware", "blob", "version", "board", "output") if not getattr(args, o)]
        if missing:
            p.error("needed to build: " + ", ".join("--" + m for m in missing))
        data = build_update(read_file(args.firmware), read_file(args.blob), args.version, args.board,
                            with_blob=not args.no_blob)
    except (OSError, ValueError) as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1
    with open(args.output, "wb") as f:
        f.write(data)
    print(f"{args.output}: {len(data)} bytes, version {args.version}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
