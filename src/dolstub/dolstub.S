/*
 * Copyright (c) 2026 Remko Kleinjan
 * The start (real mode, the hardware quiesced) follows gekkoboot r9.4's packer/crt0.S,
 * Copyright (c) 2012-2025 Extrems (GPL-2.0-or-later); low memory follows gekkoboot's
 * __SYS_PreInit (source/main.c).
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

/*
 * The loader stub for ordinary .dol files (design D43, docs/research/dol-loader.md).
 *
 * The converters (src/web/dol.js, tools/picoboot-wifi.py, tools/dolstub.py) make a payload image
 * of this stub, then a table, then the .dol's sections. The boot ROM's BS1 puts the image at
 * 0x81300000 and jumps to it. The table, at the end of this stub (big-endian words):
 *
 *     +0   entry point           the .dol's, as in its header
 *     +4   BSS address           as in the header
 *     +8   BSS size              a multiple of 4
 *     +12  section count
 *     +16  per section: address, offset of its data from the table, size (multiples of 4)
 *
 * Addresses are the .dol's own (0x80000000 and up); the stub uses their physical address. The
 * converters refuse a .dol that reaches the image (0x81300000 and up) or low memory (below
 * 0x80003100), so nothing here overwrites the stub, its data or what it set up.
 *
 * It uses no stack and no memory of its own besides the image. In order:
 *   1. real mode, as gekkoboot's packer: nothing depends on BS1's undocumented BATs;
 *   2. DABR and IABR cleared; the hardware quiesced: a DI transfer stopped, the DSP reset and
 *      halted, audio DMA off, the DMAs finished;
 *   3. low memory zeroed, then the values gekkoboot's __SYS_PreInit fills in; each exception vector
 *      branches to itself, except the system call's, which returns (as Swiss's packer: libogc's
 *      cache flushes end in sc, which a program may run before it sets up its own handlers);
 *   4. the BSS zeroed, then each section copied (a section that shares the BSS's range wins);
 *   5. everything written back from the data cache, the instruction cache invalidated, then a
 *      jump to the entry's physical address, still in real mode (libogc's crt0 sets up its own).
 */

        .equ    DABR, 1013
        .equ    IABR, 1010
        .equ    HID0, 1008
        .equ    EAR, 282

        .text
        .globl  _start
_start:
        # BS1's state: translation on, running at 0x81300000. Switch to real mode at the physical
        # address this stub is linked at (0x01300000)
        li      r3, 0
        mtsrr1  r3
        lis     r3, startup@ha
        addi    r3, r3, startup@l
        mtsrr0  r3
        rfi

startup:
        bl      1f
1:      mflr    r31                     # r31: this point's physical address

        # In real mode, loads and stores are cached; eciwx/ecowx reach the hardware registers
        # uncached, once EAR enables them
        lis     r0, 0x8000
        ori     r0, r0, 0x000C
        mtspr   EAR, r0
        lis     r3, 0x0C00              # The hardware registers (0xCC000000 with translation)

        # No breakpoints (as Swiss's newer packer)
        li      r0, 0
        mtspr   DABR, r0
        mtspr   IABR, r0

        # DI: a transfer running is asked to stop (DICR TSTART, then DISR BRK)
        li      r4, 0x601C
        eciwx   r0, r4, r3
        andi.   r0, r0, 1
        beq     2f
        li      r4, 0x6000
        eciwx   r0, r4, r3
        ori     r0, r0, 1
        ecowx   r0, r4, r3

        # The DSP reset and halted (DSPCR, in the word at 0x5008), audio DMA off (0x5036's bit 15),
        # then wait until the DSP and ARAM DMAs are done (DSPCR 0x0600)
2:      li      r4, 0x5008
        li      r0, 0x804
        ecowx   r0, r4, r3
        li      r5, 0x5034
        eciwx   r0, r5, r3
        rlwinm  r0, r0, 0, 17, 15
        ecowx   r0, r5, r3
3:      eciwx   r0, r4, r3
        rlwinm. r0, r0, 0, 21, 22
        bne     3b

        # Low memory: 0x0000-0x30FF zeroed
        li      r4, 0
        li      r0, 0x3100 / 4
        mtctr   r0
        li      r5, -4
