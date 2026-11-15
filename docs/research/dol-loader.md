# A loader stub for ordinary `.dol` files (D43)

What a small loader stub in the payload needs to do so that ordinary `.dol` files boot from a payload slot, how other projects do it, how to build it without devkitPPC, and how to test it. For D43, an option for later releases; the owner chooses the scope from this note.

**Sources:**
- gekkoboot r9.4 (`dbf12ff`): `packer/crt0.S`, `packer/main.c`, `stub/stub.c`, `stub/stub.h`, `source/main.c` (its `__SYS_PreInit`, commit `04a0ca5`), `meson.build`;
- libogc2 at `b204695` (the version gekkoboot pins) and at `cb7745a` (HEAD); devkitPro libogc v2.14.1 and HEAD (`ef718e2`, libogc 3); devkitppc-crtls `11ee160`;
- cubiboot-new-ui (`06df58d`): `ipl/source/start.c`, `entry/`, `cubeboot/source/boot/sidestep.c`, `.ci/make_picoboot_uf2.py`;
- swiss-gc (`d817e8d`): `cube/packer/`, `cube/swiss/source/aram/sidestep.c`;
- Dolphin (`4f9d60d`): `Source/Core/Core/Boot/Boot.cpp`, `Boot_BS2Emu.cpp`;
- the `.dol` files of Swiss r2119 and cubiboot v1.13.0; Ubuntu 26.04's `binutils-powerpc-linux-gnu` 2.46.

A research agent read the projects' sources; the claims the design rests on were checked against the source afterwards. Nothing here was measured on hardware. **[inf]** marks a conclusion rather than a quoted fact.

## 1. What the payload is today, and what D43 changes

- **Today (D16):** the converter takes only a `.dol` built as an IPL replacement, loaded at and started from `0x81300000`. The boot ROM's BS1 puts the payload image there and jumps to it. Others are refused: the page and the CLI say "Invalid entry point or base address: … expected 0x81300000 / 0x01300000".
- **D43:** the payload image becomes a stub, a table and the `.dol`'s sections. The stub copies each section to its address, clears the BSS and starts the program.
- **Only the converters change** [inf]: the page's `src/web/dol.js` and the CLI build the payload, as they do now. The payload format (`IPLBOOT ` header, scrambled image, `PICO` footer), the firmware's C code, the loader, the injection and the boot log stay as they are. The firmware is rebuilt only because the page is in it.
- **Two corrections to D43's text:**
  - "as Swiss's PicoBoot build does in effect": Swiss's PicoBoot payload is its xz packer, which unpacks one flat image to `0x3100` (`cube/packer/Makefile`), like gekkoboot's. It doesn't copy `.dol` sections. The closest existing design is cubiboot's `entry.dol`: a stub at `0x81300000` with an ordinary `.dol` appended, gzipped (`.ci/make_picoboot_uf2.py`).
  - "It needs a GameCube toolchain (devkitPPC)": it doesn't (§5).

## 2. The `.dol` files it would take

| `.dol` | Sections and BSS | As a payload | Slots |
|---|---|---|---|
| cubiboot v1.13.0 `ipl.dol` | `0x80003100`–`0x80327CE0`, a 2.5 MB BSS | 734 KB, 717 transfers | both |
| Swiss r2119 `swiss_r2119.dol` | `0x80397940`–`0x804E8A80` | 1,350,656 B, 1319 transfers | both |
| Swiss r2119 `Legacy/swiss_r2119-lz.dol` | `0x80509400`–`0x806E0080` | 1.93 MB | neither |

- **Below the payload image:** the image would run from `0x81300000` to at most `0x81480000` (1.5 MB). None of these reaches it. The libogc link scripts put programs at `0x80003100` (devkitppc-crtls `ogc.ld`).
- **The size limit is the slots'** (1.5 MB, payload 2 one sector less). The console reads at most about 1.69 MiB ([ecosystem.md](ecosystem.md) §2).
- **The injection takes longer** for a `.dol` stored uncompressed, at 0.31 ms per 1 KB ([phase0-results.md](phase0-results.md)). cubiboot would take about 0.22 s, against 0.1 s for its gzipped UF2.

