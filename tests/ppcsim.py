"""
A small PowerPC interpreter for the D43 loader stub (src/dolstub/dolstub.S): only the instructions
the stub uses, on a model of the GameCube's 24 MB of main memory (Python standard library only).

It starts as Dolphin models BS1's state, which isn't documented for the hardware
(docs/research/dol-loader.md §3): translation on, BAT0 mapping 0x80000000 to physical 0, at
0x81300000. An `rfi` to MSR 0 gives real mode, where addresses are
physical. Hardware registers (physical 0x0C000000 and up) are reached only through eciwx/ecowx,
which need EAR's enable bit; a plain load or store there would be cached, and stops the run.

For each 32-byte line it records the last store, dcbst and icbi, and the last sync, isync and
instruction-cache flash invalidate (HID0 ICFI), so a test can check the cache rules at the jump.
Anything it doesn't know stops the run with Halt, including forms of known instructions the stub
doesn't use (record, link) and a data-cache flash invalidate, which would drop what isn't written
back.
"""

import struct

MEMORY = 24 * 1024 * 1024
HARDWARE = 0x0C000000
MSR_IR, MSR_DR = 0x20, 0x10
SPR_LR, SPR_CTR, SPR_SRR0, SPR_SRR1, SPR_EAR = 8, 9, 26, 27, 282
SPR_HID0, SPR_IABR, SPR_DABR = 1008, 1010, 1013
HID0_ICFI, HID0_DCFI = 0x0800, 0x0400


class Halt(Exception):
    pass


def _s16(v):
    return v - 0x10000 if v & 0x8000 else v


def _s32(v):
    return v - (1 << 32) if v & 0x80000000 else v


def _mask(mb, me):
    m = 0
    for bit in range(32):
        if (mb <= me and mb <= bit <= me) or (mb > me and (bit >= mb or bit <= me)):
            m |= 1 << (31 - bit)
    return m