4:      stwu    r4, 4(r5)
        bdnz    4b

        # Each exception vector (0x100-0x1700) branches to itself: an exception before the program
        # sets up its own stops there
        lis     r4, 0x4800
        li      r5, 0x100
5:      stw     r4, 0(r5)
        addi    r5, r5, 0x100
        cmplwi  r5, 0x1700
        ble     5b

        # The system call (0xC00) returns: rfi, as Swiss's packer leaves it
        lis     r4, 0x4C00
        ori     r4, r4, 0x0064
        stw     r4, 0xC00(0)

        # 0x28: the memory size, from the memory controller as BS1 set it up (0x4028 & 7)
        li      r4, 0x4028
        eciwx   r0, r4, r3
        rlwinm  r0, r0, 18, 27, 29      # ((value >> 16) & 7) * 4
        addi    r5, r31, memory_sizes - 1b
        lwzx    r0, r5, r0
        stw     r0, 0x28(0)

        # 0x2C: the console type, retail or development (DICFG 0xFF), plus the revision (0x302C)
        li      r4, 0x6024
        eciwx   r0, r4, r3
        li      r6, 1                   # Retail, HW1
        cmpwi   r0, 0xFF
        beq     6f
        lis     r6, 0x1000
        ori     r6, r6, 4               # Development, HW1
6:      li      r4, 0x302C
        eciwx   r0, r4, r3
        srwi    r0, r0, 28
        add     r6, r6, r0
        stw     r6, 0x2C(0)

        # 0xF8 and 0xFC: the bus and CPU clocks
        lis     r0, 162000000@h
        ori     r0, r0, 162000000@l
        stw     r0, 0xF8(0)
        lis     r0, 486000000@h
        ori     r0, r0, 486000000@l
        stw     r0, 0xFC(0)

        # 0x30D8: minus the time base, read consistently (64 bits)
7:      mftbu   r5
        mftb    r6
        mftbu   r7
        cmpw    r5, r7
        bne     7b
        subfic  r6, r6, 0
        subfze  r5, r5
        stw     r5, 0x30D8(0)
        stw     r6, 0x30DC(0)

        li      r8, 0
        li      r9, 0x3100
        bl      write_back

        # The program, from the table
        addi    r30, r31, table - 1b
        lwz     r28, 0(r30)             # Entry point
        lwz     r8, 4(r30)              # BSS address
        lwz     r9, 8(r30)              # BSS size
        clrlwi  r8, r8, 2
        srwi.   r0, r9, 2
        beq     8f
        mtctr   r0
        addi    r5, r8, -4
        li      r4, 0
9:      stwu    r4, 4(r5)
        bdnz    9b
8:      bl      write_back

        lwz     r27, 12(r30)            # Section count
        addi    r26, r30, 16
10:     cmpwi   r27, 0
        beq     12f
        lwz     r8, 0(r26)              # Address
        lwz     r5, 4(r26)              # Offset of its data from the table
        lwz     r9, 8(r26)              # Size
        clrlwi  r8, r8, 2
        add     r5, r5, r30
        srwi.   r0, r9, 2
        beq     11f
        mtctr   r0
        addi    r5, r5, -4
        addi    r6, r8, -4
13:     lwzu    r4, 4(r5)
        stwu    r4, 4(r6)
        bdnz    13b
11:     bl      write_back
        addi    r26, r26, 12
        addi    r27, r27, -1
        b       10b

        # Everything written is in memory; the instruction cache is invalidated (HID0 ICFI),
        # then the program starts at its entry's physical address
12:     sync
        mfspr   r0, HID0
        ori     r0, r0, 0x0800
        mtspr   HID0, r0
        isync
        clrlwi  r28, r28, 2
        mtctr   r28
        bctr

# Writes back the data cache lines of r9 bytes from r8 (dcbst; the sync comes before the jump)
write_back:
        cmpwi   r9, 0
        beqlr
        add     r9, r9, r8
        clrrwi  r8, r8, 5
14:     dcbst   0, r8
        addi    r8, r8, 32
        cmplw   r8, r9
        blt     14b
        blr

        .balign 4
# The memory size for each value of the memory controller's 0x4028 & 7 (gekkoboot's __SYS_PreInit)
memory_sizes:
        .long   0x01000000, 0x02000000, 0x01800000, 0x03000000
        .long   0x02000000, 0x04000000, 0x01800000, 0x03000000

        .balign 32
table:
