"""
Phase 0 measurement build (implementation plan step 0.1): checks that the passive CS counter
(src/measure.pio) doesn't change the injection, and that it counts what it should.

Runs the real PIO programs from a build (generated picoboot-dx.pio.h and measure.pio.h) in the
PicoBoot-DX simulator (tests/pio_sim.py) against a simulated console. Build first, then:

    DX_MEASURE_BUILD_DIR=hwtest/dx-measure/build python3 -m unittest discover -s hwtest/dx-measure/tests -v
"""

import hashlib
import os
import random
import re
import sys
import unittest
from collections import deque

HERE = os.path.dirname(os.path.abspath(__file__))
MEASURE_DIR = os.path.dirname(HERE)
REPO = os.path.dirname(os.path.dirname(MEASURE_DIR))
sys.path.insert(0, os.path.join(REPO, "tests"))
import pio_sim  # noqa: E402

BUILD_DIR = os.environ.get("DX_MEASURE_BUILD_DIR")

# SHA-256 of src/picoboot-dx.pio in PicoBoot-DX v0.10 (commit 0c8b223)
DX_V0_10_PIO_SHA256 = "28269b9fe00614f91c1ac78cc824e853137c8321bf1f3ab657394e2dbb7fccda"

# Mirrors src/picoboot-dx.c, src/pio.c and src/measure.c by hand; keep in sync with the C code.
DATA_PIN = 6
SKIPPED_TRANSFERS = 224
BITS_PER_TRANSFER = 8192
WORDS_PER_TRANSFER = BITS_PER_TRANSFER // 32
PULL_IFEMPTY_BLOCK = 0x80E0     # pio_encode_pull(true, true)
MOV_X_OSR, MOV_Y_OSR = 0xA027, 0xA047
OUT_NULL_32 = 0x6060


def load_programs():
    programs, defines = {}, {}
    for header in ("picoboot-dx.pio.h", "measure.pio.h"):
        with open(os.path.join(BUILD_DIR, header)) as f:
            p, d = pio_sim.parse_pio_header(f.read())
        programs.update(p)
        defines.update(d)
    return programs, defines


def prepare_transfer(sm, count, mov):
    """Same steps as pio_prepare_transfer() in src/pio.c."""
    sm.fifo.append(count)
    for word in (PULL_IFEMPTY_BLOCK, mov, OUT_NULL_32):
        sm.exec_instruction(word)


def simulate(words, *, with_counter=True, extra_transfers=3):
    """
    The injection state machines in one PIO block and the CS counter in another (pio1), both
    watching the same simulated console: 224 short transfers, one 1K transfer per 256 words,
    then a few short transfers (fonts, RTC).
    """
    programs, defines = load_programs()
    transfers = len(words) // WORDS_PER_TRANSFER
    console = pio_sim.Console(defines["CS_PIN"], defines["CLK_PIN"],
                              [64] * SKIPPED_TRANSFERS + [BITS_PER_TRANSFER] * transfers + [64] * extra_transfers)

    inj = pio_sim.PIO(console.level)
    on_transfer = inj.add_sm(programs["on_transfer"])
    clocked_output = inj.add_sm(programs["clocked_output"], autopull=True, threshold=32, shift_right=False,
                                out_base=DATA_PIN, out_count=1, set_base=DATA_PIN, set_count=1, fifo_depth=8)
    prepare_transfer(on_transfer, SKIPPED_TRANSFERS, MOV_X_OSR)
    prepare_transfer(clocked_output, BITS_PER_TRANSFER - 1, MOV_Y_OSR)

    cnt = pio_sim.PIO(console.level)
    counter = cnt.add_sm(programs["cs_counter"])
    counter.x = 0xFFFFFFFF                      # pio_encode_mov_not(pio_x, pio_null) in measure.c

    counter.enabled = with_counter              # cs_counter_start() comes first...
    on_transfer.enabled = clocked_output.enabled = True

    # pio_sim.run(), stepping both PIO blocks every cycle
    feed = deque(words)
    received, changes = {}, []
    direction = inj.pin_dir[DATA_PIN]
    first_irq = None
    s = 0
    for t in range(console.end + 64):
        if feed and len(clocked_output.fifo) < clocked_output.fifo_depth:
            clocked_output.fifo.append(feed.popleft())
        while s < len(console.samples) and console.samples[s][0] == t:
            received.setdefault(console.samples[s][1], []).append(inj.pin(DATA_PIN))
            s += 1
        inj.step(t)
        cnt.step(t)
        if first_irq is None and cnt.irq & 1:
            first_irq = t
        if inj.pin_dir[DATA_PIN] != direction:
            direction = inj.pin_dir[DATA_PIN]
            changes.append((t, direction))
    return {
        "received": received, "changes": changes, "console": console, "transfers": transfers,
        "pc": clocked_output.pc, "disable_chip": programs["clocked_output"]["labels"]["disable_chip"],
        "count": 0xFFFFFFFF - counter.x, "first_irq": first_irq, "sync": cnt.sync_cycles,
    }


