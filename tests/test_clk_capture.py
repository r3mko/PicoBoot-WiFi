"""
Tests for design D47's CLK capture (src/firmware/pio/clk_capture.pio, plan step L2): it runs in pio1
next to the counters and D44's recorder, set up as src/firmware/clk_capture.c does, against a
simulated console. Its header is generated in every firmware build:
    PICOBOOT_BUILD_DIR=build python3 -m unittest discover -s tests -p 'test_clk_capture.py' -v
"""

import os
import unittest
from collections import deque

import pio_sim
from test_pio_programs import (BITS_PER_TRANSFER, DATA_PIN, MOV_X_OSR, MOV_Y_OSR, SKIPPED_TRANSFERS,
                               WORDS_PER_TRANSFER, delivery_problems, prepare_transfer, random_words)
from test_timing import add_recorder, load

BUILD_DIR = os.environ.get("PICOBOOT_BUILD_DIR")
CAPTURE_WORDS = 16384           # clk_capture.c: 64 KB, 262,144 samples
FIRST_SAMPLE_DELAY = 1          # The first sample reads the lines a cycle after the first rising CLK edge


def add_capture(pio, cs):
    """Like clk_capture_init(): IN_BASE = CS, shift left, autopush at 32, RX FIFO joined.
    Mirrors src/firmware/clk_capture.c by hand; keep both in sync."""
    capture, _ = load("clk_capture.pio.h")
    return pio.add_sm(capture["clk_capture"], in_base=cs, autopush=True, push_threshold=32,
                      in_shift_right=False, rx_depth=8)


def samples_of(words):
    """(CS, CLK) of each sample: 16 a word, the first in bits 31-30, CS in bit 0 (clk_summary.h)."""
    return [((w >> (2 * k)) & 1, (w >> (2 * k + 1)) & 1) for w in words for k in range(15, -1, -1)]


def capture(data_clocks, *, half_period=4, clk_high_until=None, limit=CAPTURE_WORDS):
    """The capture alone against a console; the words as the DMA would copy them, up to `limit`.
    `clk_high_until`: CLK reads high from the start until then."""
    _, defines = load("injection.pio.h")
    cs, clk = defines["CS_PIN"], defines["CLK_PIN"]
    console = pio_sim.Console(cs, clk, data_clocks, half_period=half_period)
    if clk_high_until is not None:
        console._levels[clk][0] = 1
        console._times[clk].insert(1, clk_high_until)
        console._levels[clk].insert(1, 0)
    pio = pio_sim.PIO(console.level)
    sm = add_capture(pio, cs)
    sm.enabled = True
    words = []
    for t in range(console.end + 64):
        pio.step(t)
        while sm.rx and len(words) < limit:         # The DMA, until its buffer is full
            words.append(sm.rx.popleft())
    return console, words, pio, sm


def first_clock(console):
    """When CLK first rises in the first transfer."""
    clk = console.clk_pin
    return next(t for t, level in zip(console._times[clk], console._levels[clk]) if level and t > 0)


@unittest.skipUnless(BUILD_DIR, "set PICOBOOT_BUILD_DIR to a firmware build directory")
class CaptureTest(unittest.TestCase):
    def assertFollowsTheLines(self, console, words):
        start = first_clock(console) + FIRST_SAMPLE_DELAY
        for k, (cs, clk) in enumerate(samples_of(words)):
            t = start + k
            expected = (console.level(console.cs_pin, t), console.level(console.clk_pin, t))
            if (cs, clk) != expected:
                self.fail(f"sample {k}: CS {cs}, CLK {clk}; the lines {expected} at cycle {t}")

    def test_the_samples_follow_the_lines_from_the_first_rising_clk_edge(self):
        console, words, pio, _ = capture([64] * 3 + [256])
        self.assertEqual(samples_of(words)[0], (0, 1), "CS low and CLK high: just after the first rising edge")
        self.assertFollowsTheLines(console, words)
        self.assertEqual(sum(pio.pin_dir), 0, "passive: it drives no pin")

    def test_clk_high_from_the_start_waits_for_a_rising_edge(self):
        """D36's known limit has CLK high before the first transfer: the capture starts at the first clock."""
        console, words, _, _ = capture([64] * 2 + [256], clk_high_until=6)
        self.assertFollowsTheLines(console, words)

    def test_a_boot_fills_the_buffer_with_bs1_and_the_first_blocks_start(self):
        """With a 37.4 ns clock's nearest half period, 5 cycles: all of BS1, then the first block's
        start, then the full RX FIFO stalls the capture, as the DMA has stopped."""
        console, words, _, sm = capture([64] * SKIPPED_TRANSFERS + [BITS_PER_TRANSFER] * 2, half_period=5)
        self.assertEqual(len(words), CAPTURE_WORDS)
        self.assertEqual(len(sm.rx), 8, "the joined RX FIFO is full: the capture is stalled")
        samples = samples_of(words)
        falls = sum(1 for a, b in zip(samples, samples[1:]) if a[0] and not b[0])
        self.assertEqual(falls + 1, SKIPPED_TRANSFERS + 1, "BS1's 224 transfers and the first block's start")
        rising = sum(1 for a, b in zip(samples, samples[1:]) if not a[1] and b[1])
        self.assertGreater(rising - (SKIPPED_TRANSFERS * 96 - 1), 96, "past the first block's 97th clock")
        self.assertFollowsTheLines(console, words)

    def test_next_to_the_injection_the_counters_and_the_recorder(self):
        """pio1's four state machines and their instructions fit, and all of them follow the read while
        pio0 injects. The simulator has no DMA, bus or shared instruction memory, so this shows the
        programs agree, not that the capture can't slow the injection down."""
        injection, defines = load("injection.pio.h")
        counters, _ = load("counters.pio.h")
        timing, _ = load("timing.pio.h")
        capture_program, _ = load("clk_capture.pio.h")
        pio1_programs = [counters["cs_raw"], counters["cs_clocked"], timing["cs_timing"], capture_program["clk_capture"]]
        self.assertLessEqual(sum(len(p["instructions"]) for p in pio1_programs), 32)

        cs, clk = defines["CS_PIN"], defines["CLK_PIN"]
        words = random_words(3, seed=47)
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
        sampler = add_capture(cnt, cs)
        for sm in (raw, clocked):
            sm.x = 0xFFFFFFFF
        for sm in (raw, clocked, recorder, sampler, on_transfer, clocked_output):
            sm.enabled = True

        feed, received, changes, recorded, captured, s = deque(words), {}, [], [], [], 0
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
            while sampler.rx and len(captured) < CAPTURE_WORDS:
                captured.append(sampler.rx.popleft())
            if inj.pin_dir[DATA_PIN] != direction:
                direction = inj.pin_dir[DATA_PIN]
                changes.append((t, direction))
        result = {"received": received, "changes": changes, "console": console, "transfers": transfers,
                  "pc": clocked_output.pc, "disable_chip": injection["clocked_output"]["labels"]["disable_chip"]}
        self.assertEqual(delivery_problems(words, result), [])
        self.assertEqual(len(recorded) // 2, len(console.cs_falls))
        self.assertEqual(0xFFFFFFFF - raw.x, len(console.cs_falls))
        self.assertEqual(0xFFFFFFFF - clocked.x, len(console.cs_falls))
        self.assertEqual(len(captured), CAPTURE_WORDS)
        self.assertFollowsTheLines(console, captured)
        self.assertEqual(sum(cnt.pin_dir), 0, "pio1 drives no pin")


if __name__ == "__main__":
    unittest.main()
