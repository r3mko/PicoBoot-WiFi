"""Tests for tools/mkwifiblob.py (the Wi-Fi area image, design D14)."""

import hashlib
import os
import struct
import tempfile
import unittest

from helpers import load_tool, run_tool

wb = load_tool("mkwifiblob")


def sdk_header(fw_len, clm_len, data):
    """A small header in the cyw43-driver format."""
    body = ",\n  ".join(", ".join(f"0x{b:02x}" for b in data[i:i + 12]) for i in range(0, len(data), 12))
    return (f"static const unsigned char w43439A0_test[] CYW43_RESOURCE_ATTRIBUTE = {{\n  {body}\n}};\n"
            f"#define CYW43_WIFI_FW_LEN ({fw_len}) // launch_firmware/43439A0.bin\n"
            f"#define CYW43_CLM_LEN ({clm_len}) // clm\n"
            f"const uintptr_t fw_data = (uintptr_t)&w43439A0_test[0];\n")


class MkWifiBlobTest(unittest.TestCase):
    DATA = bytes(range(256)) * 2 + bytes([0xAA] * 100)      # 512 bytes of firmware, then 100 of CLM

    def test_marker_goes_into_the_spare_bytes(self):
        # A working blob that differs from the normal one (phase 3 update test); the firmware ignores it
        plain = wb.build_image(10, 4, bytes(520))
        marked = wb.build_image(10, 4, bytes(520), b"newblob")
        self.assertEqual(marked[20:32], b"newblob" + bytes(5))
        self.assertEqual(plain[:20], marked[:20])
        self.assertEqual(plain[32:], marked[32:])
        with self.assertRaises(ValueError):
            wb.build_image(10, 4, bytes(520), b"x" * 13)

    def test_parses_the_sdk_header(self):
        fw_len, clm_len, data = wb.parse_sdk_header(sdk_header(500, 100, self.DATA))
        self.assertEqual((fw_len, clm_len, data), (500, 100, self.DATA))

    def test_image_layout(self):
        image = wb.build_image(500, 100, self.DATA)
        magic, version, fw_len, clm_len, data_len, reserved, sha = wb.HEADER.unpack(image[:64])
        self.assertEqual((magic, version, fw_len, clm_len, data_len), (b"W43B", 1, 500, 100, len(self.DATA)))
        self.assertEqual(reserved, bytes(12))
        self.assertEqual(sha, hashlib.sha256(self.DATA).digest())
        self.assertEqual(image[64:], self.DATA)

    def test_array_too_short_for_the_clm_fails(self):
        with self.assertRaises(ValueError):
            wb.parse_sdk_header(sdk_header(500, 200, self.DATA))

    def test_not_a_firmware_header_fails(self):
        with self.assertRaises(ValueError):
            wb.parse_sdk_header("int x;\n")

    def test_command_line(self):
        with tempfile.TemporaryDirectory() as tmp:
            src, out = os.path.join(tmp, "fw.h"), os.path.join(tmp, "blob.bin")
            with open(src, "w") as f:
                f.write(sdk_header(500, 100, self.DATA))
            result = run_tool("mkwifiblob", src, out)
            self.assertEqual(result.returncode, 0, result.stderr)
            with open(out, "rb") as f:
                self.assertEqual(f.read()[64:], self.DATA)

    def test_the_wifi_area_fits_the_real_blob(self):
        # The SDK 2.3.1 Wi-Fi-only blob is 225,240 bytes (docs/research/sizes.md)
        self.assertGreaterEqual(wb.wifi_area_size(), 225240 + wb.HEADER_SIZE)


if __name__ == "__main__":
    unittest.main()
