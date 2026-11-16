"""
Tests for the D43 loader stub (src/dolstub/dolstub.S, its bytes in dolstub.bin) and
tools/dolstub.py, which puts an ordinary .dol behind it. tests/ppcsim.py runs the real stub bytes.

With PICOBOOT_BUILD_DIR, the stub the build assembled must be the checked-in dolstub.bin. With
PICOBOOT_PAYLOAD_DIR holding cubiboot_ipl.dol (cubiboot v1.13.0's ipl.dol) or swiss_r2119.dol
(Swiss r2119's DOL/swiss_r2119.dol), those run too.
"""

import os
import struct
import tempfile
import unittest

from helpers import REPO, load_tool, make_dol, run_tool
from ppcsim import SPR_DABR, SPR_EAR, SPR_HID0, SPR_IABR, Cpu, Halt

ds = load_tool("dolstub")
pi = load_tool("process_ipl")

STUB = ds.stub()
IMAGE = 0x01300000                      # Where the image is, physically
GARBAGE = 0xA5


class Hardware:
    """The hardware registers the stub reads (physical addresses), and how often."""

    def __init__(self, mi=2, dicfg=0xFF, revision=2, di_running=False, dsp_busy=2, ai=0x0000C123):
        self.values = {
            0x0C004028: (mi << 16) | 0x5A5A,        # The memory controller's 0x4028, then 0x402A
            0x0C006024: dicfg,                      # DICFG
            0x0C00302C: (revision << 28) | 0x0123456,
            0x0C00601C: 1 if di_running else 0,     # DICR: TSTART
            0x0C006000: 0x0000002A,                 # DISR
            0x0C005034: ai,                         # 0x5034, then the audio DMA's control
        }
        self.dsp_busy = dsp_busy
        self.reads = []

    def __call__(self, pa):
        self.reads.append(pa)
        if pa == 0x0C005008:                        # The word with DSPCR: DMAs busy for a while
            if self.dsp_busy:
                self.dsp_busy -= 1
                return 0x00000600
            return 0x00000004
        if pa not in self.values:
            raise Halt(f"unexpected hardware read at 0x{pa:08X}")
        return self.values[pa]


MAX_STEPS = 5_000_000                   # A small .dol takes about 20,000; a stuck loop stops here


def run(dol, hardware=None, stub_bytes=None, timebase=0x0000123456789ABC, img=None):
    """Runs the stub on garbage memory; returns the CPU, the hardware and where it jumped."""
    stub_bytes = STUB if stub_bytes is None else stub_bytes
    hardware = hardware or Hardware()
    img = ds.image(dol, stub_bytes) if img is None else img
    cpu = Cpu(img, hardware=hardware, timebase=timebase)
    cpu.mem[:IMAGE] = bytes([GARBAGE]) * IMAGE
    cpu.mem[IMAGE + len(img):] = bytes([GARBAGE]) * (len(cpu.mem) - IMAGE - len(img))
    jump = cpu.run(inside=(IMAGE, IMAGE + len(stub_bytes)), max_steps=MAX_STEPS if len(img) < 0x10000 else 50_000_000)
    return cpu, hardware, jump


def word(cpu, pa):
    return struct.unpack_from(">I", cpu.mem, pa)[0]


TEXT = bytes(range(256)) * 16           # 4 KB
DATA = b"\x11\x22\x33\x44" * 1000       # 4000 bytes
SIMPLE = make_dol([(0x80003100, TEXT), (0x80010000, DATA)], entry=0x80003100, bss=(0x80020000, 0x2000))


class StubBytesTest(unittest.TestCase):
    def test_whole_blocks(self):
        self.assertEqual(len(STUB) % 32, 0)
        self.assertLessEqual(len(STUB), 1024)

    @unittest.skipUnless(os.environ.get("PICOBOOT_BUILD_DIR"), "PICOBOOT_BUILD_DIR not set")
    def test_the_build_assembles_the_same_bytes(self):
        path = os.path.join(os.environ["PICOBOOT_BUILD_DIR"], "src", "dolstub", "dolstub.bin")
        self.assertTrue(os.path.exists(path), "the build has no stub: PowerPC binutils missing?")
        with open(path, "rb") as f:
            self.assertEqual(f.read(), STUB)


class RunTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cpu, cls.hw, cls.jump = run(SIMPLE)

    def test_jumps_to_the_entry_in_real_mode(self):
        self.assertEqual(self.jump, 0x00003100)
        self.assertEqual(self.cpu.msr, 0)

    def test_the_program_is_in_place(self):
        mem = self.cpu.mem
        self.assertEqual(bytes(mem[0x3100:0x3100 + len(TEXT)]), TEXT)
        self.assertEqual(bytes(mem[0x10000:0x10000 + len(DATA)]), DATA)
        self.assertEqual(bytes(mem[0x20000:0x22000]), bytes(0x2000))
        self.assertEqual(mem[0x22000], GARBAGE)     # Not past the BSS

    def test_low_memory(self):
        cpu = self.cpu
        expected = bytearray(0x3100)
        for vector in range(0x100, 0x1800, 0x100):
            expected[vector:vector + 4] = struct.pack(">I", 0x48000000)      # b . (itself)
        struct.pack_into(">I", expected, 0x28, 0x01800000)                  # Memory controller value 2: 24 MB
        struct.pack_into(">I", expected, 0x2C, 0x00000001 + 2)              # Retail HW1, revision 2
        struct.pack_into(">II", expected, 0xF8, 162000000, 486000000)
        expected[0x30D8:0x30E0] = cpu.mem[0x30D8:0x30E0]                    # The boot time, below
        self.assertEqual(bytes(cpu.mem[:0x3100]), bytes(expected))

    def test_boot_time_is_minus_the_time_base(self):
        boot = struct.unpack_from(">Q", self.cpu.mem, 0x30D8)[0]
        (_, _, tbu), (_, _, tbl) = self.cpu.tb_reads[:2]
        self.assertEqual((-boot) & 0xFFFFFFFFFFFFFFFF, (tbu << 32) | tbl)

    def test_stores_only_where_it_should(self):
        allowed = set(range(0, 0x3100 >> 5))
        for start, size in ((0x3100, len(TEXT)), (0x10000, len(DATA)), (0x20000, 0x2000)):
            allowed |= set(range(start >> 5, (start + size + 31) >> 5))
        self.assertEqual(sorted(line << 5 for line in set(self.cpu.stored) - allowed), [])

    def test_caches_synced_before_the_jump(self):
        self.assertEqual(self.cpu.cache_problems(), [])

    def test_hardware_quiesced(self):
        self.assertEqual(self.cpu.hw_writes, [(0x0C005008, 0x804), (0x0C005034, 0x0000C123 & ~0x8000)])
        self.assertEqual(self.hw.reads[0], 0x0C00601C)                      # DICR first: EAR set before
        self.assertEqual(self.hw.reads.count(0x0C005008), 3)                # Busy twice, then done
        self.assertEqual(self.cpu.spr[SPR_EAR], 0x8000000C)
        self.assertEqual((self.cpu.spr[SPR_DABR], self.cpu.spr[SPR_IABR]), (0, 0))

    def test_hid0_keeps_its_other_bits(self):
        # Only ICFI is set (and clears itself): the caches stay as BS1 set them
        self.assertEqual(self.cpu.spr[SPR_HID0], Cpu(b"").spr[SPR_HID0])


