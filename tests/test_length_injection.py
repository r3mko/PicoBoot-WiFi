"""
Length-based injection (design D47), plan step L1: the detector and the output program in
src/firmware/pio/length.pio, run in the PIO simulator (tests/pio_sim.py) against a simulated
console, next to today's count of 224 (injection.pio) where the two differ. The firmware doesn't
load the length programs yet. Needs a build's generated headers, so it is skipped unless
PICOBOOT_BUILD_DIR is set:

    PICOBOOT_BUILD_DIR=build python3 -m unittest discover -s tests -p 'test_length_injection.py' -v

Time is in PIO cycles: 4 ns at the injection's 250 MHz. The console's default half period of 4
cycles is a 32 ns clock, faster than the real 37.4 ns (26.76 MHz, docs/research/cs-timing-results.md).
Most cases use a few BS1 fetches and 256-bit blocks to keep the simulation short: the length
method doesn't depend on either, and the count is set to the same number of fetches.
"""

import os
import random
import unittest

import pio_sim
from test_pio_programs import (FIRMWARE_BUILD, MOV_X_OSR, MOV_Y_OSR, add_pulse, bits_of, first_difference,
                               prepare_transfer)

DATA_PIN = 6                    # INJECTION_PIN_DATA
BS1_FETCHES = 224               # A real BS1: 224 fetches of 32 command and 64 data clocks
BS1_CLOCKS = 96                 # The detector's ISR: a transfer with a 97th clock is a block
SWITCH_BIT = 66                 # The first data bit of the first block the Pico drives
BLOCK_BITS = 8192               # A real block's data bits
MOV_ISR_OSR = 0xA0C7            # mov isr, osr: pio_prepare_transfer() into ISR
COUNTER_INSTRUCTIONS = 10       # pio1 already holds cs_raw (4) and cs_clocked (6), counters.pio
HIGH_PHASE, LOW_PHASE = 2, 5    # Cycles after a rising edge, with the default half period of 4

SMALL = {"fetches": 6, "block_bits": 256}


def programs():
    if not hasattr(programs, "cache"):
        with open(os.path.join(FIRMWARE_BUILD, "length.pio.h")) as f:
            length, _ = pio_sim.parse_pio_header(f.read())
        with open(os.path.join(FIRMWARE_BUILD, "injection.pio.h")) as f:
            count, defines = pio_sim.parse_pio_header(f.read())
        programs.cache = (length, count, defines)
    return programs.cache