## 3. The state the stub starts in

- **BS1's state at `0x81300000` isn't documented.** Dolphin models it only at BS1's own start: translation on, BAT0 mapping `0x80000000` to physical 0 (cached), DBAT1 `0xC0000000` (uncached), caches on (`Boot.cpp:437-476`, `Boot_BS2Emu.cpp:116-124`). Nothing tells what BS1 leaves in `r1`.
- **gekkoboot's packer**, the code at `0x81300000` in our built-in, depends on as little of it as possible:
  1. If DABR is armed on `0x800000E8`, it writes `0xFEE1DEAD` there.
  2. It switches to real mode (`mtsrr1 0`, `rfi`: MSR = 0, translation and interrupts off). From then on it uses physical addresses and sets no BATs (`crt0.S:43-56`).
  3. It quiesces the hardware: it stops a DI transfer, resets and halts the DSP, turns audio DMA off and waits for DMAs (`crt0.S:58-102`).
  4. It points the exception vectors at a reload and sets its own stack (`crt0.S:126-141`).
  5. At the end it does `dcbst` over its output, `sync`, then `HID0 |= 0xC00` (flash-invalidates both L1 caches), and jumps to the physical entry (`main.c:109-113`).
- **Swiss's newer packer** also clears DABR and IABR, and turns the locked cache off (`cube/packer/source/crt0.S:129-156`).
- **A libogc program sets up the rest itself.** libogc2's and devkitPro libogc's crt0 set their own BATs, MSR, caches and stack before any C code. They clear their own SBSS and BSS (libogc2 `ogc_crt0.S:55-71`; libogc 3 `common_crt0.S`). Without the `_arg` magic at entry+8, argc is 0. So the stub must leave a sane machine, but needn't build the libogc environment [inf].

## 4. Low memory

The IPL normally fills low memory (`0x80000000`–`0x800030FF`) before it starts a program. Booted straight from the payload, nobody does. After a long power-off it holds garbage, and after a quick one, the last program's values.

| Library | What it reads at start-up |
|---|---|
| libogc2 (cubiboot, gekkoboot) | `0x28`/`0xF0` memory size (BAT0's size, 24 MB if 0); arena `0x30`/`0x34`/`0x38`/`0xEC`; `0x2C` console type; `0xCC` VI mode if VI is on; `0x800030D8` boot time; `0x800030E3` pad flags; at HEAD also `0xF8`/`0xFC` clocks |
| devkitPro libogc 2.x | nothing: it writes its own values (`system.c:470-519`) |
| devkitPro libogc 3 | `0x34` arena and `0x800030D8` |

- **gekkoboot's `__SYS_PreInit`** (commit `04a0ca5`, "Originally from Swiss") runs before libogc2's own set-up and gives the program a clean low memory:
  - zero `0x80000000`–`0x800030FF`;
  - `0x28` = memory size, from the memory controller's `0xCC004028 & 7` as BS1 left it;
  - `0x2C` = console type, retail or development from `0xCC006024`, plus `0xCC00302C >> 28`;
  - `0xF8` = 162000000 and `0xFC` = 486000000, the bus and CPU clocks;
  - `0x800030D8` = −timebase.
  - Its `VIDEO_Init` sets `0xCC` before it starts a `.dol`.
- **Garbage here is a likely failure on a cold boot** [inf]: a random memory size or arena breaks libogc2's start-up. cubiboot's own PicoBoot stub sets none of this and fails on the owner's cold boots (§6).

## 5. Building it without devkitPPC

- **Ubuntu 26.04's `binutils-powerpc-linux-gnu` 2.46** in our build image assembles and links a freestanding stub (`as -mregnames -m750cl`, `ld` at `0x81300000`, `objcopy -O binary`). No C, libc or SDK is involved.
- **A prototype** (copy, clear, sync, jump; in virtual mode, without §3's or §4's set-up) is 224 bytes. A full one with real mode and low memory would be a few hundred bytes [inf].
- **Where the bytes live:** the converter exists twice (`dol.js`, the CLI) and the CLI is one standard-library file. So the stub would go in as fixed bytes in both, with its assembly source in the repo. CI reassembles it and checks the bytes match. Whether binutils gives the same bytes on CI's amd64 as locally on arm64 is to be confirmed [inf: the assembler doesn't depend on the host].

