# GameCube documents: YAGCD, the Shuriken attack and PicoBoot's wiring

What three sets of documents the owner collected (2026-11-23) say about the IPL chip, the EXI bus and the boot, checked against this project's measurements and notes. **[inf]** marks a conclusion rather than a measured or sourced fact.

**Short answer:** they say nothing about CS's level at power-up, the power boards or the board revisions. But they confirm from the console's side what T3 measured:
- BS1 is the 224 transfers PicoBoot lets by;
- the transfer shapes, and with them the EXI clock;
- U10's pins, and the bus U10 shares.

They add four things:
- another modchip's way to find the injection point, by a transfer's length instead of a count;
- that modchip's own pull-up on CS;
- how PicoBoot drives the data line against U10 (with the owner's note);
- why PicoBoot's diagram joins GP6 and GP7.

## 1. The documents

| Document | What it is |
|---|---|
| **YAGCD**, "Yet Another Gamecube Documentation", groepaz/hitmen, version of 2006-12-23 | 244 pages pasted together from freely available documents and the author's findings. It says itself that its information "may be missing, incomplete or even plain wrong" |
| **The Shuriken attack**, Steven Taffs, 2014–2016 | A modchip: a Xilinx CPLD and an M25P40/M25P80 serial flash between the Flipper and U10. A web page, the schematic, a parts list, the CPLD's VHDL, flashing tools and installation photos |
| **PicoBoot's wiring diagram**, webhdx, 2022 | The official diagram for the DOL-001 and the DOL-101, and three installation photos with the same points on U10 (DOL-001 top and bottom, DOL-101) |

## 2. What they confirm

| Fact | Here before | The documents |
|---|---|---|
| **BS1 is the 224 transfers.** BS1 is about 0x700 bytes. The CPU runs it straight from U10 at the reset vector, fetching 8 bytes at a time, one transfer each: 0x700 / 8 = 224 | an inference in [hardware.md](hardware.md) §3; measured in T3 | YAGCD 2.8.3 and 18.2; the Shuriken page ("BL1 … approx 0x700 bytes … loaded 8 bytes (64 bits) at a time") |
| **The transfer shapes:** the Flipper sends a 32-bit command (1 bit read/write, 1 unknown bit, 1 bit ROM or RTC/SRAM, a 23-bit address, 6 dummy bits), then the data comes. 96 clocks for a BS1 fetch; 32 + 8192 for a 1 KB block | measured in T3 ([cs-timing-results.md](cs-timing-results.md)) | YAGCD 2.8.1; the Shuriken page ("12 bytes (96 bits)", "1028 bytes in total") |
| **U10's pins:** 1 CLK, 5 CS, 6 serial in, 7 GND, 9 serial out, which is PicoBoot's Data. 12 and 13 take the crystal | the wiring, and pin 5 and pin 6 in [cs-at-start-up.md](cs-at-start-up.md) §7 | YAGCD 2.4.2.1. PicoBoot's photos add pin 3, 3V3 |
| **EXI channel 0 is shared:** its clock and data lines serve memory card slot A (device 0), U10 (device 1: the mask ROM, the RTC and the SRAM in one chip) and serial port 1 (device 2), each with its own CS | D36's case of CLK pulses while CS is high | YAGCD 10.1 |

**The EXI clock, from the shapes:**
- T3's blocks last 307.36 µs. At 32 + 8192 clocks, that's **26.76 MHz**. T3 fitted the same figure assuming 96 and 8224 clocks ([cs-timing-results.md](cs-timing-results.md) §2); the documents now confirm those counts.
- At that clock, T3's BS1 fetches (CS low for 3.656–3.664 µs) are 97.8–98.0 clocks: the 96, plus about 2 clocks of CS set-up and hold [inf].
- YAGCD calls the fastest setting "32 MHz" (`EXI0CSR` CLK = 5). It gives the Gekko–Flipper bus as about 162 MHz, and 162 / 6 = 27 MHz, which fits [inf].

**The descrambler (YAGCD 2.8, 2.8.3, 5.9):**
- The IPL in U10 is XORed with a key stream. The Flipper removes it in hardware, between U10's output and its shift register.
- The stream moves on with every transfer, not with the address: "reading from 0xFFF00100 more than one time will give you each time another result".
- BS1 turns it off (`EXI0CSR` ROMDIS) once BS2 is loaded.
- So a payload has to be scrambled for its exact place in the stream. That's why sending it one transfer early, as a counted glitch does (D36), breaks the boot [inf].

## 3. What's new for this project

**1. An injection point found by length, not by a count.**
- **How the Shuriken CPLD works** (its VHDL, version 1.2):
  - it counts clocks within each CS-low period, from 0 every time CS goes high;
  - the first transfer that passes 255 clocks is the first IPL block, since BS1's fetches are 96 clocks;
  - in that transfer it starts the serial flash, and at clock 287 switches the Flipper's data input over to it;
  - in every later block it switches right after the 32-bit command.
- **What that's immune to [inf]:**
  - a CS pulse without clocks, such as the dev console's false edge, so D36's known limit (CLK already high at the false edge) wouldn't apply;
  - noise that splits BS1 transfers, which failed the second console's boots at its TV ([main-console-tv.md](main-console-tv.md)), because the pieces stay short.
- **What it gives up:**
  - it only knows a transfer is a block after more than 96 clocks, so the first block's first 32 bytes still come from U10. The page: "IPL executable data starts to get loaded into memory after the first 32 bytes of the first 1028 chunk";
  - its image starts 32 bytes into BS2. PicoBoot's payloads start at the first block's first byte.
- A research option, not a proposal: D36's count has handled every glitch seen.

**2. The Shuriken board pulls CS up.**
- Its schematic has **R1, 10 kΩ from 3.3 V to EXI_CS,** which is also the CPLD's `exi_cs` input (pin 32 in its pin file). That's about five times stronger than the RP2350's internal pull-up.
- **A likely reason [inf]:** the CPLD drives the Flipper's data input whenever CS reads low (`exi_serial_to_gc … when (exi_cs = '0')`). An undriven CS at power-up would make it drive.
- **What it shows:** another modchip defines CS itself. That fits a console like the dev console leaving CS undriven at first ([cs-at-start-up.md](cs-at-start-up.md)).
- **What it doesn't change:** release 1.1.1 dropped PicoBoot-WiFi's pull-up for the second console's sake (D36).

**3. PicoBoot drives the data line against U10.**
- **The Shuriken board lifts U10 pin 9,** "the same as tmbinc's original mod chip install", and its CPLD is then the only driver of the Flipper's data input. PicoBoot's diagram and photos solder to pin 9 as it is.
- **The owner (2026-11-23):** the first PicoBoot drove GP6 and GP7 together to overpower U10's output. Later one pin was enough, at a higher drive. Upstream has driven a single pin at 12 mA since `e9f420b` (2024).
- **So during the payload, the Pico and U10 both drive pin 9's line,** and the Pico wins.
- **What follows [inf]:** whenever a payload bit differs from the bit U10 is sending, current flows from one driver into the other. On the Pico's side it returns through the ground wire.
- **The pull-up research fits** ([cs-pull-up.md](cs-pull-up.md)):
  - with the long ground wire, 12 mA gave two to six times more CS transfers than 8 mA. With Swiss r2119 and the pull-down, that was 339,265–340,449 against 56,146–57,284;
  - a stronger driver means a bigger current in the fight, and so more ground bounce;
  - that note's "ground bounce" now has a mechanism beyond the data line's switching.
- **PicoBoot-WiFi drives GP6 alone at 8 mA,** with fast slew (`src/firmware/injection.c`). That beats U10 on both consoles: with clean wiring, every power-on has started its payload (T3, and the console tests of 1.1.1 and 1.1.2). It also puts less current in the fight than upstream's 12 mA [inf].
- **The other side:** a console whose U10 drives harder could need more [inf]. That's not seen.
- **Not shown by these documents:** how hard U10 drives, or a scope trace of pin 9 during a block.

**4. GP6 and GP7.**
- PicoBoot's diagram says: "GP6 and GP7 are connected together. Use a single wire to connect them to the corresponding pin on GC board." That's the first PicoBoot's two-pin drive (point 3).
- PicoBoot-WiFi sets up only GP4, GP5 and GP6, so on a board wired that way GP7 stays as reset leaves it: undriven, with its pull-down. It only adds a second weak pull-down to the data line [inf], next to GP6's own reset pull-down, which the firmware keeps ([injection-end.md](injection-end.md)).
- The README links to that diagram and didn't mention GP7.

## 4. Smaller notes

- **The power connector** (P1, YAGCD 2.4.1.6) brings separate 3.3 V, 1.8 V and 1.55 V rails, plus 5 V and 12 V, and a "thermo detect" line. That fits the idea that the order in which the power board brings its rails up sets when Flipper first drives CS ([cs-at-start-up.md](cs-at-start-up.md) §2) [inf].
- **Boot 108** ([wiring-quality.md](wiring-quality.md) §4) counted 5368 transfers on clean wiring.
  - **What YAGCD adds:** the descrambler gives different data for a second read of the same address. So a console that re-read its IPL in the same power-on would get garbage, and might crash or read again [inf].
  - **The limit:** that would fit many extra transfers, but it proves nothing about boot 108.
- **PicoBoot's diagram takes GND, CLK and 3V3 at memory card slot A's connector** (P4), as the Shuriken photos take GND and 3.3 V. Since EXI channel 0's clock is shared (§2), CLK there is the same line as at U10 pin 1.
- **YAGCD contradicts itself on BS1's switch:**
  - 18.2.1 says BS1 clears bit 17 of `0xCC006800`;
  - 5.9 has the switch as ROMDIS, bit 13, set to 1.

  It doesn't matter here.

## 5. Corrections to this project's notes

- **[hardware.md](hardware.md) §3** said BS1 "is fetched in 32-byte bursts (gc-ipl)". It's fetched 8 bytes at a time, one transfer each (§2).
- **[cs-pull-up.md](cs-pull-up.md)** gave the ground bounce as "the data line's switching current". The fight with U10 on that line is the likelier larger part (§3, point 3) [inf].
