"""Tests for tools/gekkoboot_dol.py (a gekkoboot release's apploader.img -> the built-in IPL's .dol)."""

import hashlib
import os
import struct
import tempfile
import unittest
import zlib

from helpers import IPL_DOL, REPO, load_tool, run_tool

gd = load_tool("gekkoboot_dol")
pi = load_tool("process_ipl")


def make_image(text=0x40, data=0x40, sbss_size=0x100, sbss_offset=0, crt0=True, r13_set_up=True):
    """A made-up packer image: its code with crt0's r13 set-up and .sbss clear, then an "xz stream".
    The .sbss starts sbss_offset bytes after the image's end; r13_set_up=False leaves it out."""
    start = gd.BASE - 0x80000000 + text + data + sbss_offset    # The packer's own address
    r13 = start + 0x7FE0                                         # So __sbss_start@sdarel is -0x7FE0
    ha = ((r13 + 0x8000) >> 16) & 0xFFFF
    lo = (r13 - (ha << 16)) & 0xFFFF
    words = [0x3C400000, (gd.LIS_R13 << 16) | ha if r13_set_up else 0,
             0x38420000, (gd.ADDI_R13 << 16) | lo if r13_set_up else 0,
             (gd.ADDI_R3_R13 << 16) | 0x8020, (gd.ADDI_R4_R13 << 16) | ((0x8020 + sbss_size) & 0xFFFF),
             gd.SUBF_R4_R3_R4 if crt0 else 0, 0x48000000]
    stub = struct.pack(f">{len(words)}I", *words).ljust(text, b"\0")
    return stub + (gd.XZ_MAGIC + bytes(range(16))).ljust(data, b"\xaa")


def apploader(image, date=b"*gekkoboot", size=None, crc=None):
    return struct.pack(">16s8xII", date, len(image) if size is None else size,
                       zlib.crc32(image) if crc is None else crc) + image


class LayoutTest(unittest.TestCase):
    def setUp(self):
        self.image = make_image()
        self.dol = gd.dol_from_image(self.image)
        self.header = struct.unpack(">64I", self.dol[:256])

    def test_text_is_the_packer_up_to_the_xz_stream(self):
        self.assertEqual(self.header[0], 0x100)
        self.assertEqual(self.header[18], gd.BASE)
        self.assertEqual(self.header[36], 0x40)

    def test_data_is_the_rest(self):
        self.assertEqual(self.header[7], 0x140)
        self.assertEqual(self.header[25], gd.BASE + 0x40)
        self.assertEqual(self.header[43], 0x40)

    def test_bss_is_the_sbss_that_crt0_clears(self):
        self.assertEqual(self.header[54:56], (gd.BASE + 0x80, 0x100))

    def test_entry_and_nothing_else(self):
        self.assertEqual(self.header[56], gd.BASE)
        used = {0, 7, 18, 25, 36, 43, 54, 55, 56}
        self.assertEqual([w for i, w in enumerate(self.header) if i not in used], [0] * (64 - len(used)))
        self.assertEqual(self.dol[256:], self.image)

    def test_process_ipl_reads_the_image_back(self):
        load, entry, image = pi.flatten_dol(bytearray(self.dol))
        self.assertEqual((load, entry, bytes(image)), (gd.BASE, gd.BASE, self.image))


class RefusalTest(unittest.TestCase):
    def test_no_xz_stream(self):
        image = make_image().replace(gd.XZ_MAGIC, bytes(6))
        with self.assertRaisesRegex(ValueError, "xz"):
            gd.dol_from_image(image)

    def test_xz_stream_off_a_32_byte_boundary(self):
        with self.assertRaisesRegex(ValueError, "xz"):
            gd.dol_from_image(make_image(text=0x50, data=0x30))

    def test_image_not_whole_32_byte_blocks(self):
        with self.assertRaisesRegex(ValueError, "32-byte blocks"):
            gd.dol_from_image(make_image(data=0x44))

    def test_no_crt0(self):
        with self.assertRaisesRegex(ValueError, "crt0"):
            gd.dol_from_image(make_image(crt0=False))

    def test_no_r13_set_up(self):
        with self.assertRaisesRegex(ValueError, "crt0"):
            gd.dol_from_image(make_image(r13_set_up=False))

    def test_empty_sbss(self):
        with self.assertRaisesRegex(ValueError, "0 bytes"):
            gd.dol_from_image(make_image(sbss_size=0))

    def test_sbss_not_right_after_the_image(self):
        with self.assertRaisesRegex(ValueError, "doesn't follow the image"):
            gd.dol_from_image(make_image(sbss_offset=32))