## 6. Cache handling, and cubiboot's case

- **Why it matters:** the Gekko's instruction and data caches aren't coherent. Code written through the data cache must be written back (`dcbst`), then `sync`, then the instruction cache invalidated (`icbi`, or `HID0` ICFI) and `isync`, before it runs.
- **gekkoboot** writes back each section and the BSS, then `sync` and a flash invalidate of both caches (`stub.c:73-102`). A flash invalidate of the data cache drops anything not yet written back, so everything the stub still needs must be written back before it [inf: gekkoboot's stub doesn't write back the args it copied to its stack].
- **cubiboot's PicoBoot stub** (`ipl/source/start.c`) copies, clears and jumps with no cache handling and no low-memory set-up. On the owner's DOL-001 it fails after a long power-off but not a quick one, and recovers after a Swiss boot. With cubiboot's `ipl.dol` loaded by the built-in gekkoboot instead, four cold boots worked. A D43 payload of that same `ipl.dol` is a natural console test [inf].

## 7. Testing on the host

- **A small PowerPC interpreter** (a prototype of about 200 lines of Python, standard library only) runs the real stub bytes on a model of main memory. For each 32-byte line it records the last store, `dcbst` and `icbi`, and the last `sync` and `isync`.
- **On the prototype stub** with cubiboot's `ipl.dol` (2.3 million instructions, 1.9 s) and Swiss r2119 (1.0 s), with garbage memory to start:
  - every section was copied and the BSS cleared;
  - nothing was written outside the program's ranges;
  - every line written was synced before the jump;
  - the jump went to the entry.
- **It catches a missing sync:** with the cache calls taken out, it flags 43,146 unsynced lines while the copies still look right.
- **A full stub needs a few more instructions in it:** `mfspr`/`mtspr` (SRR0/1, HID0), `rfi`, `mftb`, and hardware-register reads with test values.
- **The console stays the real test:** the interpreter checks the stub's logic, not BS1's state or the hardware.

## 8. Open choices for the plan

1. **The starting state:** run in BS1's state and its virtual addresses (as the prototype), or switch to real mode first as gekkoboot's packer does. Real mode doesn't depend on BS1's undocumented BATs.
2. **Low memory:** leave it, as cubiboot does; or fill it as gekkoboot's `__SYS_PreInit` does (zero it, memory size, console type, clocks, boot time), perhaps also `0xCC`.
3. **Hardware quiescing and the exception vectors:** copy gekkoboot's packer (DI, DSP, audio DMA, DABR/IABR, vectors), or leave them as BS1 left them.
4. **Overlap:** refuse a `.dol` whose sections or BSS reach the payload image at `0x81300000`, or below `0x80003100` where low memory is. No surveyed `.dol` does either. The alternative is moving the stub first, as gekkoboot (to `0x80001000`) and Swiss do.
5. **Cache handling:** `dcbst` per range, `sync`, then `icbi` per range or one ICFI, and `isync`.
6. **argv:** none (argc 0), or the file name.
7. **Tests:** the interpreter in CI. On the console, a range of `.dol` files, one per library (libogc2, devkitPro libogc 2.x and 3, Swiss's self-unpacking build), with cold boots after a long power-off.
