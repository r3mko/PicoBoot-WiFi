"""
Runs the real PIO programs from a firmware build in the simulator (tests/pio_sim.py),
against a simulated GameCube reading the IPL. Needs the build's generated injection.pio.h
and ipl.h (in build/src/firmware), so it is skipped unless PICOBOOT_BUILD_DIR is set:

    PICOBOOT_BUILD_DIR=build python3 -m unittest discover -s tests -p 'test_pio_programs.py' -v
"""

import os
import random
import unittest

import pio_sim
from helpers import header_array_words

BUILD_DIR = os.environ.get("PICOBOOT_BUILD_DIR")
FIRMWARE_BUILD = os.path.join(BUILD_DIR, "src", "firmware") if BUILD_DIR else ""
IPL_H = os.path.join(FIRMWARE_BUILD, "ipl.h")

# Mirrors src/firmware/injection.c and src/firmware/injection_pio.c by hand; keep in sync with the C code.
DATA_PIN = 6                    # INJECTION_PIN_DATA
SKIPPED_TRANSFERS = 224         # pio_prepare_transfer(pio, transfer_start_sm, 224, pio_x)
BITS_PER_TRANSFER = 8191 + 1    # pio_prepare_transfer(pio, clocked_output_sm, 8191, pio_y)
WORDS_PER_TRANSFER = BITS_PER_TRANSFER // 32

# The instructions pio_prepare_transfer() executes with pio_sm_exec()
PULL_IFEMPTY_BLOCK = 0x80E0     # pio_encode_pull(true, true)
MOV_X_OSR, MOV_Y_OSR = 0xA027, 0xA047
OUT_NULL_32 = 0x6060

SET_PINDIRS_0 = 0xE080          # set pindirs, 0
MOV_Y_Y = 0xA042                # mov y, y (does nothing)


def load_programs():
    with open(os.path.join(FIRMWARE_BUILD, "injection.pio.h")) as f:
        return pio_sim.parse_pio_header(f.read())


def prepare_transfer(sm, count, mov):
    """Same steps as pio_prepare_transfer() in src/pio.c."""
    sm.fifo.append(count)                           # pio_sm_put()
    for word in (PULL_IFEMPTY_BLOCK, mov, OUT_NULL_32):
        sm.exec_instruction(word)


def add_pulse(console, pin, start, width=2, level=0):
    """
    Drive `pin` of the simulated console to `level` for `width` cycles at `start`; the pin must be
    at the other level then. A CS pulse to 0 without clocks is a glitch (phase 0, design D36).
    """
    times, levels = console._times[pin], console._levels[pin]
    k = next(i for i, t in enumerate(times) if t > start)
    assert levels[k - 1] != level and times[k] > start + width
    times[k:k] = [start, start + width]
    levels[k:k] = [level, 1 - level]


def simulate(words, *, lazy_autopull=False, patch=None, extra_transfers=2, pulses=(), on_transfer_program=None):
    """
    Set up both state machines like on_transfer_program_init(), clocked_output_program_init()
    and injection_start() do, then let a simulated console read the IPL: 224 short transfers,
    one 1K transfer per 256 payload words, and a few short transfers afterwards (fonts, RTC).
    `pulses` adds (pin, start, level) pulses to the console; a start of ("after", n) means a few
    cycles after transfer n's CS rise.
    """
    programs, defines = load_programs()
    transfers = len(words) // WORDS_PER_TRANSFER
    console = pio_sim.Console(defines["CS_PIN"], defines["CLK_PIN"],
                              [64] * SKIPPED_TRANSFERS + [BITS_PER_TRANSFER] * transfers + [64] * extra_transfers)
    for pin, start, level in pulses:
        if isinstance(start, tuple):
            start = console.cs_rises[start[1]] + 3
        add_pulse(console, defines[pin], start, level=level)
    pio = pio_sim.PIO(console.level, lazy_autopull=lazy_autopull)
    clocked_program = dict(programs["clocked_output"])
    if patch:
        clocked_program["instructions"] = patch(list(clocked_program["instructions"]))
    # on_transfer: SDK default config (shift right, no autopull), JMP PIN on CS; clocked_output:
    # shift left, autopull at 32 bits, joined 8-word TX FIFO, OUT and SET on the data pin
    on_transfer = pio.add_sm(on_transfer_program or programs["on_transfer"], jmp_pin=defines["CS_PIN"])
    clocked_output = pio.add_sm(clocked_program, autopull=True, threshold=32, shift_right=False,
                                out_base=DATA_PIN, out_count=1, set_base=DATA_PIN, set_count=1, fifo_depth=8)
    prepare_transfer(on_transfer, SKIPPED_TRANSFERS, MOV_X_OSR)
    prepare_transfer(clocked_output, BITS_PER_TRANSFER - 1, MOV_Y_OSR)
    on_transfer.enabled = clocked_output.enabled = True
    received, changes = pio_sim.run(pio, console, clocked_output, words, DATA_PIN)
    return {"received": received, "changes": changes, "console": console, "transfers": transfers,
            "pc": clocked_output.pc, "disable_chip": programs["clocked_output"]["labels"]["disable_chip"]}


def bits_of(words):
    return [(w >> (31 - i)) & 1 for w in words for i in range(32)]


def first_difference(got, expected):
    for i, (a, b) in enumerate(zip(got, expected)):
        if a != b:
            return f"bit {i}: got {a}, expected {b}"
    return f"length {len(got)}, expected {len(expected)}" if len(got) != len(expected) else None


def delivery_problems(words, result):
    """Everything wrong with how the payload reached the console (empty list if correct)."""
    problems = []
    received, console = result["received"], result["console"]
    first, last = SKIPPED_TRANSFERS, SKIPPED_TRANSFERS + result["transfers"] - 1
    for n in range(first):
        if any(bit is not None for bit in received[n]):
            problems.append(f"data line driven during skipped transfer {n}")
            break
    for k in range(result["transfers"]):
        expected = bits_of(words[k * WORDS_PER_TRANSFER:(k + 1) * WORDS_PER_TRANSFER])
        difference = first_difference(received[first + k], expected)
        if difference:
            problems.append(f"transfer {k}: {difference}")
    for n in range(last + 1, len(console.cs_falls)):
        if any(bit is not None for bit in received[n]):
            problems.append(f"data line driven during later transfer {n}")
    if len(result["changes"]) != 2:
        problems.append(f"data line direction changed {len(result['changes'])} times, expected on once and off once")
    else:
        (t_on, on), (t_off, off) = result["changes"]
        if not (on == 1 and console.cs_falls[first] < t_on < console.cs_rises[first]):
            problems.append("data line not driven from the first payload transfer")
        if not (off == 0 and console.cs_rises[last] < t_off < console.cs_falls[last + 1]):
            problems.append("data line not released right after CS goes high at the end of the payload")
    if result["pc"] != result["disable_chip"]:
        problems.append(f"clocked_output ended at {result['pc']}, not at disable_chip ({result['disable_chip']})")
    return problems


def random_words(transfers, seed):
    rng = random.Random(seed)
    return [rng.getrandbits(32) for _ in range(transfers * WORDS_PER_TRANSFER)]


@unittest.skipUnless(BUILD_DIR, "set PICOBOOT_BUILD_DIR to a firmware build directory")
class PioProgramsTest(unittest.TestCase):
    def test_single_transfer(self):
        words = random_words(1, seed=1)
        self.assertEqual(delivery_problems(words, simulate(words)), [])

    def test_three_transfers(self):
        words = random_words(3, seed=2)
        self.assertEqual(delivery_problems(words, simulate(words)), [])

    @unittest.skipUnless(os.path.exists(IPL_H), "ipl.h not generated (build the firmware first)")
    def test_full_builtin_ipl(self):
        """The case that booted on a GameCube: the bundled IPL, 60 transfers."""
        with open(IPL_H) as f:
            words = header_array_words(f.read())
        self.assertEqual(len(words), 60 * WORDS_PER_TRANSFER)
        self.assertEqual(delivery_problems(words, simulate(words)), [])


