"""
Tests for the PIO simulator itself (tests/pio_sim.py): each rule it implements, checked
against the RP2040 datasheet (page numbers in pio_sim.py). No firmware build needed.
"""

import unittest

import pio_sim
from pio_sim import PIO, UnsupportedInstruction, decode, program

# Hand-assembled instructions
OUT_NULL_1 = 0x6061         # out null, 1
OUT_PINS_1 = 0x6001         # out pins, 1
JMP_NOT_OSRE_0 = 0x00E0     # jmp !osre, 0
JMP_X_DEC_0 = 0x0040        # jmp x--, 0
JMP_NOT_X_0 = 0x0020        # jmp !x, 0
JMP_PIN_0 = 0x00C0          # jmp pin, 0
MOV_X_NOT_NULL = 0xA02B     # mov x, ~null
MOV_ISR_X = 0xA0C1          # mov isr, x
PUSH_NOBLOCK = 0x8000       # push noblock
PUSH_BLOCK = 0x8020         # push block
JMP_0 = 0x0000              # jmp 0
WAIT_1_GPIO_5 = 0x2085      # wait 1 gpio, 5
WAIT_1_IRQ_0 = 0x20C0       # wait 1 irq, 0
IRQ_SET_0 = 0xC000          # irq nowait 0
MOV_Y_Y = 0xA042            # mov y, y
PULL_IFEMPTY_BLOCK = 0x80E0  # pio_encode_pull(true, true)
JMP_Y_DEC_0 = 0x0080        # jmp y--, 0
IN_PINS_1 = 0x4001          # in pins, 1
IN_PINS_2 = 0x4002          # in pins, 2
IN_X_32 = 0x4020            # in x, 32
IN_Y_31 = 0x405F            # in y, 31
IN_X_4 = 0x4024             # in x, 4

WORD = 0x12345678


def machine(words, *, input_level=lambda gpio, t: 0, lazy_autopull=False, **config):
    pio = PIO(input_level, lazy_autopull=lazy_autopull)
    config.setdefault("autopull", True)
    config.setdefault("shift_right", False)
    sm = pio.add_sm(program(words), **config)
    sm.enabled = True
    return pio, sm


class AutopullTest(unittest.TestCase):
    """Datasheet p.339, "Autopull Details"."""

    def test_out_that_empties_the_osr_refills_it_in_the_same_cycle(self):
        pio, sm = machine([OUT_NULL_1, JMP_NOT_OSRE_0, JMP_0])
        sm.osr, sm.osr_count = 0x80000000, 31
        sm.fifo.append(WORD)
        pio.step(0)
        self.assertEqual((sm.osr, sm.osr_count), (WORD, 0))
        pio.step(1)
        self.assertEqual(sm.pc, 0, "jmp !osre sees a full OSR and is taken")

    def test_refill_waits_for_the_next_out_in_lazy_mode(self):
        pio, sm = machine([OUT_NULL_1, JMP_NOT_OSRE_0, JMP_0], lazy_autopull=True)
        sm.osr, sm.osr_count = 0x80000000, 31
        sm.fifo.append(WORD)
        pio.step(0)
        pio.step(1)
        self.assertEqual((sm.osr_count, sm.pc), (32, 2), "rp2040js-style: OSR still empty, jmp !osre not taken")

    def test_out_on_an_empty_osr_stalls_until_data_then_refills_then_shifts(self):
        pio, sm = machine([OUT_NULL_1, JMP_0])
        pio.step(0)
        self.assertEqual(sm.pc, 0, "stalls while the FIFO is empty")
        sm.fifo.append(WORD)
        pio.step(1)
        self.assertEqual((sm.pc, sm.osr, sm.osr_count), (0, WORD, 0), "refill cycle, still stalled")
        pio.step(2)
        self.assertEqual((sm.pc, sm.osr_count), (1, 1), "OUT executes on the next cycle")

    def test_refill_happens_in_the_background_on_non_out_cycles(self):
        pio, sm = machine([WAIT_1_GPIO_5])
        sm.fifo.append(WORD)
        pio.step(0)
        self.assertEqual((sm.pc, sm.osr, sm.osr_count), (0, WORD, 0), "refilled while stalled on WAIT")

    def test_out_shifts_left_msb_first_to_the_pin(self):
        pio, sm = machine([OUT_PINS_1, JMP_0], out_base=6)
        sm.osr, sm.osr_count = 0x80000001, 0
        pio.pin_dir[6] = 1
        pio.step(0)
        self.assertEqual((pio.pin(6), sm.osr), (1, 0x00000002))


