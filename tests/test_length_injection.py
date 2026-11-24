"""
Length-based injection (design D47), plan step L1: the detector and the output program in
src/firmware/pio/length.pio, run in the PIO simulator (tests/pio_sim.py) against a simulated
console, next to the standard count of 224 (injection.pio) where the two differ. The firmware loads the
length programs when boot control chooses them (D48). Needs a build's generated headers, so it is skipped unless
PICOBOOT_BUILD_DIR is set:

    PICOBOOT_BUILD_DIR=build python3 -m unittest discover -s tests -p 'test_length_injection.py' -v

Time is in PIO cycles: 4 ns at the injection's 250 MHz. The console's default half period of 4
cycles is a 32 ns clock, faster than BS1's measured 37.04 ns (27.00 MHz, docs/research/length-injection-results.md);
MeasuredConsole has the measured timing.
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


def length_defines():
    """pio/length.pio's public defines: the pins, LENGTH_BS1_CLOCKS and LENGTH_SWITCH_BIT."""
    with open(os.path.join(FIRMWARE_BUILD, "length.pio.h")) as f:
        return pio_sim.parse_pio_header(f.read())[1]


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


def without_cs_wait(words, cs_pin):
    """clocked_output as releases before 1.2.0 had it: without the `wait 1 gpio CS` after its first
    instruction (D48), so later jump targets move back by one."""
    assert words[1] == 0x2080 | cs_pin, "clocked_output's second instruction is the wait for CS high"
    return [w - 1 if w >> 13 == 0 and (w & 0x1F) > 1 else w    # JMP: the address is the low 5 bits
            for i, w in enumerate(words) if i != 1]


def run_length(console, words, block_bits, counters=None, sync=(2, 2)):
    """Set up the two state machines as src/firmware/injection.c and injection_pio.c do for length-based
    injection (D47, D48), and let the console read. Keep both in sync. Returns the bits the console read,
    the data pin's direction changes and the output's final PC (injection_complete() wants disable_chip).
    With `counters` (counters.pio.h's programs), pio1 also runs D31's two CS counters, as the firmware
    does, and their counts come last. `sync`: pio0's and pio1's input synchroniser delays."""
    length, _, defines = programs()
    pio0, pio1 = pio_sim.PIO(console.level, sync_cycles=sync[0]), pio_sim.PIO(console.level, sync_cycles=sync[1])
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
        return received, changes, out.pc, [0xFFFFFFFF - sm.x for sm in sms]
    return received, changes, out.pc