class Cpu:
    def __init__(self, image, load=0x01300000, hardware=None, timebase=0x0000123456789ABC, tb_divider=1):
        """`image` goes to physical `load`; `hardware(pa)` answers eciwx reads (default 0). The time
        base is `timebase` plus the instructions run so far, divided by `tb_divider`."""
        self.mem = bytearray(MEMORY)
        self.mem[load:load + len(image)] = image
        # What BS1 leaves in the registers isn't known: garbage, so the stub must set what it uses
        self.r = [0xDEAD0000 | i for i in range(32)]
        self.pc, self.msr = 0x80000000 | load, MSR_IR | MSR_DR
        self.lr, self.ctr, self.cr, self.ca = 0xDEADBEE8, 0xDEADBEEC, 0x2A2A2A2A, 1
        # HID0 as Dolphin's BS1 sets it (0x0011C464), as a read returns it: ICFI and DCFI clear
        # themselves once the invalidate starts
        self.spr = {SPR_SRR0: 0xDEADBEE0, SPR_SRR1: 0x00003032, SPR_EAR: 0, SPR_HID0: 0x0011C064,
                    SPR_IABR: 0x80001001, SPR_DABR: 0x800000EE}
        self.hardware = hardware or (lambda pa: 0)
        self.hw_writes = []                     # (physical address, value), in order
        self.timebase, self.tb_divider = timebase, tb_divider
        self.tb_reads = []                      # (step, TBR, value)
        self.step = 0
        self.stored, self.dcbst, self.icbi = {}, {}, {}
        self.last_sync = self.last_isync = self.last_icfi = -1

    # Addresses
    def physical(self, ea, fetch=False):
        translated = self.msr & (MSR_IR if fetch else MSR_DR)
        if translated:
            if 0x80000000 <= ea < 0x80000000 + MEMORY:
                return ea - 0x80000000
            if 0xC0000000 <= ea < 0xC0000000 + MEMORY:
                return ea - 0xC0000000
            raise Halt(f"no BAT for 0x{ea:08X} at pc 0x{self.pc:08X}")
        if ea >= MEMORY:
            raise Halt(f"real-mode access outside main memory at 0x{ea:08X}, pc 0x{self.pc:08X}")
        return ea

    def load32(self, ea):
        pa = self.physical(ea)
        if pa & 3:
            raise Halt(f"unaligned load at 0x{ea:08X}")
        return struct.unpack_from(">I", self.mem, pa)[0]

    def store32(self, ea, value):
        pa = self.physical(ea)
        if pa & 3:
            raise Halt(f"unaligned store at 0x{ea:08X}")
        struct.pack_into(">I", self.mem, pa, value & 0xFFFFFFFF)
        self.stored[pa >> 5] = self.step

    def external(self, ea):
        if not self.spr[SPR_EAR] & 0x80000000:
            raise Halt(f"eciwx/ecowx with EAR off at pc 0x{self.pc:08X}")
        if self.msr & MSR_DR:
            if not 0xCC000000 <= ea < 0xCD000000:
                raise Halt(f"eciwx/ecowx at 0x{ea:08X}")
            return ea - 0xC0000000
        if not HARDWARE <= ea < HARDWARE + 0x01000000 or ea & 3:
            raise Halt(f"eciwx/ecowx at 0x{ea:08X}")
        return ea

    # Condition register
    def compare(self, crf, a, b):
        bits = 8 if a < b else 4 if a > b else 2
        shift = 28 - 4 * crf
        self.cr = (self.cr & ~(0xF << shift) & 0xFFFFFFFF) | (bits << shift)

    def record(self, value):
        self.compare(0, _s32(value & 0xFFFFFFFF), 0)

    def branch_ok(self, bo, bi):
        if not bo & 0x04:
            self.ctr = (self.ctr - 1) & 0xFFFFFFFF
            if (self.ctr != 0) == bool(bo & 0x02):
                return False
        return bool(bo & 0x10) or ((self.cr >> (31 - bi)) & 1) == bool(bo & 0x08)

    # Running
    def run(self, inside, max_steps=50_000_000):
        """Runs while the pc's physical address is in range(*inside); returns the pc it left to."""
        while self.step < max_steps:
            pa = self.physical(self.pc, fetch=True)
            if not inside[0] <= pa < inside[1]:
                return self.pc
            self.step += 1
            self.execute(struct.unpack_from(">I", self.mem, pa)[0])
        raise Halt("too many steps")

    def execute(self, ins):
        op = ins >> 26
        rd, ra, rb = (ins >> 21) & 31, (ins >> 16) & 31, (ins >> 11) & 31
        uimm, simm = ins & 0xFFFF, _s16(ins & 0xFFFF)
        r = self.r
        base = r[ra] if ra else 0
        npc = (self.pc + 4) & 0xFFFFFFFF
        if op == 18:                                            # b, bl
            li = ins & 0x03FFFFFC
            li = li - 0x04000000 if li & 0x02000000 else li
            if ins & 1:
                self.lr = npc
            npc = (li if ins & 2 else self.pc + li) & 0xFFFFFFFF
        elif op == 16:                                          # bc
            if ins & 1:
                self.lr = npc
            if self.branch_ok(rd, ra):
                bd = _s16(ins & 0xFFFC)
                npc = (bd if ins & 2 else self.pc + bd) & 0xFFFFFFFF
        elif op == 19:
            xo = (ins >> 1) & 0x3FF
            if xo == 16:                                        # bclr
                target = self.lr
                if ins & 1:
                    self.lr = npc
                if self.branch_ok(rd, ra):
                    npc = target
            elif xo == 528:                                     # bcctr
                if ins & 1:
                    raise Halt(f"bcctrl at 0x{self.pc:08X}")
                if self.branch_ok(rd | 0x04, ra):
                    npc = self.ctr
            elif xo == 50:                                      # rfi
                self.msr = self.spr[SPR_SRR1] & 0x87C0FF73
                npc = self.spr[SPR_SRR0] & 0xFFFFFFFC
            elif xo == 150:                                     # isync
                self.last_isync = self.step
            else:
                raise Halt(f"op 19/{xo} at 0x{self.pc:08X}")
        elif op == 14:                                          # addi, li
            r[rd] = (base + simm) & 0xFFFFFFFF
        elif op == 15:                                          # addis, lis
            r[rd] = (base + (simm << 16)) & 0xFFFFFFFF
        elif op == 8:                                           # subfic
            a = r[ra]
            self.ca = int((simm & 0xFFFFFFFF) + ((~a) & 0xFFFFFFFF) + 1 > 0xFFFFFFFF)
            r[rd] = (simm - a) & 0xFFFFFFFF
        elif op == 11:                                          # cmpi
            self.compare(rd >> 2, _s32(r[ra]), simm)
        elif op == 10:                                          # cmpli
            self.compare(rd >> 2, r[ra], uimm)
        elif op == 24:                                          # ori
            r[ra] = r[rd] | uimm
        elif op == 25:                                          # oris
            r[ra] = r[rd] | (uimm << 16)
        elif op == 28:                                          # andi.
            r[ra] = r[rd] & uimm
            self.record(r[ra])
        elif op == 21:                                          # rlwinm
            sh, mb, me = rb, (ins >> 6) & 31, (ins >> 1) & 31
            v = r[rd]
            v = ((v << sh) | (v >> (32 - sh))) & 0xFFFFFFFF if sh else v
            r[ra] = v & _mask(mb, me)
            if ins & 1:
                self.record(r[ra])
        elif op == 32:                                          # lwz
            r[rd] = self.load32((base + simm) & 0xFFFFFFFF)
        elif op == 33:                                          # lwzu
            if ra in (0, rd):
                raise Halt(f"invalid lwzu at 0x{self.pc:08X}")
            r[ra] = (r[ra] + simm) & 0xFFFFFFFF
            r[rd] = self.load32(r[ra])
        elif op == 36:                                          # stw
            self.store32((base + simm) & 0xFFFFFFFF, r[rd])
        elif op == 37:                                          # stwu: stores rS as it was
            if ra == 0:
                raise Halt(f"invalid stwu at 0x{self.pc:08X}")
            value, r[ra] = r[rd], (r[ra] + simm) & 0xFFFFFFFF
            self.store32(r[ra], value)
        elif op == 31:
            self.execute_31(ins, rd, ra, rb, base)
        else:
            raise Halt(f"op {op} at 0x{self.pc:08X}")
        self.pc = npc

    def execute_31(self, ins, rd, ra, rb, base):
        r = self.r
        xo = (ins >> 1) & 0x3FF
        spr = ((ins >> 16) & 31) | (((ins >> 11) & 31) << 5)
        if ins & 1:
            raise Halt(f"the record form of 31/{xo} at 0x{self.pc:08X}")
        if xo == 339:                                           # mfspr
            r[rd] = self.lr if spr == SPR_LR else self.ctr if spr == SPR_CTR else self.spr_value(spr)
        elif xo == 467:                                         # mtspr
            if spr == SPR_LR:
                self.lr = r[rd]
            elif spr == SPR_CTR:
                self.ctr = r[rd]
            else:
                self.spr_value(spr)
                if spr == SPR_HID0 and r[rd] & HID0_DCFI:
                    raise Halt(f"a data-cache flash invalidate at 0x{self.pc:08X}")
                if spr == SPR_HID0 and r[rd] & HID0_ICFI:
                    self.last_icfi = self.step
                self.spr[spr] = r[rd] & (~HID0_ICFI if spr == SPR_HID0 else 0xFFFFFFFF)
        elif xo == 371:                                         # mftb
            tb = (self.timebase + self.step // self.tb_divider) & 0xFFFFFFFFFFFFFFFF
            if spr == 268:
                r[rd] = tb & 0xFFFFFFFF
            elif spr == 269:
                r[rd] = tb >> 32
            else:
                raise Halt(f"mftb {spr}")
            self.tb_reads.append((self.step, spr, r[rd]))
        elif xo == 266:                                         # add
            r[rd] = (r[ra] + r[rb]) & 0xFFFFFFFF
        elif xo == 200:                                         # subfze
            total = ((~r[ra]) & 0xFFFFFFFF) + self.ca
            r[rd], self.ca = total & 0xFFFFFFFF, int(total > 0xFFFFFFFF)
        elif xo == 23:                                          # lwzx
            r[rd] = self.load32((base + r[rb]) & 0xFFFFFFFF)
        elif xo == 0:                                           # cmp
            self.compare(rd >> 2, _s32(r[ra]), _s32(r[rb]))
        elif xo == 32:                                          # cmpl
            self.compare(rd >> 2, r[ra], r[rb])
        elif xo == 310:                                         # eciwx
            r[rd] = self.hardware(self.external((base + r[rb]) & 0xFFFFFFFF)) & 0xFFFFFFFF
        elif xo == 438:                                         # ecowx
            self.hw_writes.append((self.external((base + r[rb]) & 0xFFFFFFFF), r[rd]))
        elif xo == 54:                                          # dcbst
            self.dcbst[self.physical((base + r[rb]) & 0xFFFFFFFF) >> 5] = self.step
        elif xo == 982:                                         # icbi
            self.icbi[self.physical((base + r[rb]) & 0xFFFFFFFF) >> 5] = self.step
        elif xo == 598:                                         # sync
            self.last_sync = self.step
        else:
            raise Halt(f"op 31/{xo} at 0x{self.pc:08X}")

    def spr_value(self, spr):
        if spr not in self.spr:
            raise Halt(f"SPR {spr} at 0x{self.pc:08X}")
        return self.spr[spr]

    # Checks
    def cache_problems(self):
        """Physical lines stored to but not written back (dcbst, then sync) and dropped from the
        instruction cache (icbi or ICFI after that, then isync) by the end of the run."""
        bad = []
        for line, stored in self.stored.items():
            written = self.dcbst.get(line, -1)
            dropped = max(self.icbi.get(line, -1), self.last_icfi)
            if not (stored < written < self.last_sync < dropped < self.last_isync):
                bad.append(line << 5)
        return bad
