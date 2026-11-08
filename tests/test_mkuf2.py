"""Tests for tools/mkuf2.py (the USB install UF2, design D24, D37)."""

import os
import tempfile
import unittest

from helpers import load_tool, run_tool

mk = load_tool("mkuf2")
LAYOUT = mk.read_layout()
XIP = mk.XIP_BASE


def build(loader=b"L" * 2740, firmware=b"F" * 186972, wifi=b"W" * 225304):
    return mk.read_uf2(mk.build_uf2(mk.install_regions(LAYOUT, loader, firmware, wifi)))


def covered(blocks):
    """Set of page addresses the UF2 writes."""
    return {address for address, *_ in blocks}


class InstallUf2Test(unittest.TestCase):
    def test_blocks_are_well_formed(self):
        blocks = build()
        total = len(blocks)
        for n, (address, payload, family, block_no, num_blocks, flags) in enumerate(blocks):
            self.assertEqual((family, block_no, num_blocks, flags), (mk.FAMILY_RP2350_ARM_S, n, total, 0x2000))
            self.assertEqual(len(payload), 256)
            self.assertEqual(address % 256, 0)

    def test_regions_land_where_layout_h_says(self):
        pages = covered(build())
        for name, offset_key, length in [("loader", "LOADER_OFFSET", 2740), ("Wi-Fi A", "WIFI_A_OFFSET", 225304),
                                         ("slot A", "SLOT_A_OFFSET", 186972)]:
            start = XIP + LAYOUT[offset_key]
            with self.subTest(name):
                self.assertTrue(all(a in pages for a in range(start, start + length, 256)))

    def test_boot_control_and_settings_are_written_blank(self):
        blocks = build()
        for key, size_key in [("BOOTCTL_OFFSET", "BOOTCTL_SIZE"), ("SETTINGS_OFFSET", "SETTINGS_SIZE")]:
            start, end = XIP + LAYOUT[key], XIP + LAYOUT[key] + LAYOUT[size_key]
            area = [payload for address, payload, *_ in blocks if start <= address < end]
            self.assertEqual(len(area), LAYOUT[size_key] // 256)
            self.assertTrue(all(p == b"\xff" * 256 for p in area))

    def test_payloads_and_the_reserved_sector_are_untouched(self):
        pages = covered(build())
        for key, size_key in [("PAYLOAD1_OFFSET", "PAYLOAD1_SIZE"), ("PAYLOAD2_OFFSET", "PAYLOAD2_SIZE"),
                              ("RESERVED_OFFSET", "RESERVED_SIZE")]:
            start, end = XIP + LAYOUT[key], XIP + LAYOUT[key] + LAYOUT[size_key]
            with self.subTest(key):
                self.assertFalse([a for a in pages if start <= a < end])
        self.assertFalse([a for a in pages if a >= 0x10FFFF00], "no picotool E10 block (design D37)")

    def test_padding_is_ff(self):
        blocks = build(loader=b"L" * 300)
        loader_pages = [payload for address, payload, *_ in blocks if address < XIP + 0x200]
        self.assertEqual(loader_pages[1], b"L" * 44 + b"\xff" * 212)

    def test_too_big_firmware_fails(self):
        with self.assertRaises(ValueError):
            build(firmware=b"F" * (LAYOUT["SLOT_SIZE"] + 1))

    def test_command_line(self):
        with tempfile.TemporaryDirectory() as tmp:
            paths = {}
            for name, size in (("loader", 100), ("firmware", 1000), ("wifi", 500)):
                paths[name] = os.path.join(tmp, name + ".bin")
                with open(paths[name], "wb") as f:
                    f.write(bytes([0x5A]) * size)
            out = os.path.join(tmp, "install.uf2")
            result = run_tool("mkuf2", "--loader", paths["loader"], "--firmware", paths["firmware"],
                              "--wifi", paths["wifi"], "--out", out)
            self.assertEqual(result.returncode, 0, result.stderr)
            with open(out, "rb") as f:
                blocks = mk.read_uf2(f.read())
            # loader 1 page + boot control 32 + settings 32 + Wi-Fi 2 + firmware 4
            self.assertEqual(len(blocks), 1 + 32 + 32 + 2 + 4)


if __name__ == "__main__":
    unittest.main()