def run_count(console, words, block_bits, fetches, output=None):
    """The standard method (D36), with its count set to the console's number of BS1 fetches. `output`:
    another clocked_output's instructions."""
    _, count, defines = programs()
    pio = pio_sim.PIO(console.level)
    on_transfer = pio.add_sm(count["on_transfer"], jmp_pin=defines["CS_PIN"])
    program = dict(count["clocked_output"], instructions=output) if output else count["clocked_output"]
    out = pio.add_sm(program, autopull=True, threshold=32, shift_right=False,
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
    received, changes, pc = run_length(c, words, block_bits)
    return problems(words, received, c, blocks, block_bits, fetches, first_driven=SWITCH_BIT), received, changes, c, pc


def disable_chip():
    return programs()[0]["clocked_output_length"]["labels"]["disable_chip"]


class MeasuredConsole:
    """
    A console with the EXI timing both consoles showed (docs/research/length-injection-results.md, from
    the L7 review's model): periods of 9 or 10 cycles (9.26 on average), high 4-6 and low 4-5; CS falls
    18-19 cycles before a BS1 fetch's first clock and 78-79 before a block's, and rises 16-17 after the
    last clock falls; a block's 32 command clocks, then a 165-cycle low (the 660 ns pause), and every
    64th data period stretched to 12-13 cycles. `pause_in_every_block`: the pause and the stretches in
    later blocks too (measured only in the first). Same interface as pio_sim.Console.
    """

    def __init__(self, cs_pin, clk_pin, transfers, rng, pause_in_every_block=False):
        cs, clk = [(-10**9, 1)], [(-10**9, 0)]
        self.cs_pin, self.clk_pin = cs_pin, clk_pin
        self.samples, self.cs_falls, self.cs_rises = [], [], []
        t, blocks_seen = 200, 0
        for n, (clocks, kind) in enumerate(transfers):
            block = kind == "block"
            measured = block and (blocks_seen == 0 or pause_in_every_block)
            blocks_seen += block
            cs.append((t, 0))
            self.cs_falls.append(t)
            t += rng.choice([78, 79]) if block else rng.choice([18, 19])
            for c in range(32 + clocks):
                if measured and c == 32:
                    t += 165 - 4                        # the 660 ns pause instead of an ordinary low
                elif measured and c > 32 and (c - 32) % 64 == 0:
                    t += rng.choice([7, 8]) - 4         # a stretched period: a longer low before it
                period = rng.choices([9, 10], weights=[74, 26])[0]
                high = rng.choice([h for h in (4, 5, 6) if 4 <= period - h <= 5])
                clk.append((t, 1))
                if c >= 32:
                    self.samples.append((t, n, c - 32))
                clk.append((t + high, 0))
                t += period
            t += rng.choice([16, 17])
            cs.append((t, 1))
            self.cs_rises.append(t)
            t += rng.choice([34, 36, 38])
        self.end = t
        self._times = {pin: [e[0] for e in ev] for pin, ev in ((cs_pin, cs), (clk_pin, clk))}
        self._levels = {pin: [e[1] for e in ev] for pin, ev in ((cs_pin, cs), (clk_pin, clk))}
        self._cursor = {pin: 0 for pin in self._times}

    def level(self, gpio, t):
        times = self._times.get(gpio)
        if times is None:
            return 0
        i = self._cursor[gpio]
        if times[i] > t:
            i = 0
        while i + 1 < len(times) and times[i + 1] <= t:
            i += 1
        self._cursor[gpio] = i
        return self._levels[gpio][i]


def count_case(*, blocks=3, fetches=SMALL["fetches"], block_bits=SMALL["block_bits"], prepare=None, output=None,
               **console):
    words = payload(blocks, block_bits)
    c = make_console(blocks, block_bits, fetches, **console)
    if prepare:
        prepare(c)
    received = run_count(c, words, block_bits, fetches, output)
    return problems(words, received, c, blocks, block_bits, fetches, first_driven=0)


@unittest.skipUnless(os.environ.get("PICOBOOT_BUILD_DIR"), "PICOBOOT_BUILD_DIR not set")
class LengthInjectionTest(unittest.TestCase):
    def test_the_programs_fit(self):
        """The detector fits in pio1 next to the counters, the output program in pio0 on its own."""
        length, _, _ = programs()
        self.assertLessEqual(len(length["length_detect"]["instructions"]) + COUNTER_INSTRUCTIONS, 32)
        self.assertLessEqual(len(length["clocked_output_length"]["instructions"]), 32)

    def test_the_output_program_is_clocked_output(self):
        """From `start` on: clocked_output, with its wait for CS high (D48), its jumps moved by the entry's length."""
        length, count, _ = programs()
        mine = length["clocked_output_length"]["instructions"]
        theirs = count["clocked_output"]["instructions"]
        entry = len(mine) - len(theirs)
        self.assertEqual(mine[entry:], [w + entry if w >> 13 == 0 else w for w in theirs])

    def test_a_clean_boot_full_size(self):
        """224 BS1 fetches and two 1 KB blocks: U10 sends the first block's bits 0-65, the Pico the rest."""
        found, received, changes, console, pc = length_case(blocks=2, fetches=BS1_FETCHES, block_bits=BLOCK_BITS)
        self.assertEqual(found, [])
        self.assertEqual(pc, disable_chip(), "injection_complete() needs the output at disable_chip")
        self.assertEqual([d for _, d in changes], [1, 0], "the data line is taken over once and released once")

    def test_the_firmwares_set_up_with_the_counters(self):
        """As the firmware runs it by length (L6, D48): the detector and D31's counters in pio1, the output in pio0, a real
        BS1 and two 1 KB blocks. The payload arrives, and the counters count every transfer."""
        with open(os.path.join(FIRMWARE_BUILD, "counters.pio.h")) as f:
            counters, _ = pio_sim.parse_pio_header(f.read())
        words = payload(2, BLOCK_BITS)
        c = make_console(2, BLOCK_BITS, BS1_FETCHES)
        received, _, pc, (raw, clocked) = run_length(c, words, BLOCK_BITS, counters=counters)
        self.assertEqual(problems(words, received, c, 2, BLOCK_BITS, BS1_FETCHES, first_driven=SWITCH_BIT), [])
        self.assertEqual(pc, disable_chip())
        self.assertEqual((raw, clocked), (len(c.cs_falls), len(c.cs_falls)))

    def test_the_constants_and_pins_come_from_the_program(self):
        """pio/length.pio's public defines, which injection.c uses and checks, are this test's."""
        d, (_, _, defines) = length_defines(), programs()
        self.assertEqual((d["LENGTH_BS1_CLOCKS"], d["LENGTH_SWITCH_BIT"]), (BS1_CLOCKS, SWITCH_BIT))
        self.assertEqual((d["LENGTH_CS_PIN"], d["LENGTH_CLK_PIN"]), (defines["CS_PIN"], defines["CLK_PIN"]))

    def test_the_measured_timing(self):
        """
        A console with the measured timing (MeasuredConsole), a real BS1, two 1 KB blocks and transfers after
        the payload as long as a fetch and as long as half a block; with the pause in every block or only the
        first, and with pio0's and pio1's synchronisers a cycle apart (L7). The payload arrives, nothing is
        driven after it, and the output ends at disable_chip. The standard method too, with its CS wait (D48).
        """
        _, _, defines = programs()
        transfers = [(64, "bs1")] * BS1_FETCHES + [(BLOCK_BITS, "block")] * 2 + [(64, "other"), (4096, "other")]
        for seed, every, sync in ((1, False, (2, 2)), (2, True, (2, 2)), (3, False, (2, 3)), (4, True, (3, 2))):
            with self.subTest(seed=seed, pause_in_every_block=every, sync=sync):
                console = MeasuredConsole(defines["CS_PIN"], defines["CLK_PIN"], transfers, random.Random(seed),
                                          pause_in_every_block=every)
                words = payload(2, BLOCK_BITS, seed=seed)
                received, _, pc = run_length(console, words, BLOCK_BITS, sync=sync)
                self.assertEqual(problems(words, received, console, 2, BLOCK_BITS, BS1_FETCHES,
                                          first_driven=SWITCH_BIT), [])
                self.assertEqual(pc, disable_chip())
                console = MeasuredConsole(defines["CS_PIN"], defines["CLK_PIN"], transfers, random.Random(seed),
                                          pause_in_every_block=every)
                received = run_count(console, words, BLOCK_BITS, BS1_FETCHES)
                self.assertEqual(problems(words, received, console, 2, BLOCK_BITS, BS1_FETCHES, first_driven=0), [])

    def test_a_clk_glitch_during_bs1(self):
        """
        L7's finding 1: one CLK pulse inside a BS1 fetch makes its 97th rising edge, so the length method starts
        the payload in BS1. The standard count takes one clock per transfer and ignores it. A risk for noisy wiring,
        weighed at L9; on clean wiring L4 saw no CLK phase under 16 ns.
        """
        pulse = lambda c: add_pulse(c, c.clk_pin, c.cs_falls[2] + 4, width=4, level=1)
        self.assertEqual(count_case(prepare=pulse), [])
        found, *_ = length_case(prepare=pulse)
        self.assertTrue(any(problem.startswith("data line driven during BS1 fetch") for problem in found), found)

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
        A CS blip in the middle of a block (L1's finding). clocked_output before 1.2.0 then counted the
        block's last clock as the next block's first command clock, so every later block was one bit late.
        With the wait for CS high, both methods' output programs stay in step (D48).
        """
        _, count, defines = programs()
        old = without_cs_wait(count["clocked_output"]["instructions"], defines["CS_PIN"])
        for phase in range(2, 8):
            with self.subTest(phase=phase):
                noise = blip(SMALL["fetches"] + 1, 150, phase)
                self.assertNotEqual(count_case(prepare=noise, output=old), [], "1.1.2's clocked_output should lose a bit")
                self.assertEqual(count_case(prepare=noise), [])
                found, *_ = length_case(prepare=noise)
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
        faster than BS1's measured 37.04 ns) and up, and both fail at 3 (24 ns): the limit is the output loop's.
        """
        for half_period in (4, 5, 6, 8):
            with self.subTest(half_period=half_period):
                found, *_ = length_case(blocks=2, half_period=half_period)
                self.assertEqual(found, [])
        self.assertNotEqual(length_case(blocks=2, half_period=3)[0], [])
        self.assertNotEqual(count_case(blocks=2, half_period=3), [])
        _, _, changes, console, _ = length_case(blocks=2, half_period=4)
        driven = changes[0][0]                                  # set pindirs 1: bit 66 on the line
        self.assertEqual(driven - clock_time(console, SMALL["fetches"], 32 + SWITCH_BIT - 1), 3)
        self.assertEqual(clock_time(console, SMALL["fetches"], 32 + SWITCH_BIT) - driven, 5)


if __name__ == "__main__":
    unittest.main()
