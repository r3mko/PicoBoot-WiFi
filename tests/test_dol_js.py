"""Tests for src/web/dol.js (the browser's .dol converter) against tools/process_ipl.py, byte for byte."""

import os
import random
import re
import shutil
import struct
import subprocess
import tempfile
import unittest

from helpers import IPL_DOL, REPO, load_tool, make_dol
from test_dolstub import run as run_stub

pi = load_tool("process_ipl")
ds = load_tool("dolstub")
cli = load_tool("picoboot-wifi")
NODE = shutil.which("node")
BASE = 0x81300000


@unittest.skipUnless(NODE, "node not found")
class DolJsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def js(self, dol):
        """(payload, None) or (None, message) from dol.js; self.loader says if a stub was put in front."""
        src, out = os.path.join(self.tmp.name, "in.dol"), os.path.join(self.tmp.name, "out.bin")
        with open(src, "wb") as f:
            f.write(dol)
        run = subprocess.run([NODE, os.path.join(REPO, "tests", "js", "dol_convert.js"), src, out],
                             capture_output=True, text=True)
        if run.returncode:
            return None, run.stderr
        self.assertIn(run.stdout, ("loader", "plain"))
        self.loader = run.stdout == "loader"
        with open(out, "rb") as f:
            return f.read(), None

    @staticmethod
    def reference(dol):
        """(payload, loader, None) or (None, None, message), as the contract has it from the Python tools."""
        try:
            if pi.flatten_dol(bytearray(dol))[0] & 0x017FFFFF == 0x01300000:
                return pi.dol_to_payload(dol), False, None
            return pi.dol_to_payload(ds.wrap(dol)), True, None
        except ValueError as e:
            return None, None, str(e)

    def same(self, dol):
        """dol.js and the Python tools agree: the same payload and loader flag, or the same error message."""
        want, loader, error = self.reference(dol)
        self.assertEqual(self.js(dol), (want, error))
        if error is None:
            self.assertEqual(self.loader, loader)
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

    def test_the_embedded_stub_is_the_checked_in_one(self):
        with open(os.path.join(REPO, "src", "web", "dol.js")) as f:
            source = f.read()
        literal = re.search(r"var STUB_HEX =((?:\s*\"[0-9a-f]+\"\s*\+?)+);", source).group(1)
        self.assertEqual(bytes.fromhex("".join(re.findall(r'"([0-9a-f]+)"', literal))), ds.stub())

    def test_ordinary_dols_get_the_loader_stub(self):
        rnd = random.Random(3)
        low = 0x80003100
        for sections, entry, bss in (
                ([(low, rnd.randbytes(4096)), (low + 0x10000, rnd.randbytes(1000))], low, (low + 0x20000, 0x2000)),
                ([(low + 0x800, rnd.randbytes(64)), (low, rnd.randbytes(512)), (low + 0x4000, rnd.randbytes(8))],
                 low, (0, 0)),                                        # out of order, gaps, no BSS
                ([(low, b"\2" * 4)], low, (0, 0)),
                ([(low, rnd.randbytes(100)), (0x80400000, rnd.randbytes(4000))], low + 8, (0x80500000, 4)),
                ([(low, b"\3" * 8)], low + 4, (0, 0))):
            with self.subTest(sections=len(sections), entry=entry):
                self.assertIsNotNone(self.same(make_dol(sections, entry=entry, bss=bss)))
                self.assertTrue(self.loader)
        # A text section may be any of 0 to 6; a data section isn't one
        self.same(make_dol([(low, b"\1" * 16)] + [(0, b"")] * 3 + [(low + 0x100, b"\2" * 16)], entry=low + 0x100))
        self.assertTrue(self.loader)

    def test_ipl_builds_get_no_stub(self):
        self.same(make_dol([(BASE, b"\1" * 64)]))
        self.assertFalse(self.loader)
        with open(os.path.join(REPO, IPL_DOL), "rb") as f:
            self.same(f.read())
        self.assertFalse(self.loader)

    def test_the_loader_refusals(self):
        text = bytes(range(256)) * 16
        low = 0x80003100
        cases = (make_dol([(0x80003000, text)], entry=0x80003000),
                 make_dol([(0x812FF800, text)], entry=0x812FF800),
                 make_dol([(low, text)], entry=low, bss=(0x81200000, 0x200000)),
                 make_dol([(low, text)], entry=low, bss=(0x80002000, 0x100)),
                 make_dol([(low + 2, text)], entry=low + 2),
                 make_dol([(low, text + b"x")], entry=low),
                 make_dol([(low, text)], entry=0x80100000),
                 make_dol([(low, text)] + [(0, b"")] * 6 + [(0x80010000, b"\1" * 16)], entry=0x80010000),
                 make_dol([(low, text)], entry=low)[:-1],
                 make_dol([(0xC0003100, text)], entry=0xC0003100),
                 make_dol([(0x00003100, text)], entry=0x00003100))
        for dol in cases:
            with self.subTest(len(dol)):
                self.assertIsNone(self.same(dol))
        self.assertIn("reaches low memory", self.js(cases[0])[1])
        self.assertIn("reaches the payload image", self.js(cases[1])[1])
        self.assertIn("outside main memory", self.js(cases[-2])[1])
        self.assertIn("outside main memory", self.js(cases[-1])[1])

    def test_a_dol_at_the_ipl_address_with_a_wrong_entry_keeps_the_old_message(self):
        dol = make_dol([(BASE, b"\1" * 16)], entry=0x80003100)
        self.assertTrue(self.js(dol)[1].startswith("Invalid entry point or base address"))
        self.same(dol)

    def test_the_cli_agrees(self):
        dol = make_dol([(0x80003100, bytes(range(256)) * 4), (0x80010000, b"\5" * 100)], entry=0x80003100,
                       bss=(0x80020000, 0x100))
        self.assertEqual(cli.convert_dol(dol, "x"), (self.same(dol), True))

    def runs(self, name):
        path = os.path.join(os.environ["PICOBOOT_PAYLOAD_DIR"], name)
        if not os.path.exists(path):
            self.skipTest(f"no {name} in PICOBOOT_PAYLOAD_DIR")
        with open(path, "rb") as f:
            dol = f.read()
        payload = self.same(dol)
        self.assertTrue(self.loader)
        self.assertEqual(cli.convert_dol(dol, name), (payload, True))
        found, (bss, bss_size), entry = ds.sections(dol)
        image = bytes(pi.scramble(bytearray(0x720) + payload[32:-4])[0x720:])
        cpu, _, jump = run_stub(dol, img=image)
        self.assertEqual(jump, entry & 0x3FFFFFFF)
        for _, address, offset, size in found:
            pa = address & 0x3FFFFFFF
            self.assertEqual(bytes(cpu.mem[pa:pa + size]), dol[offset:offset + size])
        self.assertEqual(bytes(cpu.mem[bss & 0x3FFFFFFF:(bss & 0x3FFFFFFF) + bss_size]), bytes(bss_size))
        self.assertEqual(cpu.cache_problems(), [])

    @unittest.skipUnless(os.environ.get("PICOBOOT_PAYLOAD_DIR"), "PICOBOOT_PAYLOAD_DIR not set")
    def test_cubiboot(self):
        self.runs("cubiboot_ipl.dol")

    @unittest.skipUnless(os.environ.get("PICOBOOT_PAYLOAD_DIR"), "PICOBOOT_PAYLOAD_DIR not set")
    def test_swiss(self):
        self.runs("swiss_r2119.dol")

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
