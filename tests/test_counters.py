"""
Tests for the passive CS counters (src/firmware/pio/counters.pio, design D31, D36): they run in
pio1 next to the injection in pio0, against a simulated console, and are set up and read back with
the same exec'd instructions as src/firmware/counters.c. Needs a firmware build:

    PICOBOOT_BUILD_DIR=build python3 -m unittest discover -s tests -p 'test_counters.py' -v
"""

import os
import unittest
from collections import deque

import pio_sim
from test_pio_programs import (BITS_PER_TRANSFER, DATA_PIN, MOV_X_OSR, MOV_Y_OSR, SKIPPED_TRANSFERS,
                               WORDS_PER_TRANSFER, add_pulse, delivery_problems, prepare_transfer, random_words)

BUILD_DIR = os.environ.get("PICOBOOT_BUILD_DIR")
FIRMWARE_BUILD = os.path.join(BUILD_DIR, "src", "firmware") if BUILD_DIR else ""

# Mirrors src/firmware/counters.c by hand; keep in sync with the C code.
MOV_X_NOT_NULL = 0xA02B         # pio_encode_mov_not(pio_x, pio_null)
MOV_ISR_X = 0xA0C1              # pio_encode_mov(pio_isr, pio_x)
PUSH_NOBLOCK = 0x8000           # pio_encode_push(false, false)
FLAG_RAW, FLAG_CLOCKED = 2, 3


def load(header):
    with open(os.path.join(FIRMWARE_BUILD, header)) as f:
        return pio_sim.parse_pio_header(f.read())


def read_count(sm):
    """counters.c read_count(): exec'd `mov isr, x` and `push`, then 0xFFFFFFFF minus the value."""
    sm.exec_instruction(MOV_ISR_X)
    sm.exec_instruction(PUSH_NOBLOCK)
    return 0xFFFFFFFF - sm.rx.popleft()


def simulate(words, pulses=()):
    injection, defines = load("injection.pio.h")
    counters, _ = load("counters.pio.h")
    cs, clk = defines["CS_PIN"], defines["CLK_PIN"]
    transfers = len(words) // WORDS_PER_TRANSFER
    console = pio_sim.Console(cs, clk, [64] * SKIPPED_TRANSFERS + [BITS_PER_TRANSFER] * transfers + [64] * 2)
    for pin, start, level in pulses:
        add_pulse(console, defines[pin], start, level=level)

    inj = pio_sim.PIO(console.level)                    # pio0: the injection
    on_transfer = inj.add_sm(injection["on_transfer"], jmp_pin=cs)
    clocked_output = inj.add_sm(injection["clocked_output"], autopull=True, threshold=32, shift_right=False,
                                out_base=DATA_PIN, out_count=1, set_base=DATA_PIN, set_count=1, fifo_depth=8)
    prepare_transfer(on_transfer, SKIPPED_TRANSFERS, MOV_X_OSR)
    prepare_transfer(clocked_output, BITS_PER_TRANSFER - 1, MOV_Y_OSR)

    cnt = pio_sim.PIO(console.level)                    # pio1: the counters
    raw = cnt.add_sm(counters["cs_raw"], jmp_pin=cs)
    clocked = cnt.add_sm(counters["cs_clocked"], jmp_pin=cs)
    for sm in (raw, clocked):
        sm.exec_instruction(MOV_X_NOT_NULL)

    raw.enabled = clocked.enabled = True                # counters_start(), then injection_go()
    on_transfer.enabled = clocked_output.enabled = True

    feed = deque(words)
    received, changes, first = {}, [], {}
    direction = inj.pin_dir[DATA_PIN]
    s = 0
    for t in range(console.end + 64):
        if feed and len(clocked_output.fifo) < clocked_output.fifo_depth:
            clocked_output.fifo.append(feed.popleft())
        while s < len(console.samples) and console.samples[s][0] == t:
            received.setdefault(console.samples[s][1], []).append(inj.pin(DATA_PIN))
            s += 1
        inj.step(t)
        cnt.step(t)
        for flag in (FLAG_RAW, FLAG_CLOCKED):
            if flag not in first and cnt.irq & (1 << flag):
                first[flag] = t
        if inj.pin_dir[DATA_PIN] != direction:
            direction = inj.pin_dir[DATA_PIN]
            changes.append((t, direction))
    return {
        "received": received, "changes": changes, "console": console, "transfers": transfers,
        "pc": clocked_output.pc, "disable_chip": injection["clocked_output"]["labels"]["disable_chip"],
        "raw": read_count(raw), "clocked": read_count(clocked), "first": first,
    }


@unittest.skipUnless(BUILD_DIR, "set PICOBOOT_BUILD_DIR to a firmware build directory")
class CountersTest(unittest.TestCase):
    def run_case(self, pulses=()):
        words = random_words(3, seed=31)
        r = simulate(words, pulses)
        self.assertEqual(delivery_problems(words, r), [], "the injection still delivers the payload")
        return r

    def test_without_glitches_both_count_every_transfer(self):
        r = self.run_case()
        falls = r["console"].cs_falls
        self.assertEqual((r["raw"], r["clocked"]), (len(falls), len(falls)))
        self.assertTrue(falls[0] < r["first"][FLAG_RAW] <= falls[0] + 4, "raw flag at the first CS edge")
        self.assertGreater(r["first"][FLAG_CLOCKED], r["first"][FLAG_RAW], "clocked flag once it clocks")

    def test_a_glitch_counts_only_in_the_raw_count(self):
        r = self.run_case([("CS_PIN", 4, 0)])
        falls = r["console"].cs_falls
        self.assertEqual((r["raw"], r["clocked"]), (len(falls) + 1, len(falls)))
        self.assertLess(r["first"][FLAG_RAW], falls[0], "the raw flag marks the glitch")
        self.assertGreater(r["first"][FLAG_CLOCKED], falls[0], "the clocked flag marks the first real transfer")

    def test_a_glitch_then_clk_while_cs_is_high_counts_only_in_the_raw_count(self):
        r = self.run_case([("CS_PIN", 4, 0), ("CLK_PIN", 9, 1)])
        falls = r["console"].cs_falls
        self.assertEqual((r["raw"], r["clocked"]), (len(falls) + 1, len(falls)))

    def test_clk_while_cs_is_high_counts_nothing(self):
        r = self.run_case([("CLK_PIN", 8, 1)])
        falls = r["console"].cs_falls
        self.assertEqual((r["raw"], r["clocked"]), (len(falls), len(falls)))


if __name__ == "__main__":
    unittest.main()
