"""Tests for tools/mkupdate.py (the over-the-air update file, docs/update-format.md)."""

import hashlib
import os
import re
import struct
import tempfile
import unittest

from helpers import REPO, load_tool, run_tool

mk = load_tool("mkupdate")
C = mk.read_constants()
BUILD_DIR = os.environ.get("PICOBOOT_BUILD_DIR")

IMAGE = bytes(range(256)) * 700            # 179200 bytes
BLOB = b"W43B" + bytes(60) + b"blob data" * 1000
VERSION = "0.3.0-dev+1a2b3c4"


def sha(data):
    return hashlib.sha256(data).digest()


def build(image=IMAGE, blob=BLOB, version=VERSION, board="pico2_w", with_blob=True):
    return mk.build_update(image, blob, version, board, with_blob)


def rehash(data):
    """Fix the header hash after a test changed a header byte."""
    data = bytearray(data)
    data[224:256] = sha(bytes(data[:224]))
    return bytes(data)


def patch(data, offset, value):
    data = bytearray(data)
    data[offset:offset + len(value)] = value
    return bytes(data)


def refused(test, data, code):
    with test.assertRaises(mk.UpdateError) as cm:
        mk.parse_update(data)
    test.assertEqual(cm.exception.code, code)
    return str(cm.exception)


class ConstantsTest(unittest.TestCase):
    def test_values_come_from_the_headers(self):
        self.assertEqual(C, {"layout": 1, "slot_size": 0x40000, "wifi_size": 0x3C000, "loader": 2})


class HeaderTest(unittest.TestCase):
    def test_fields_at_the_documented_offsets(self):
        data = build()
        magic, fmt, header_size, flags, layout, loader, image_size, blob_size, reserved = \
            struct.unpack_from("<4sHHIIIIII", data, 0)
        self.assertEqual((magic, fmt, header_size, flags), (b"PBWU", 1, 256, 1))
        self.assertEqual((layout, loader, reserved), (C["layout"], C["loader"], 0))
        self.assertEqual((image_size, blob_size), (len(IMAGE), len(BLOB)))
        self.assertEqual(data[32:48], b"pico2_w" + bytes(9))
        self.assertEqual(data[48:80], VERSION.encode() + bytes(32 - len(VERSION)))
        self.assertEqual(data[80:112], sha(IMAGE))
        self.assertEqual(data[112:144], sha(BLOB))
        self.assertEqual(data[144:224], bytes(80))
        self.assertEqual(data[224:256], sha(data[:224]))

    def test_total_length_and_parts(self):
        data = build()
        self.assertEqual(len(data), 256 + len(IMAGE) + len(BLOB))
        self.assertEqual(data[256:256 + len(IMAGE)], IMAGE)
        self.assertEqual(data[256 + len(IMAGE):], BLOB)

    def test_no_blob(self):
        data = build(with_blob=False)
        self.assertEqual(len(data), 256 + len(IMAGE))
        self.assertEqual(struct.unpack_from("<I", data, 8)[0], 0)
        self.assertEqual(struct.unpack_from("<I", data, 24)[0], 0)
        self.assertEqual(data[112:144], sha(BLOB))
        info = mk.parse_update(data)
        self.assertEqual((info["flags"], info["blob_size"], info["blob"]), (0, 0, b""))

    def test_deterministic(self):
        self.assertEqual(build(), build())

    def test_limits_are_accepted(self):
        build(image=b"x", blob=b"W43B" + bytes(60))
        build(image=bytes(C["slot_size"]), blob=b"W43B" + bytes(C["wifi_size"] - 4))
        build(version="v" * 31, board="b" * 15)