def bits_of(words):
    return [(w >> (31 - i)) & 1 for w in words for i in range(32)]


def delivery_problems(words, r):
    """Everything wrong with how the payload reached the console (empty if correct)."""
    problems = []
    first, last = SKIPPED_TRANSFERS, SKIPPED_TRANSFERS + r["transfers"] - 1
    for n in list(range(first)) + list(range(last + 1, len(r["console"].cs_falls))):
        if any(bit is not None for bit in r["received"][n]):
            problems.append(f"data line driven during transfer {n}")
    for k in range(r["transfers"]):
        expected = bits_of(words[k * WORDS_PER_TRANSFER:(k + 1) * WORDS_PER_TRANSFER])
        if r["received"][first + k] != expected:
            problems.append(f"payload transfer {k} differs")
    if len(r["changes"]) != 2:
        problems.append(f"data line direction changed {len(r['changes'])} times")
    if r["pc"] != r["disable_chip"]:
        problems.append("clocked_output didn't end at disable_chip")
    return problems


def random_words(transfers, seed):
    rng = random.Random(seed)
    return [rng.getrandbits(32) for _ in range(transfers * WORDS_PER_TRANSFER)]


class InjectionSourceTest(unittest.TestCase):
    def test_injection_programs_are_picoboot_dx_v0_10(self):
        with open(os.path.join(MEASURE_DIR, "src", "picoboot-dx.pio"), "rb") as f:
            self.assertEqual(hashlib.sha256(f.read()).hexdigest(), DX_V0_10_PIO_SHA256)


@unittest.skipUnless(BUILD_DIR, "set DX_MEASURE_BUILD_DIR to a dx-measure build directory")
class CounterTest(unittest.TestCase):
    def check(self, words):
        with_counter = simulate(words)
        without = simulate(words, with_counter=False)
        # The injection is the same with and without the counter, and correct
        self.assertEqual(delivery_problems(words, with_counter), [])
        self.assertEqual(with_counter["received"], without["received"])
        self.assertEqual(with_counter["changes"], without["changes"])
        # The counter saw every CS falling edge...
        console = with_counter["console"]
        self.assertEqual(with_counter["count"], len(console.cs_falls))
        # ...and flagged the first one within a few cycles (input synchroniser + one instruction)
        first_fall = console.cs_falls[0]
        self.assertIsNotNone(with_counter["first_irq"])
        self.assertGreater(with_counter["first_irq"], first_fall)
        self.assertLessEqual(with_counter["first_irq"], first_fall + with_counter["sync"] + 3)

    def test_three_transfers(self):
        self.check(random_words(3, seed=1))

    def test_full_builtin_ipl(self):
        """The bundled gekkoboot, 60 transfers, from the build's ipl.h."""
        with open(os.path.join(BUILD_DIR, "ipl.h")) as f:
            words = [int(w, 16) for w in re.findall(r"0x([0-9A-F]{8})", f.read())]
        self.assertEqual(len(words), 60 * WORDS_PER_TRANSFER)
        self.check(words)


if __name__ == "__main__":
    unittest.main()