def payload(blocks, block_bits, seed=47):
    rng = random.Random(seed)
    return [rng.getrandbits(32) for _ in range(blocks * block_bits // 32)]


def make_console(blocks, block_bits, fetches, *, half_period=4, cs_low_until=None, lead=0):
    _, _, defines = programs()
    return pio_sim.Console(defines["CS_PIN"], defines["CLK_PIN"],
                           [64] * fetches + [block_bits] * blocks + [64] * 2,
                           half_period=half_period, cs_low_until=cs_low_until, lead=lead)


def clock_time(console, transfer, clock):
    """When the console's CLK rises for clock `clock` (0-based, the command's included) of a transfer."""
    return console.cs_falls[transfer] + console.gap + 2 * console.half_period * clock


def merge(console, transfer):
    """Hold CS low from `transfer` through the next one: noise that bridges the gap between them."""
    cs = console.cs_pin
    for t in (console.cs_rises[transfer], console.cs_falls[transfer + 1]):
        i = console._times[cs].index(t)
        del console._times[cs][i], console._levels[cs][i]


def blip(transfer, clock, phase, width=4):
    """A CS-high blip `phase` cycles after a clock's rising edge, the console clocking on."""
    return lambda c: add_pulse(c, c.cs_pin, clock_time(c, transfer, clock) + phase, width=width, level=1)


def with_cs_wait(words, cs_pin):
    """clocked_output with `wait 1 gpio CS` after its first instruction; later jump targets move by one."""
    out = []
    for i, w in enumerate(words):
        out.append(w + 1 if w >> 13 == 0 and (w & 0x1F) >= 1 else w)    # JMP: the address is the low 5 bits
        if i == 0:
            out.append(0x2080 | cs_pin)
    return out


def run_length(console, words, block_bits, counters=None):
    """Set up the two state machines as src/firmware/injection.c and injection_pio.c do in a length build
    (D47, plan step L6), and let the console read. Keep both in sync. With `counters` (counters.pio.h's
    programs), pio1 also runs D31's two CS counters, as the firmware does; they are returned too."""
    length, _, defines = programs()
    pio0, pio1 = pio_sim.PIO(console.level), pio_sim.PIO(console.level)
    pio_sim.link(pio0, pio1)
    # clocked_output_length: as clocked_output (shift left, autopull at 32, joined TX FIFO, OUT and
    # SET on the data pin). length_detect: JMP PIN on CS, IN_BASE CLK with IN_COUNT 1
    out = pio0.add_sm(length["clocked_output_length"], autopull=True, threshold=32, shift_right=False,
                      out_base=DATA_PIN, out_count=1, set_base=DATA_PIN, set_count=1, fifo_depth=8)
    detect = pio1.add_sm(length["length_detect"], jmp_pin=defines["CS_PIN"], in_base=defines["CLK_PIN"], in_count=1)
    prepare_transfer(detect, BS1_CLOCKS, MOV_ISR_OSR)
    prepare_transfer(out, block_bits - 1, MOV_Y_OSR)
    prepare_transfer(out, block_bits - SWITCH_BIT - 1, MOV_ISR_OSR)
    out.enabled = detect.enabled = True
    if counters:
        # As counters.c: JMP PIN on CS, X counted down from all ones
        sms = [pio1.add_sm(counters[name], jmp_pin=defines["CS_PIN"]) for name in ("cs_raw", "cs_clocked")]
        for sm in sms:
            sm.x, sm.enabled = 0xFFFFFFFF, True
    received, changes = pio_sim.run([pio0, pio1], console, out, words, DATA_PIN)
    if counters:
        return received, changes, [0xFFFFFFFF - sm.x for sm in sms]
    return received, changes


def run_count(console, words, block_bits, fetches):
    """Today's method (D36), with its count set to the console's number of BS1 fetches."""
    _, count, defines = programs()
    pio = pio_sim.PIO(console.level)
    on_transfer = pio.add_sm(count["on_transfer"], jmp_pin=defines["CS_PIN"])
    out = pio.add_sm(count["clocked_output"], autopull=True, threshold=32, shift_right=False,
                     out_base=DATA_PIN, out_count=1, set_base=DATA_PIN, set_count=1, fifo_depth=8)
    prepare_transfer(on_transfer, fetches, MOV_X_OSR)
    prepare_transfer(out, block_bits - 1, MOV_Y_OSR)
    on_transfer.enabled = out.enabled = True
    received, _ = pio_sim.run(pio, console, out, words, DATA_PIN)
    return received


def problems(words, received, console, blocks, block_bits, fetches, *, first_driven):
    """Everything wrong with how the payload reached the console; first_driven: SWITCH_BIT or 0."""
    found = []
    for n in range(fetches):
        if any(bit is not None for bit in received[n]):
            found.append(f"data line driven during BS1 fetch {n}")
            break
    bits = bits_of(words)
    first = received[fetches]
    if any(bit is not None for bit in first[:first_driven]):
        found.append(f"first block: driven before bit {first_driven}")
    difference = first_difference(first[first_driven:], bits[first_driven:block_bits])
    if difference:
        found.append(f"first block: {difference}")
    for k in range(1, blocks):
        difference = first_difference(received[fetches + k], bits[k * block_bits:(k + 1) * block_bits])
        if difference:
            found.append(f"block {k}: {difference}")
    for n in range(fetches + blocks, len(console.cs_falls)):
        if any(bit is not None for bit in received[n]):
            found.append(f"data line driven during transfer {n}, after the payload")
            break
    return found


def length_case(*, blocks=3, fetches=SMALL["fetches"], block_bits=SMALL["block_bits"], prepare=None, **console):
    words = payload(blocks, block_bits)
    c = make_console(blocks, block_bits, fetches, **console)
    if prepare:
        prepare(c)
    received, changes = run_length(c, words, block_bits)
    return problems(words, received, c, blocks, block_bits, fetches, first_driven=SWITCH_BIT), received, changes, c


def count_case(*, blocks=3, fetches=SMALL["fetches"], block_bits=SMALL["block_bits"], prepare=None, **console):
    words = payload(blocks, block_bits)
    c = make_console(blocks, block_bits, fetches, **console)
    if prepare:
        prepare(c)
    received = run_count(c, words, block_bits, fetches)
    return problems(words, received, c, blocks, block_bits, fetches, first_driven=0)


@unittest.skipUnless(os.environ.get("PICOBOOT_BUILD_DIR"), "PICOBOOT_BUILD_DIR not set")
class LengthInjectionTest(unittest.TestCase):
    def test_the_programs_fit(self):
        """The detector fits in pio1 next to the counters, the output program in pio0 on its own."""
        length, _, _ = programs()
        self.assertLessEqual(len(length["length_detect"]["instructions"]) + COUNTER_INSTRUCTIONS, 32)
        self.assertLessEqual(len(length["clocked_output_length"]["instructions"]), 32)

    def test_the_output_program_is_clocked_output_with_the_cs_wait(self):
        """From `start` on: clocked_output plus the wait for CS high, its jumps moved by the entry's length."""
        length, count, defines = programs()
        mine = length["clocked_output_length"]["instructions"]
        theirs = with_cs_wait(count["clocked_output"]["instructions"], defines["CS_PIN"])
        entry = len(mine) - len(theirs)
        self.assertEqual(mine[entry:], [w + entry if w >> 13 == 0 else w for w in theirs])

    def test_a_clean_boot_full_size(self):
        """224 BS1 fetches and two 1 KB blocks: U10 sends the first block's bits 0-65, the Pico the rest."""
        found, received, changes, console = length_case(blocks=2, fetches=BS1_FETCHES, block_bits=BLOCK_BITS)
        self.assertEqual(found, [])
        self.assertEqual([d for _, d in changes], [1, 0], "the data line is taken over once and released once")

    def test_the_firmwares_set_up_with_the_counters(self):
        """As a length build runs it (L6): the detector and D31's counters in pio1, the output in pio0, a real
        BS1 and two 1 KB blocks. The payload arrives, and the counters count every transfer."""
        with open(os.path.join(FIRMWARE_BUILD, "counters.pio.h")) as f:
            counters, _ = pio_sim.parse_pio_header(f.read())
        words = payload(2, BLOCK_BITS)
        c = make_console(2, BLOCK_BITS, BS1_FETCHES)
        received, _, (raw, clocked) = run_length(c, words, BLOCK_BITS, counters=counters)
        self.assertEqual(problems(words, received, c, 2, BLOCK_BITS, BS1_FETCHES, first_driven=SWITCH_BIT), [])
        self.assertEqual((raw, clocked), (len(c.cs_falls), len(c.cs_falls)))

    def test_the_dev_consoles_start(self):
        """CS low at first, then the console's unclean first rise: a pulse without clocks."""
        found, *_ = length_case(cs_low_until=10, lead=64,
                                prepare=lambda c: add_pulse(c, c.cs_pin, 30, width=2, level=0))
        self.assertEqual(found, [])

    def test_a_false_edge_while_clk_is_high(self):
        """D36's known limit: a CS pulse while CLK reads high. The count takes it; the length method doesn't."""
        def pulse(c):
            add_pulse(c, c.clk_pin, 20, width=20, level=1)
            add_pulse(c, c.cs_pin, 26, width=6, level=0)
        self.assertNotEqual(count_case(cs_low_until=10, lead=64, prepare=pulse), [], "D36's limit no longer shows")
        found, *_ = length_case(cs_low_until=10, lead=64, prepare=pulse)
        self.assertEqual(found, [])

    def test_noise_that_splits_bs1_fetches(self):
        """CS blips during BS1 fetches, the clock running on (the second console at its TV)."""
        for phase in (HIGH_PHASE, LOW_PHASE):
            def blips(c, phase=phase):
                for n in (1, 3, 4):
                    blip(n, 40, phase)(c)
            with self.subTest(phase=phase):
                self.assertNotEqual(count_case(prepare=blips), [], "the count should send the payload early")
                found, *_ = length_case(prepare=blips)
                self.assertEqual(found, [])

    def test_noise_during_the_payload(self):
        """
        A CS blip in the middle of a block (L1's finding). Today's clocked_output then counts the block's
        last clock as the next block's first command clock, so every later block is one bit late. With
        the wait for CS high, the prototype's output program stays in step.
        """
        for phase in range(2, 8):
            with self.subTest(phase=phase):
                self.assertNotEqual(count_case(prepare=blip(SMALL["fetches"] + 1, 150, phase)), [],
                                    "today's clocked_output should lose a bit")
                found, *_ = length_case(prepare=blip(SMALL["fetches"] + 1, 150, phase))
                self.assertEqual(found, [])

    def test_a_blip_before_the_first_blocks_97th_clock(self):
        """
        The length method's own window, about 3.6 us: a CS blip the detector sees while CLK is low restarts
        its count in the middle of the first block, so the switch lands at the wrong bit. One in CLK's high
        phase it doesn't see, so it does no harm.
        """
        found, *_ = length_case(prepare=blip(SMALL["fetches"], 50, LOW_PHASE))
        self.assertNotEqual(found, [])
        found, *_ = length_case(prepare=blip(SMALL["fetches"], 50, HIGH_PHASE))
        self.assertEqual(found, [])

    def test_cs_held_low_across_a_gap(self):
        """Noise that bridges the gap between two BS1 fetches: 192 clocks look like a block. Not seen."""
        found, *_ = length_case(prepare=lambda c: merge(c, 2))
        self.assertTrue(any(problem.startswith("data line driven during BS1 fetch") for problem in found), found)
        self.assertNotEqual(count_case(prepare=lambda c: merge(c, 2)), [], "the count sends it one late")

    def test_the_switch_timing(self):
        """
        Bit 66 is ready on the pin before the takeover, so the Pico drives it 3 cycles after clock 97 rises,
        the timing of every other bit. Both methods work with a half period of 4 cycles (a 32 ns clock,
        faster than the real 37.4 ns) and up, and both fail at 3 (24 ns): the limit is the output loop's.
        """
        for half_period in (4, 5, 6, 8):
            with self.subTest(half_period=half_period):
                found, *_ = length_case(blocks=2, half_period=half_period)
                self.assertEqual(found, [])
        self.assertNotEqual(length_case(blocks=2, half_period=3)[0], [])
        self.assertNotEqual(count_case(blocks=2, half_period=3), [])
        _, _, changes, console = length_case(blocks=2, half_period=4)
        driven = changes[0][0]                                  # set pindirs 1: bit 66 on the line
        self.assertEqual(driven - clock_time(console, SMALL["fetches"], 32 + SWITCH_BIT - 1), 3)
        self.assertEqual(clock_time(console, SMALL["fetches"], 32 + SWITCH_BIT) - driven, 5)


if __name__ == "__main__":
    unittest.main()