@unittest.skipUnless(BUILD_DIR, "set PICOBOOT_BUILD_DIR to a firmware build directory")
class NegativeTest(unittest.TestCase):
    """These must fail, to show the tests above can catch the mistakes they are meant for."""

    def test_wrong_autopull_model_is_detected(self):
        """rp2040js-style autopull (refill only at the next OUT) stops after one transfer."""
        words = random_words(3, seed=3)
        problems = delivery_problems(words, simulate(words, lazy_autopull=True))
        self.assertTrue(any(p.startswith("transfer 1:") for p in problems), problems)

    def test_releasing_the_line_before_cs_goes_high_is_detected(self):
        """Without `wait 1 gpio CS_PIN` before `set pindirs 0`, the last bit is lost."""
        def without_hold(instructions):
            i = instructions.index(SET_PINDIRS_0)
            self.assertEqual(instructions[i - 1] & 0xFF80, 0x2080, "wait 1 gpio before set pindirs 0")
            instructions[i - 1] = MOV_Y_Y
            return instructions

        words = random_words(2, seed=4)
        problems = delivery_problems(words, simulate(words, patch=without_hold))
        self.assertIn(f"transfer 1: bit {BITS_PER_TRANSFER - 1}: got None, expected {bits_of(words)[-1]}", problems)


# Earlier on_transfer programs, for the negative tests (hand-copied from pioasm output):
# PicoBoot-DX v0.10, which counts every CS falling edge
ON_TRANSFER_V0_10 = pio_sim.program([0x2084, 0x2004, 0x0040, 0xC000, 0x2084, 0x2004, 0xC000],
                                    wrap_target=4, wrap=6)
# A CLK wait after each skipped CS edge, but no check that CS is still low (first handoff draft)
ON_TRANSFER_CLK_WAIT_ONLY = pio_sim.program([0x2084, 0x2004, 0x0047, 0xC000, 0x2084, 0x2004, 0xC000,
                                             0x2085, 0x0000], wrap_target=4, wrap=6)

GLITCH_BEFORE_FIRST = [("CS_PIN", 4, 0)]                       # the first transfer starts at t=16
GLITCH_BETWEEN_SKIPPED = [("CS_PIN", ("after", 100), 0)]
CLK_WHILE_CS_HIGH = [("CLK_PIN", 8, 1)]
GLITCH_THEN_CLK_WHILE_CS_HIGH = [("CS_PIN", 4, 0), ("CLK_PIN", 9, 1)]


@unittest.skipUnless(BUILD_DIR, "set PICOBOOT_BUILD_DIR to a firmware build directory")
class GlitchTest(unittest.TestCase):
    """Design D36: a CS pulse without clocks before the IPL read must not shift the payload."""

    def check(self, pulses):
        words = random_words(3, seed=21)
        self.assertEqual(delivery_problems(words, simulate(words, pulses=pulses)), [])

    def test_glitch_before_the_first_transfer(self):
        """Phase 0: the false edge came as the console first drove CS, ~120 ms before the IPL read."""
        self.check(GLITCH_BEFORE_FIRST)

    def test_glitch_between_skipped_transfers(self):
        self.check(GLITCH_BETWEEN_SKIPPED)

    def test_clk_pulse_while_cs_is_high(self):
        """EXI channel 0's clock is shared with other devices."""
        self.check(CLK_WHILE_CS_HIGH)

    def test_glitch_then_clk_pulse_while_cs_is_high(self):
        """The case the phase 0 code review found."""
        self.check(GLITCH_THEN_CLK_WHILE_CS_HIGH)

    def test_without_glitches_the_output_is_the_same_as_picoboot_dx(self):
        words = random_words(3, seed=22)
        new, old = simulate(words), simulate(words, on_transfer_program=ON_TRANSFER_V0_10)
        self.assertEqual((new["received"], new["changes"]), (old["received"], old["changes"]))


@unittest.skipUnless(BUILD_DIR, "set PICOBOOT_BUILD_DIR to a firmware build directory")
class GlitchNegativeTest(unittest.TestCase):
    """These must fail, to show the glitch tests catch what they are meant for."""

    def test_picoboot_dx_shifts_the_payload_on_a_glitch(self):
        words = random_words(3, seed=23)
        problems = delivery_problems(words, simulate(words, pulses=GLITCH_BEFORE_FIRST,
                                                     on_transfer_program=ON_TRANSFER_V0_10))
        self.assertTrue(problems)

    def test_clk_wait_without_the_cs_check_counts_a_glitch_then_clk(self):
        words = random_words(3, seed=24)
        problems = delivery_problems(words, simulate(words, pulses=GLITCH_THEN_CLK_WHILE_CS_HIGH,
                                                     on_transfer_program=ON_TRANSFER_CLK_WAIT_ONLY))
        self.assertTrue(problems)


if __name__ == "__main__":
    unittest.main()
