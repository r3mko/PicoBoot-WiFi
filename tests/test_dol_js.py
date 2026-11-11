"""Tests for src/web/dol.js (the browser's .dol converter) against tools/process_ipl.py, byte for byte."""

import os
import random
import shutil
import struct
import subprocess
import tempfile
import unittest

from helpers import IPL_DOL, REPO, load_tool, make_dol

pi = load_tool("process_ipl")
NODE = shutil.which("node")
BASE = 0x81300000


@unittest.skipUnless(NODE, "node not found")
class DolJsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def js(self, dol):
        """(payload, None) or (None, message) from dol.js."""
        src, out = os.path.join(self.tmp.name, "in.dol"), os.path.join(self.tmp.name, "out.bin")
        with open(src, "wb") as f:
            f.write(dol)
        run = subprocess.run([NODE, os.path.join(REPO, "tests", "js", "dol_convert.js"), src, out],
                             capture_output=True, text=True)
        if run.returncode:
            return None, run.stderr
        with open(out, "rb") as f:
            return f.read(), None

    def same(self, dol):
        """dol.js and process_ipl.py agree: the same payload, or the same error message."""
        try:
            want, error = pi.dol_to_payload(dol), None
        except ValueError as e:
            want, error = None, str(e)
        self.assertEqual(self.js(dol), (want, error))
        return want

    def test_the_bundled_ipl(self):
        with open(os.path.join(REPO, IPL_DOL), "rb") as f:
            self.assertEqual(len(self.same(f.read())), 60548)

    def test_several_sections_gaps_and_odd_sizes(self):
        rnd = random.Random(7)
        sections = [(BASE, rnd.randbytes(1001)), (BASE + 0x2000, rnd.randbytes(77)),
                    (BASE + 0x1000, rnd.randbytes(4096)), (BASE + 0x9001, rnd.randbytes(3))]
        self.same(make_dol(sections, bss=(BASE + 0x10000, 0x5000)))
        self.same(make_dol(sections[::-1]))                 # out of address order
        self.same(make_dol([(BASE, b"\1" * 64)]))           # exactly one word multiple
        self.same(make_dol([(BASE, b"\1")]))

    def test_overlapping_sections_later_wins(self):
        self.same(make_dol([(BASE, b"\1" * 100), (BASE + 50, b"\2" * 100)]))

    def test_empty_section_with_a_lower_address_extends_the_image(self):
        self.same(make_dol([(BASE - 0x100, b""), (BASE, b"\3" * 32)]))

    def test_masked_addresses(self):
        self.same(make_dol([(0x01300000, b"\1" * 8)], entry=0xC1300000))
        self.same(make_dol([(0xC1300000, b"\1" * 8)], entry=0x01300000))

    def test_a_large_dol(self):
        rnd = random.Random(1)
        dol = make_dol([(BASE, rnd.randbytes(1_000_000)), (BASE + 0x100000, rnd.randbytes(300_001))])
        self.assertEqual(len(self.same(dol)), 32 + 0x100000 + 300_004 + 4)

    def test_error_messages(self):
        good = make_dol([(BASE, b"\1" * 16)])

        def patched(**words):
            header = list(struct.unpack(">64I", good[:256]))
            for index, value in words.items():
                header[int(index[1:])] = value
            return struct.pack(">64I", *header) + good[256:]

        for dol in (b"", good[:255],
                    make_dol([]),                                        # no sections
                    make_dol([(0, b"\1" * 16)]),                         # data, no load address
                    good[:-1],                                           # truncated
                    patched(w0=0xFFFFFFF0),                              # offset + size past 2^32
                    make_dol([(BASE, b"\1" * 16), (BASE + 0x01800001, b"\1")]),  # span over 24 MB
                    make_dol([(BASE, b"\1" * 16)], entry=0x81300004),    # wrong entry
                    make_dol([(BASE + 4, b"\1" * 16)]),                  # wrong base
                    make_dol([(BASE, b"\1" * 16)], entry=0),
                    patched(w18=0xFFFFFFFF, w36=0xFFFFFFFF)):
            with self.subTest(len(dol)):
                self.assertIsNone(self.same(dol))

    @unittest.skipUnless(os.environ.get("PICOBOOT_PAYLOAD_DIR"), "PICOBOOT_PAYLOAD_DIR not set")
    def test_the_projects_release_payloads_round_trip(self):
        # As in test_host.py: a release payload, descrambled and wrapped as a .dol, converts back to itself.
        for name in ("gekkoboot_pico.uf2", "swiss_r2119.uf2"):
            with self.subTest(name), open(os.path.join(os.environ["PICOBOOT_PAYLOAD_DIR"], name), "rb") as f:
                data = f.read()
                blocks = {}
                for off in range(0, len(data), 512):
                    addr, size, family = struct.unpack_from("<I I 8x I", data, off + 12)
                    blocks.setdefault(family, {})[addr] = data[off + 32:off + 32 + size]
                first = blocks[next(iter(blocks))]
                image = b"".join(first[a] for a in sorted(first))
                payload = image[:struct.unpack(">I", image[8:12])[0]]
                plain = bytes(pi.scramble(bytearray(0x720) + payload[32:-4])[0x720:])
                header = [0x100] + [0] * 17 + [BASE] + [0] * 17 + [len(plain)] + [0] * 17 + [0, 0, BASE] + [0] * 7
                dol = struct.pack(">64I", *header) + plain
                self.assertEqual(self.js(dol), (payload, None))
                self.assertEqual(self.same(dol), payload)


if __name__ == "__main__":
    unittest.main()