class ApploaderTest(unittest.TestCase):
    def test_the_image_after_the_header(self):
        image = make_image()
        self.assertEqual(gd.image_from_apploader(apploader(image)), image)

    def test_refuses_a_wrong_header(self):
        image = make_image()
        for name, data in (("short", b"*gekkoboot"),
                           ("date", apploader(image, date=b"*other")),
                           ("size", apploader(image, size=len(image) - 32)),
                           ("crc", apploader(image, crc=zlib.crc32(image) ^ 1))):
            with self.subTest(name), self.assertRaises(ValueError):
                gd.image_from_apploader(data)


class AssetTest(unittest.TestCase):
    """The asset is the tool's output for its own image, so its header can't drift from the tool's."""

    def test_the_asset_is_the_tools_dol(self):
        with open(os.path.join(REPO, IPL_DOL), "rb") as f:
            asset = f.read()
        self.assertEqual(hashlib.sha256(asset).hexdigest(),
                         "6172f4a802968fb819b0a595812ec8c0495ef7c40cc3e1c734253b384a15fe62")
        self.assertEqual(gd.dol_from_image(asset[256:]), asset)


class CommandLineTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.input = os.path.join(self.tmp.name, "apploader.img")
        self.output = os.path.join(self.tmp.name, "gekkoboot.dol")

    def test_writes_the_dol(self):
        image = make_image()
        with open(self.input, "wb") as f:
            f.write(apploader(image))
        result = run_tool("gekkoboot_dol", self.input, self.output)
        self.assertEqual(result.returncode, 0, result.stderr)
        with open(self.output, "rb") as f:
            self.assertEqual(f.read(), gd.dol_from_image(image))

    def test_a_missing_input(self):
        result = run_tool("gekkoboot_dol", self.input, self.output)
        self.assertEqual(result.returncode, 1)
        self.assertNotIn("Traceback", result.stderr)
        self.assertFalse(os.path.exists(self.output))

    def test_refuses_without_writing(self):
        with open(self.input, "wb") as f:
            f.write(apploader(make_image(crt0=False)))
        result = run_tool("gekkoboot_dol", self.input, self.output)
        self.assertEqual(result.returncode, 1)
        self.assertIn("crt0", result.stderr)
        self.assertFalse(os.path.exists(self.output))


@unittest.skipUnless(os.environ.get("PICOBOOT_PAYLOAD_DIR"), "PICOBOOT_PAYLOAD_DIR not set")
class ReleaseTest(unittest.TestCase):
    """The built-in IPL is gekkoboot r9.4's release build (docs/research/gekkoboot-dol.md). With
    PICOBOOT_PAYLOAD_DIR set to a folder holding the release's gekkoboot_pico.uf2 and, optionally, its
    swiss_igr/swiss/patches/apploader.img copied in as apploader.img."""

    @classmethod
    def setUpClass(cls):
        cls.folder = os.environ["PICOBOOT_PAYLOAD_DIR"]
        with open(os.path.join(REPO, IPL_DOL), "rb") as f:
            cls.asset = f.read()

    def test_the_asset_makes_the_release_pico_payload(self):
        with open(os.path.join(self.folder, "gekkoboot_pico.uf2"), "rb") as f:
            data = f.read()
        blocks = {}
        for off in range(0, len(data), 512):
            address, size = struct.unpack_from("<II", data, off + 12)
            blocks[address] = data[off + 32:off + 32 + size]
        image = b"".join(blocks[a] for a in sorted(blocks))
        payload = image[:struct.unpack(">I", image[8:12])[0]]
        self.assertEqual(pi.dol_to_payload(self.asset), payload)

    def test_the_tool_rebuilds_the_asset_from_the_release(self):
        path = os.path.join(self.folder, "apploader.img")
        if not os.path.exists(path):
            self.skipTest("no apploader.img in PICOBOOT_PAYLOAD_DIR")
        with open(path, "rb") as f:
            self.assertEqual(gd.dol_from_image(gd.image_from_apploader(f.read())), self.asset)


if __name__ == "__main__":
    unittest.main()