class InstructionTest(unittest.TestCase):
    def test_jmp_x_dec_always_decrements(self):              # p.323
        pio, sm = machine([JMP_X_DEC_0, JMP_0], autopull=False)
        sm.x = 0
        pio.step(0)
        self.assertEqual((sm.pc, sm.x), (1, 0xFFFFFFFF), "not taken, but X still decremented")

    def test_jmp_not_x_is_taken_only_when_x_is_zero(self):  # p.322
        for x, pc in ((0, 0), (5, 1)):
            with self.subTest(x=x):
                pio, sm = machine([JMP_NOT_X_0, JMP_0], autopull=False)
                sm.pc, sm.x = 0, x
                sm.wrap_target, sm.wrap = 0, 1
                pio.step(0)
                self.assertEqual((sm.pc, sm.x), (pc, x), "X is not changed")

    def test_jmp_pin_follows_the_jmp_pin_two_cycles_late(self):    # p.322, p.342
        pio, sm = machine([JMP_PIN_0, JMP_0], autopull=False, jmp_pin=4,
                          input_level=lambda gpio, t: int(gpio == 4 and t >= 10))
        pio.step(11)
        self.assertEqual(sm.pc, 1, "the pin is still low through the synchroniser at t=11")
        sm.pc = 0
        pio.step(12)
        self.assertEqual(sm.pc, 0, "taken once the pin is high")

    def test_jmp_pin_without_a_jmp_pin_raises(self):
        """A setup that forgets jmp_pin must fail, not read the pin as 0 (PicoBoot-DX 0ed9e8b)."""
        pio, sm = machine([JMP_PIN_0, JMP_0], autopull=False)
        with self.assertRaises(ValueError):
            pio.step(0)

    def test_jmp_y_dec_always_decrements(self):              # p.323, as for X
        pio, sm = machine([JMP_Y_DEC_0, JMP_0], autopull=False)
        sm.y = 0
        pio.step(0)
        self.assertEqual((sm.pc, sm.y), (1, 0xFFFFFFFF), "not taken, but Y still decremented")
        sm.pc, sm.y = 0, 2
        pio.step(1)
        self.assertEqual((sm.pc, sm.y), (0, 1), "taken: Y was non-zero")

    def test_in_shifts_the_least_significant_bits_into_the_isr(self):   # "IN"
        # 0x5, then 0xA: shifted left they read 0x5A; shifted right they enter at the top, 0xA5000000
        for shift_right, isr in ((False, 0x0000005A), (True, 0xA5000000)):
            with self.subTest(shift_right=shift_right):
                pio, sm = machine([IN_X_4, IN_X_4, JMP_0], autopull=False, in_shift_right=shift_right)
                sm.wrap_target, sm.wrap = 0, 2
                sm.x = 0xFFFFFFF5
                pio.step(0)
                sm.x = 0x0000000A
                pio.step(1)
                self.assertEqual((sm.isr, sm.isr_count), (isr, 8))

    def test_in_32_replaces_the_isr(self):                   # "IN": a count of 0 encodes 32
        pio, sm = machine([IN_X_32, JMP_0], autopull=False)
        sm.isr, sm.x = 0x12345678, 0x9ABCDEF0
        pio.step(0)
        self.assertEqual((sm.isr, sm.isr_count), (0x9ABCDEF0, 32))

    def test_in_pins_reads_from_in_base_through_the_synchroniser(self):   # "IN", p.342
        level = lambda gpio, t: int((gpio == 5 and t >= 10) or gpio == 6)
        pio, sm = machine([IN_PINS_2, JMP_0], autopull=False, in_base=5, in_shift_right=False,
                          input_level=level)
        pio.step(11)
        self.assertEqual(sm.isr, 0b10, "GPIO 5's rise isn't visible yet at t=11; GPIO 6 is bit 1")
        sm.pc, sm.isr = 0, 0
        pio.step(12)
        self.assertEqual(sm.isr, 0b11)

    def test_autopush_pushes_at_the_threshold_and_clears_the_isr(self):   # "Autopush Details"
        pio, sm = machine([IN_PINS_1, IN_Y_31, JMP_0], autopull=False, autopush=True, push_threshold=32,
                          in_shift_right=False, in_base=5, input_level=lambda gpio, t: 1)
        sm.y = 0xFFFFFFF0
        pio.step(0)
        self.assertEqual((list(sm.rx), sm.isr_count), ([], 1), "1 bit in: below the threshold")
        pio.step(1)
        self.assertEqual((list(sm.rx), sm.isr, sm.isr_count), ([0xFFFFFFF0], 0, 0),
                         "the pin's 1 in bit 31, then Y's low 31 bits; pushed and cleared")

    def test_autopush_stalls_on_a_full_rx_fifo(self):        # "Autopush Details"
        pio, sm = machine([IN_X_32, JMP_0], autopull=False, autopush=True, rx_depth=8)
        sm.rx.extend(range(8))
        sm.x = 7
        pio.step(0)
        self.assertEqual((sm.pc, sm.isr_count), (0, 0), "stalled: nothing shifted")
        sm.rx.popleft()
        pio.step(1)
        self.assertEqual((sm.pc, sm.rx[-1]), (1, 7))

    def test_mov_inverted_null_sets_all_ones(self):          # p.328
        pio, sm = machine([JMP_0, JMP_0], autopull=False)
        sm.exec_instruction(MOV_X_NOT_NULL)
        self.assertEqual(sm.x, 0xFFFFFFFF)

    def test_mov_isr_and_push_read_out_x(self):             # p.326, p.328
        pio, sm = machine([JMP_0, JMP_0], autopull=False)
        sm.x = 0xFFFFFF00
        sm.exec_instruction(MOV_ISR_X)
        sm.exec_instruction(PUSH_NOBLOCK)
        self.assertEqual((list(sm.rx), sm.isr), ([0xFFFFFF00], 0))

    def test_push_block_stalls_on_a_full_rx_fifo(self):     # p.326
        pio, sm = machine([PUSH_BLOCK, JMP_0], autopull=False)
        sm.rx.extend([1, 2, 3, 4])
        pio.step(0)
        self.assertEqual(sm.pc, 0)
        sm.rx.popleft()
        pio.step(1)
        self.assertEqual(sm.pc, 1)

    def test_wait_irq_clears_the_flag(self):                 # p.324
        pio, sm = machine([WAIT_1_IRQ_0, JMP_0], autopull=False)
        pio.irq = 1
        pio.step(0)
        self.assertEqual((sm.pc, pio.irq), (1, 0))

    def test_irq_set_is_visible_from_the_next_cycle(self):   # p.330
        pio, sm = machine([IRQ_SET_0, JMP_0], autopull=False)
        pio.step(0)
        self.assertEqual(pio.irq, 1)

    def test_wait_gpio_sees_an_input_change_two_cycles_late(self):   # p.342
        pio, sm = machine([WAIT_1_GPIO_5, JMP_0], autopull=False, input_level=lambda gpio, t: int(t >= 10))
        for t in range(12):
            pio.step(t)
        self.assertEqual(sm.pc, 0, "not yet visible at t=11")
        pio.step(12)
        self.assertEqual(sm.pc, 1)

    def test_exec_does_not_advance_the_pc(self):
        pio, sm = machine([JMP_0, JMP_0, JMP_0])
        sm.pc = 2
        sm.fifo.append(WORD)
        sm.exec_instruction(PULL_IFEMPTY_BLOCK)
        self.assertEqual((sm.pc, sm.osr, sm.osr_count), (2, WORD, 0))

    def test_unsupported_instructions_raise(self):
        for name, word in [("IN isr", 0x40C1), ("delay", 0x0100), ("jmp !y", 0x0060),
                           ("wait pin", 0x20A0), ("irq clear", 0xC040), ("mov pins, x", 0xA001)]:
            with self.subTest(name), self.assertRaises(UnsupportedInstruction):
                decode(word)


class HeaderParserTest(unittest.TestCase):
    def test_parse_pioasm_header(self):
        text = """
#define CS_PIN 4
#define demo_wrap_target 1
#define demo_wrap 2
#define demo_offset_done 2u
static __pio_const uint16_t demo_program_instructions[] = {
    0x2084, //  0: wait   1 gpio, 4
            //     .wrap_target
    0xc000, //  1: irq    nowait 0
    0x0002, //  2: jmp    2
            //     .wrap
};
"""
        programs, defines = pio_sim.parse_pio_header(text)
        self.assertEqual(defines["CS_PIN"], 4)
        self.assertEqual(programs["demo"], {"instructions": [0x2084, 0xC000, 0x0002],
                                            "wrap_target": 1, "wrap": 2, "labels": {"done": 2}})


if __name__ == "__main__":
    unittest.main()