class BuilderRefusalTest(unittest.TestCase):
    def refuses(self, **kwargs):
        with self.assertRaises(ValueError):
            build(**kwargs)

    def test_image(self):
        self.refuses(image=b"")
        self.refuses(image=bytes(C["slot_size"] + 1))

    def test_blob(self):
        self.refuses(blob=b"")
        self.refuses(blob=b"W43B" + bytes(59))
        self.refuses(blob=b"XXXX" + bytes(100))
        self.refuses(blob=b"W43B" + bytes(C["wifi_size"] - 3))

    def test_version(self):
        self.refuses(version="")
        self.refuses(version="v" * 32)
        self.refuses(version="1.0\n")
        self.refuses(version="1.0é")
        self.refuses(version="1.0\x00")

    def test_board(self):
        self.refuses(board="")
        self.refuses(board="b" * 16)
        self.refuses(board="pico\t2")
        self.refuses(board="pïco")


class ParseTest(unittest.TestCase):
    def test_roundtrip(self):
        info = mk.parse_update(build())
        self.assertEqual(info["version"], VERSION)
        self.assertEqual(info["board"], "pico2_w")
        self.assertEqual((info["format"], info["flags"], info["layout"], info["loader"]), (1, 1, 1, 2))
        self.assertEqual((info["image_size"], info["blob_size"]), (len(IMAGE), len(BLOB)))
        self.assertEqual(info["image_sha256"], sha(IMAGE).hex())
        self.assertEqual(info["blob_sha256"], sha(BLOB).hex())
        self.assertEqual((info["image"], info["blob"]), (IMAGE, BLOB))

    def test_not_an_update(self):
        refused(self, b"", "not_an_update")
        refused(self, b"PBW", "not_an_update")
        refused(self, patch(build(), 0, b"XBWU"), "not_an_update")
        self.assertIn("UF2", refused(self, b"UF2\n" + bytes(1000), "not_an_update"))
        self.assertNotIn("UF2", refused(self, b"ABCD" + bytes(1000), "not_an_update"))

    def test_short_file(self):
        refused(self, build()[:255], "bad_header")

    def test_format_and_header_size(self):
        refused(self, rehash(patch(build(), 4, struct.pack("<H", 2))), "unsupported_format")
        refused(self, rehash(patch(build(), 6, struct.pack("<H", 128))), "unsupported_format")

    def test_header_hash(self):
        data = build()
        for offset in (8, 12, 20, 32, 48, 80, 112, 200, 224, 255):
            refused(self, patch(data, offset, bytes([data[offset] ^ 1])), "bad_header")

    def test_board_and_layout(self):
        refused(self, rehash(patch(build(), 32, b"pico_w")), "wrong_board")
        refused(self, rehash(patch(build(), 12, struct.pack("<I", 2))), "needs_usb_install")

    def test_text_fields(self):
        refused(self, rehash(patch(build(), 32, b"pico2_w" + b"\0" * 7 + b"x\0")), "bad_header")
        refused(self, rehash(patch(build(), 32, b"pico2_w" * 2 + b"xx")), "bad_header")
        refused(self, rehash(patch(build(), 48, b"\x01" * 32)), "bad_header")
        refused(self, rehash(patch(build(), 48, b"a" * 32)), "bad_header")
        refused(self, rehash(patch(build(), 48, bytes(32))), "bad_header")
        refused(self, rehash(patch(build(), 48, b"1.0\0\0junk")), "bad_header")

    def test_flags(self):
        refused(self, rehash(patch(build(), 8, struct.pack("<I", 3))), "bad_header")
        refused(self, rehash(patch(build(), 8, struct.pack("<I", 0x80000001))), "bad_header")
        refused(self, rehash(patch(build(), 8, struct.pack("<I", 0))), "bad_header")  # blob_size without flag
        refused(self, rehash(patch(build(with_blob=False), 8, struct.pack("<I", 1))), "bad_header")

    def test_reserved(self):
        for offset in (28, 31, 144, 200, 223):
            refused(self, rehash(patch(build(), offset, b"\1")), "bad_header")

    def test_sizes_in_range(self):
        for size in (0, C["slot_size"] + 1, 0xFFFFFFFF):
            refused(self, rehash(patch(build(), 20, struct.pack("<I", size))), "bad_header")
        for size in (1, 63, C["wifi_size"] + 1, 0xFFFFFFFF):
            refused(self, rehash(patch(build(), 24, struct.pack("<I", size))), "bad_header")

    def test_total_length(self):
        data = build()
        refused(self, data[:-1], "bad_size")
        refused(self, data + b"\0", "bad_size")
        refused(self, build(with_blob=False) + b"\0", "bad_size")
        refused(self, rehash(patch(data, 20, struct.pack("<I", len(IMAGE) - 1))), "bad_size")

    def test_image_hash(self):
        data = build()
        for offset in (256, 256 + len(IMAGE) // 2, 256 + len(IMAGE) - 1):
            refused(self, patch(data, offset, bytes([data[offset] ^ 1])), "bad_image")
        refused(self, rehash(patch(data, 80, bytes(32))), "bad_image")

    def test_blob_hash(self):
        data = build()
        for offset in (256 + len(IMAGE), len(data) - len(BLOB) // 2, len(data) - 1):
            refused(self, patch(data, offset, bytes([data[offset] ^ 1])), "bad_blob")
        refused(self, rehash(patch(data, 112, bytes(32))), "bad_blob")

    def test_no_blob_ignores_blob_hash(self):
        mk.parse_update(rehash(patch(build(with_blob=False), 112, bytes(32))))


class CommandLineTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = lambda name: os.path.join(self.tmp.name, name)
        for name, data in (("fw.bin", IMAGE), ("blob.bin", BLOB)):
            with open(self.path(name), "wb") as f:
                f.write(data)

    def make(self, *extra, version=VERSION):
        return run_tool("mkupdate", "--firmware", self.path("fw.bin"), "--blob", self.path("blob.bin"),
                        "--version", version, "--board", "pico2_w", "-o", self.path("up.bin"), *extra)

    def test_build_and_info(self):
        r = self.make()
        self.assertEqual(r.returncode, 0, r.stderr)
        with open(self.path("up.bin"), "rb") as f:
            self.assertEqual(f.read(), build())
        r = run_tool("mkupdate", "--info", self.path("up.bin"))
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn(VERSION, r.stdout)
        self.assertTrue(r.stdout.rstrip().endswith("OK"))

    def test_no_blob_flag(self):
        self.assertEqual(self.make("--no-blob").returncode, 0)
        with open(self.path("up.bin"), "rb") as f:
            self.assertEqual(f.read(), build(with_blob=False))

    def test_refusal_exits_non_zero(self):
        r = self.make(version="v" * 32)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("version", r.stderr)
        self.assertFalse(os.path.exists(self.path("up.bin")))

    def test_info_reports_the_problem(self):
        with open(self.path("bad.bin"), "wb") as f:
            f.write(b"UF2\n" + bytes(600))
        r = run_tool("mkupdate", "--info", self.path("bad.bin"))
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("UF2", r.stderr)
        self.assertNotIn("OK", r.stdout)

    def test_missing_arguments(self):
        r = run_tool("mkupdate", "--firmware", self.path("fw.bin"))
        self.assertNotEqual(r.returncode, 0)


@unittest.skipUnless(BUILD_DIR, "set PICOBOOT_BUILD_DIR to a build directory")
class BuiltUpdateTest(unittest.TestCase):
    """The update file a build made holds that build's firmware and Wi-Fi blob, byte for byte."""

    def read(self, *path):
        with open(os.path.join(BUILD_DIR, *path), "rb") as f:
            return f.read()

    def test_built_update_holds_the_built_parts(self):
        info = mk.parse_update(self.read("picoboot-wifi-update.bin"))
        self.assertEqual(info["image"], self.read("src", "firmware", "firmware.bin"))
        self.assertEqual(info["blob"], self.read("wifi-blob.bin"))
        self.assertEqual(info["board"], "pico2_w")
        with open(os.path.join(REPO, "CMakeLists.txt")) as f:
            version = re.search(r'set\(PICOBOOT_WIFI_VERSION "([^"]+)"\)', f.read()).group(1)
        self.assertTrue(info["version"].startswith(version), info["version"])
