"""
Tests for design D44's CS timing recorder (src/firmware/pio/timing.pio): it runs in pio1 next to the
counters, set up as src/firmware/timing.c does, against a simulated console. The recorder's header
is generated in every firmware build:
    PICOBOOT_BUILD_DIR=build python3 -m unittest discover -s tests -p 'test_timing.py' -v
"""

import os
import unittest
from collections import deque

import pio_sim
from test_pio_programs import (BITS_PER_TRANSFER, DATA_PIN, MOV_X_OSR, MOV_Y_OSR, SKIPPED_TRANSFERS,
                               WORDS_PER_TRANSFER, add_pulse, delivery_problems, prepare_transfer, random_words)

BUILD_DIR = os.environ.get("PICOBOOT_BUILD_DIR")
FIRMWARE_BUILD = os.path.join(BUILD_DIR, "src", "firmware") if BUILD_DIR else ""
# The loops count in steps of 2 cycles and start a few cycles after each edge: a period between two
# edges reads up to DEAD_TIME cycles short (one of 2 cycles reads 0). The first period starts with the
# recorder, not at an edge, and reads up to 2 cycles long (the input synchroniser).
DEAD_TIME = 6


def load(header):
    with open(os.path.join(FIRMWARE_BUILD, header)) as f:
        return pio_sim.parse_pio_header(f.read())


def high_cycles(word):
    """Word 1 of a pair: the CS-high time, counted down from all ones in steps of 2 cycles."""
    return 2 * (0x7FFFFFFF - (word & 0x7FFFFFFF))


def low_cycles(word):
    return 2 * (0xFFFFFFFF - word)


def add_recorder(pio, cs, clk):
    """Like timing_init(): JMP_PIN = CS, IN_BASE = CLK, shift left, autopush at 32, RX FIFO joined.
    Mirrors src/firmware/timing.c by hand; keep both in sync."""
    timing, _ = load("timing.pio.h")
    return pio.add_sm(timing["cs_timing"], jmp_pin=cs, in_base=clk, autopush=True, push_threshold=32,
                      in_shift_right=False, rx_depth=8)


def record(data_clocks, pulses=(), cs_low_until=None):
    """The recorder alone against a console; the words as the DMA would copy them, in pairs."""
    _, defines = load("injection.pio.h")
    cs, clk = defines["CS_PIN"], defines["CLK_PIN"]
    console = pio_sim.Console(cs, clk, data_clocks, cs_low_until=cs_low_until)
    for pin, start, width, level in pulses:
        add_pulse(console, defines[pin], start, width=width, level=level)
    pio = pio_sim.PIO(console.level)
    sm = add_recorder(pio, cs, clk)
    sm.enabled = True
    words = []
    for t in range(console.end + 64):
        pio.step(t)
        while sm.rx:
            words.append(sm.rx.popleft())
    return console, list(zip(words[0::2], words[1::2])), pio


@unittest.skipUnless(BUILD_DIR, "set PICOBOOT_BUILD_DIR to a firmware build directory")
class RecorderTest(unittest.TestCase):
    def assertNear(self, measured, actual, what):
        self.assertTrue(-DEAD_TIME <= measured - actual <= 2, f"{what}: {measured} cycles, actually {actual}")

    def test_every_transfer_gets_its_cs_high_and_cs_low_time(self):
        console, pairs, pio = record([64, 64, 64, 256])
        falls, rises = console.cs_falls, console.cs_rises
        self.assertEqual(len(pairs), len(falls))
        self.assertNear(high_cycles(pairs[0][0]), falls[0], "from the start to the first fall")
        for k, (w1, w2) in enumerate(pairs):
            if k:
                self.assertNear(high_cycles(w1), falls[k] - rises[k - 1], f"gap before transfer {k + 1}")
            self.assertNear(low_cycles(w2), rises[k] - falls[k], f"transfer {k + 1}")
            self.assertEqual(w1 >> 31, 0, "CLK idles low as CS falls")
        self.assertEqual(sum(pio.pin_dir), 0, "passive: it drives no pin")

    def test_a_low_start_is_a_pair_of_its_own(self):
        """CS undriven and low at first (the dev console since 1.1.1): no CS-high time, a short low one."""
        console, pairs, _ = record([64, 64], cs_low_until=4)
        self.assertEqual(len(pairs), len(console.cs_falls) + 1)
        self.assertEqual(high_cycles(pairs[0][0]), 0)
        self.assertNear(low_cycles(pairs[0][1]), 4, "the undriven start")

    def test_a_clockless_glitch_is_a_short_cs_low_period(self):
        """Phase 0's false edge: an extra pair, CS low for 2 cycles, before the first transfer."""
        console, pairs, _ = record([64, 64], pulses=[("CS_PIN", 8, 2, 0)])
        self.assertEqual(len(pairs), len(console.cs_falls) + 1)
        self.assertNear(low_cycles(pairs[0][1]), 2, "the glitch")
        self.assertNear(high_cycles(pairs[1][0]), console.cs_falls[0] - 10, "glitch to the first transfer")

    def test_a_cs_high_blip_inside_a_transfer_splits_it(self):
        """Noise during a transfer (the second console at its TV): a short CS-high gap between two parts,
        here while CLK is high, which the CLK bit shows."""
        console, pairs, _ = record([64, 64], pulses=[("CS_PIN", 111, 2, 1)])   # CLK is high at 112-116
        self.assertEqual(len(pairs), len(console.cs_falls) + 1)
        blip = pairs[1]
        self.assertNear(high_cycles(blip[0]), 2, "the blip")
        self.assertEqual(blip[0] >> 31, 1, "CLK high as CS fell again")
        self.assertNear(low_cycles(pairs[0][1]), 111 - console.cs_falls[0], "the first part")
        self.assertNear(low_cycles(blip[1]), console.cs_rises[0] - 113, "the second part")

    def test_clk_high_at_a_glitch(self):
        """D36's known limit: a false CS edge while CLK reads high. The CLK bit records it."""
        _, pairs, _ = record([64], pulses=[("CLK_PIN", 8, 4, 1), ("CS_PIN", 9, 2, 0)])
        self.assertEqual(pairs[0][0] >> 31, 1)
        self.assertEqual([w1 >> 31 for w1, _ in pairs[1:]], [0])

    def test_next_to_the_injection_and_the_counters(self):
        """In pio1 with the counters, while pio0 injects: every transfer is recorded and counted, and the
        injection delivers its payload. The simulator has no DMA, bus or shared instruction memory, so
        this shows the programs agree, not that the recorder can't slow the injection down."""
        injection, defines = load("injection.pio.h")
        counters, _ = load("counters.pio.h")
        cs, clk = defines["CS_PIN"], defines["CLK_PIN"]
        words = random_words(3, seed=41)
        transfers = len(words) // WORDS_PER_TRANSFER
        console = pio_sim.Console(cs, clk, [64] * SKIPPED_TRANSFERS + [BITS_PER_TRANSFER] * transfers + [64])

        inj = pio_sim.PIO(console.level)
        on_transfer = inj.add_sm(injection["on_transfer"], jmp_pin=cs)
        clocked_output = inj.add_sm(injection["clocked_output"], autopull=True, threshold=32, shift_right=False,
                                    out_base=DATA_PIN, out_count=1, set_base=DATA_PIN, set_count=1, fifo_depth=8)
        prepare_transfer(on_transfer, SKIPPED_TRANSFERS, MOV_X_OSR)
        prepare_transfer(clocked_output, BITS_PER_TRANSFER - 1, MOV_Y_OSR)
        cnt = pio_sim.PIO(console.level)
        raw = cnt.add_sm(counters["cs_raw"], jmp_pin=cs)
        clocked = cnt.add_sm(counters["cs_clocked"], jmp_pin=cs)
        recorder = add_recorder(cnt, cs, clk)
        for sm in (raw, clocked):
            sm.x = 0xFFFFFFFF
        raw.enabled = clocked.enabled = recorder.enabled = True
        on_transfer.enabled = clocked_output.enabled = True

        feed, received, changes, recorded, s = deque(words), {}, [], [], 0
        direction = inj.pin_dir[DATA_PIN]
        for t in range(console.end + 64):
            if feed and len(clocked_output.fifo) < clocked_output.fifo_depth:
                clocked_output.fifo.append(feed.popleft())
            while s < len(console.samples) and console.samples[s][0] == t:
                received.setdefault(console.samples[s][1], []).append(inj.pin(DATA_PIN))
                s += 1
            inj.step(t)
            cnt.step(t)
            while recorder.rx:
                recorded.append(recorder.rx.popleft())
            if inj.pin_dir[DATA_PIN] != direction:
                direction = inj.pin_dir[DATA_PIN]
                changes.append((t, direction))
        result = {"received": received, "changes": changes, "console": console, "transfers": transfers,
                  "pc": clocked_output.pc, "disable_chip": injection["clocked_output"]["labels"]["disable_chip"]}
        self.assertEqual(delivery_problems(words, result), [])
        self.assertEqual(len(recorded) // 2, len(console.cs_falls))
        self.assertEqual(0xFFFFFFFF - raw.x, len(console.cs_falls))
        self.assertEqual(0xFFFFFFFF - clocked.x, len(console.cs_falls))
        self.assertEqual(sum(cnt.pin_dir), 0, "pio1 drives no pin")


if __name__ == "__main__":
    unittest.main()