class HardwareVariantsTest(unittest.TestCase):
    def test_memory_size_from_the_memory_controller(self):
        sizes = [0x01000000, 0x02000000, 0x01800000, 0x03000000, 0x02000000, 0x04000000, 0x01800000, 0x03000000]
        for mi, size in enumerate(sizes):
            with self.subTest(mi=mi):
                cpu, _, _ = run(SIMPLE, Hardware(mi=mi))
                self.assertEqual(word(cpu, 0x28), size)

    def test_console_type(self):
        for dicfg, revision, expected in ((0xFF, 0, 0x00000001), (0xFF, 3, 0x00000004),
                                           (0x00, 0, 0x10000004), (0x01, 5, 0x10000009)):
            with self.subTest(dicfg=dicfg, revision=revision):
                cpu, _, _ = run(SIMPLE, Hardware(dicfg=dicfg, revision=revision))
                self.assertEqual(word(cpu, 0x2C), expected)

    def test_a_running_di_transfer_is_stopped(self):
        cpu, _, _ = run(SIMPLE, Hardware(di_running=True))
        self.assertEqual(cpu.hw_writes[0], (0x0C006000, 0x2A | 1))

    def test_no_bss(self):
        cpu, _, jump = run(make_dol([(0x80003100, TEXT)], entry=0x80003100))
        self.assertEqual(jump, 0x3100)
        self.assertEqual(bytes(cpu.mem[0x3100:0x3100 + len(TEXT)]), TEXT)
        self.assertEqual(cpu.cache_problems(), [])

    def test_sections_off_cache_lines(self):
        # 4-byte aligned, starting near a line's end and running into the next one
        dol = make_dol([(0x80003100, TEXT), (0x8001001C, b"ABCDEFGH"), (0x8001103C, b"ijklmnop")],
                       entry=0x80003100, bss=(0x8002001C, 8))
        cpu, _, _ = run(dol)
        self.assertEqual(bytes(cpu.mem[0x1001C:0x10024]), b"ABCDEFGH")
        self.assertEqual(bytes(cpu.mem[0x1103C:0x11044]), b"ijklmnop")
        self.assertEqual(bytes(cpu.mem[0x2001C:0x20024]), bytes(8))
        self.assertEqual(cpu.cache_problems(), [])

    def test_an_empty_section_record_is_skipped(self):
        # The converters leave empty sections out; the stub copes with one anyway
        img = bytearray(ds.image(SIMPLE, STUB))
        table = len(STUB)
        struct.pack_into(">I", img, table + 16 + 8, 0)          # The first section's size
        cpu, _, jump = run(SIMPLE, img=bytes(img))
        self.assertEqual(jump, 0x3100)
        self.assertEqual(cpu.mem[0x3100], GARBAGE)
        self.assertEqual(bytes(cpu.mem[0x10000:0x10000 + len(DATA)]), DATA)

    def test_time_base_read_again_across_a_carry(self):
        first = Cpu(ds.image(SIMPLE), hardware=Hardware(), timebase=0)
        first.run(inside=(IMAGE, IMAGE + len(STUB)), max_steps=MAX_STEPS)
        step = first.tb_reads[0][0]
        # The upper word changes between the first two reads, so the stub reads all three again
        cpu, _, _ = run(SIMPLE, timebase=(2 << 32) - step - 1)
        self.assertEqual(len(cpu.tb_reads), 6)
        boot = struct.unpack_from(">Q", cpu.mem, 0x30D8)[0]
        (_, _, tbu), (_, _, tbl) = cpu.tb_reads[3:5]
        self.assertEqual(tbu, 2)
        self.assertEqual((-boot) & 0xFFFFFFFFFFFFFFFF, (tbu << 32) | tbl)


class CacheCheckTest(unittest.TestCase):
    """The cache check isn't vacuous: stubs with a step taken out fail it."""

    def patched(self, old, new):
        stub = bytearray(STUB)
        at = [i for i in range(0, len(stub), 4) if stub[i:i + 4] == struct.pack(">I", old)]
        self.assertEqual(len(at), 1)
        stub[at[0]:at[0] + 4] = struct.pack(">I", new)
        return bytes(stub)

    def test_without_the_instruction_cache_invalidate(self):
        cpu, _, _ = run(SIMPLE, stub_bytes=self.patched(0x60000800, 0x60000000))   # ori r0,r0,0x800
        self.assertTrue(cpu.cache_problems())

    def test_without_the_write_back(self):
        cpu, _, _ = run(SIMPLE, stub_bytes=self.patched(0x7C00406C, 0x60000000))   # dcbst 0,r8
        self.assertTrue(cpu.cache_problems())


