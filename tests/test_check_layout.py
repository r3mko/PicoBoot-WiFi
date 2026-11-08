"""Tests for tests/check_layout.py (no firmware build needed)."""

import unittest

import check_layout


def real_layout():
    with open(check_layout.LAYOUT_H) as f:
        return check_layout.parse_layout(f.read())


class LayoutTest(unittest.TestCase):
    def test_layout_h_is_correct(self):
        self.assertEqual(check_layout.check_layout(real_layout()), [])

    def test_payload_1_is_where_picoboot_payloads_land(self):
        layout = real_layout()
        self.assertEqual(layout["PAYLOAD1_OFFSET"], 0x80000)
        self.assertEqual(layout["PAYLOAD1_SIZE"], 0x180000)

    def test_last_sector_is_reserved(self):
        layout = real_layout()
        self.assertEqual(layout["RESERVED_OFFSET"], 0x3FF000)

    def test_overlap_is_found(self):
        layout = real_layout()
        layout["SLOT_B_OFFSET"] -= 0x1000
        problems = check_layout.check_layout(layout)
        self.assertTrue(any("slot B" in p and "overlaps" in p for p in problems), problems)

    def test_unaligned_region_is_found(self):
        layout = real_layout()
        layout["WIFI_SIZE"] += 0x100
        problems = check_layout.check_layout(layout)
        self.assertTrue(any("not a multiple of the sector size" in p for p in problems), problems)

    def test_moving_payload_1_is_found(self):
        layout = real_layout()
        layout["WIFI_B_OFFSET"] += 0x1000
        layout["WIFI_SIZE"] -= 0x1000
        problems = check_layout.check_layout(layout)
        self.assertTrue(problems)

    def test_payload_2_up_to_the_end_of_flash_is_found(self):
        layout = real_layout()
        layout["PAYLOAD2_SIZE"] += 0x1000
        layout["RESERVED_OFFSET"] += 0x1000
        layout["RESERVED_SIZE"] = 0
        problems = check_layout.check_layout(layout)
        self.assertIn("the last sector of flash must stay reserved (design D37)", problems)


class ElfTest(unittest.TestCase):
    NM = "10000ab4 N __flash_binary_end\n10000000 T __logical_binary_start\n"

    def test_loader_that_fits_passes(self):
        values = check_layout.parse_nm(self.NM)
        self.assertEqual(check_layout.check_elf("loader", values, real_layout()), [])

    def test_loader_that_is_too_big_fails(self):
        values = check_layout.parse_nm(self.NM.replace("10000ab4", "10002010"))
        self.assertIn("outside", check_layout.check_elf("loader", values, real_layout())[0])

    def test_firmware_up_to_the_slot_size_passes(self):
        values = check_layout.parse_nm(self.NM.replace("10000ab4", "10040000"))
        self.assertEqual(check_layout.check_elf("firmware", values, real_layout()), [])

    def test_missing_symbol_fails(self):
        self.assertEqual(check_layout.check_elf("firmware", {}, real_layout()),
                         ["firmware: symbol not found: __flash_binary_end"])


if __name__ == "__main__":
    unittest.main()