class LayoutTest(unittest.TestCase):
    def test_the_table(self):
        img = ds.image(SIMPLE)
        entry, bss, bss_size, count = struct.unpack_from(">IIII", img, len(STUB))
        self.assertEqual((entry, bss, bss_size, count), (0x80003100, 0x80020000, 0x2000, 2))
        records = [struct.unpack_from(">III", img, len(STUB) + 16 + 12 * i) for i in range(2)]
        self.assertEqual([(a, s) for a, _, s in records], [(0x80003100, len(TEXT)), (0x80010000, len(DATA))])
        table = len(STUB)
        self.assertEqual(img[table + records[0][1]:table + records[0][1] + len(TEXT)], TEXT)
        self.assertEqual(img[table + records[1][1]:table + records[1][1] + len(DATA)], DATA)

    def test_wrap_is_an_ipl_dol(self):
        wrapped = ds.wrap(SIMPLE)
        load, entry, img = pi.flatten_dol(bytearray(wrapped))
        self.assertEqual((load, entry, bytes(img)), (0x81300000, 0x81300000, ds.image(SIMPLE)))
        pi.dol_to_payload(wrapped)                  # The IPL converter takes it

    def test_refusals(self):
        cases = [
            ("low memory", make_dol([(0x80003000, TEXT)], entry=0x80003000), "reaches low memory"),
            ("the image", make_dol([(0x81300000, TEXT)], entry=0x81300000), "reaches the payload image"),
            ("just below the image", make_dol([(0x812FF800, TEXT)], entry=0x812FF800), "reaches the payload image"),
            ("BSS in the image", make_dol([(0x80003100, TEXT)], entry=0x80003100, bss=(0x81200000, 0x200000)),
             "The BSS .* reaches the payload image"),
            ("BSS in low memory", make_dol([(0x80003100, TEXT)], entry=0x80003100, bss=(0x80002000, 0x100)),
             "The BSS .* reaches low memory"),
            ("unaligned", make_dol([(0x80003102, TEXT)], entry=0x80003102), "whole words"),
            ("odd size", make_dol([(0x80003100, TEXT + b"x")], entry=0x80003100), "whole words"),
            ("entry outside", make_dol([(0x80003100, TEXT)], entry=0x80100000), "isn't in a text section"),
            ("entry in data", make_dol([(0x80003100, TEXT)] + [(0, b"")] * 6 + [(0x80010000, DATA)], entry=0x80010000),
             "isn't in a text section"),
            ("no sections", make_dol([]), "no sections"),
            ("short", b"\0" * 100, "smaller than a DOL header"),
            ("truncated", SIMPLE[:-1], "past the end of the file"),
        ]
        for name, dol, message in cases:
            with self.subTest(name), self.assertRaisesRegex(ValueError, message):
                ds.image(dol)


class CommandLineTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.input = os.path.join(self.tmp.name, "program.dol")
        self.output = os.path.join(self.tmp.name, "program-ipl.dol")

    def test_writes_the_wrapped_dol(self):
        with open(self.input, "wb") as f:
            f.write(SIMPLE)
        result = run_tool("dolstub", self.input, self.output)
        self.assertEqual(result.returncode, 0, result.stderr)
        with open(self.output, "rb") as f:
            self.assertEqual(f.read(), ds.wrap(SIMPLE))

    def test_refuses_without_writing(self):
        with open(self.input, "wb") as f:
            f.write(make_dol([(0x81300000, TEXT)], entry=0x81300000))
        result = run_tool("dolstub", self.input, self.output)
        self.assertEqual(result.returncode, 1)
        self.assertIn("payload image", result.stderr)
        self.assertNotIn("Traceback", result.stderr)
        self.assertFalse(os.path.exists(self.output))


@unittest.skipUnless(os.environ.get("PICOBOOT_PAYLOAD_DIR"), "PICOBOOT_PAYLOAD_DIR not set")
class RealDolTest(unittest.TestCase):
    def check(self, name):
        path = os.path.join(os.environ["PICOBOOT_PAYLOAD_DIR"], name)
        if not os.path.exists(path):
            self.skipTest(f"no {name} in PICOBOOT_PAYLOAD_DIR")
        with open(path, "rb") as f:
            dol = f.read()
        found, (bss, bss_size), entry = ds.sections(dol)
        cpu, _, jump = run(dol)
        self.assertEqual(jump, entry & 0x3FFFFFFF)
        for _, address, offset, size in found:
            pa = address & 0x3FFFFFFF
            self.assertEqual(bytes(cpu.mem[pa:pa + size]), dol[offset:offset + size])
        self.assertEqual(bytes(cpu.mem[bss & 0x3FFFFFFF:(bss & 0x3FFFFFFF) + bss_size]), bytes(bss_size))
        self.assertEqual(cpu.cache_problems(), [])

    def test_cubiboot(self):
        self.check("cubiboot_ipl.dol")

    def test_swiss(self):
        self.check("swiss_r2119.dol")


if __name__ == "__main__":
    unittest.main()
