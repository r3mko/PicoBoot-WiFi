# The end of the injection

What the firmware leaves running after the injection, whether tidying it up can affect Wi-Fi, and whether the CS counters could spot a console reset. For release 1.0.1 (the plan's "Release 1.0.1"); the owner chose its scope from this note.

**Sources:** the firmware at `1.0.0` (`src/firmware/injection.c`, `counters.c`, `main.c`), the Pico SDK 2.3.1 in the build image (`docker/Dockerfile`), [gekkoboot](https://github.com/redolution/gekkoboot) (`28a1bf2`, `source/main.c`) and [libogc](https://github.com/devkitPro/libogc) (`libogc/system.c`). Nothing here was measured on hardware.

## 1. What's left after `injection_finish()`

| What | State at 1.0.0 | Matters? |
|---|---|---|
| System clock | Back to 150 MHz (`SYS_CLK_KHZ`) | Yes: Wi-Fi needs it (below) |
| Core voltage | Never changed: in SDK 2.3.1, `SYS_CLK_VREG_VOLTAGE_AUTO_ADJUST` is on only for an RP2040 at 200 MHz, and `set_sys_clock_khz()` never touches the regulator. The 250 MHz boost runs at the default voltage | Nothing to put back |
| pio0 (the injection) | Both state machines disabled. Their programs stay loaded and the state machines stay claimed | No |
| pio1 (the CS counters) | Both keep running until power-off, stalled on a `WAIT` between CS edges. Their counts are read once, by `counters_snapshot()` | Barely (section 3) |
| The injection's DMA channel | Stays claimed (1 of 16) | No |
| Bus priority | "DMA first" (`bus_ctrl_hw->priority`, set in `injection_prepare()`) | Barely (section 2) |
| GP4, GP5, GP6 | pio0 inputs. CS keeps its pull-up (D36); DATA keeps fast slew and 8 mA, its input enabled and the reset pull-down | DATA's input: section 4 |

Design D31 says the counter "counts every CS pulse until the injection ends": the counters running on is a gap between the code and the design, not a choice.

## 2. Wi-Fi

- **The clock has to come down:** the CYW43 SPI link runs from the system clock through a fixed PIO divider of 2 (`CYW43_PIO_CLOCK_DIV_INT`). At 150 MHz that gives 37.5 MHz; at 250 MHz it would be 62.5 MHz, above the CYW43439's 50 MHz. 1.0.0 already lowers it before Wi-Fi starts.
- **Wi-Fi's PIO:** `pio_claim_free_sm_and_add_program_for_gpio_range()` searches the PIO blocks from the last one down (`while (pio_num--)`), so the driver lands in pio2, which nothing else uses. Freeing pio0 or pio1 doesn't move it.
- **Wi-Fi's DMA:** two channels, claimed without a preference, paced by its PIO.
- **Bus priority:** with "DMA first", the CPU waits when it and the DMA reach for the same memory at once. At the SPI link's rate that's rare, so no visible effect is expected either way. Priority 0 is the SDK's default, where they take turns.
- **One figure may move:** the "DMA 64 KB" time (`flash_dma_64k_us()`) is measured after the injection, so far under "DMA first". Only test builds show it, on their `/test` page, so the re-test leaves it out (the code review, the owner's choice).

## 3. The CS counters

- **Cost:** no PIO interrupt is enabled for them, so their `irq set` never wakes the main loop's `__wfi()`. Two stalled state machines should draw far too little to see next to the radio (not measured).
- **Wrapping:** each counts X down from `0xFFFFFFFF`; at 0, `jmp x--` falls through with X back at `0xFFFFFFFF`, so they wrap after 2³² edges without stalling. A reader that compares two counts uses unsigned 32-bit subtraction.
- **A console reset re-reads U10:**
  - The GameCube's reset button is an interrupt (`IRQ_PI_RSW`); software answers it with a hot reset. libogc's `__reload()` calls `__dohotreset()`, which calls `PPCHotReset()`.
  - gekkoboot relies on this: holding reset or D-Pad Left turns a Qoob modchip off and returns from `main()`, which "reboots into the original IPL". That only works if a hot reset reads the IPL from U10 again.
  - So after a reset the counters should see a new burst, probably like a boot's 1,544 CS edges (phase 0). Not measured.
- **The count alone can't tell a reset apart:** after boot the console also uses U10 for its fonts (large ROM reads), the clock and SRAM. gekkoboot itself writes to U10's EXI device on every boot (address `0xC0000000`, the Qoob's off switch). A reset shows clearly only in the burst's timing, or in the addresses on a fourth wire (the plan's later-releases item: reads from the start of the ROM).
- **Stopping them closes nothing:** a later feature can start the counters again with `counters_start()`, after clearing their PIO IRQ flags 2 and 3, which stay set (otherwise `counters_first_*_seen()` is true at once).

## 4. The DATA pin after its release

- **Today:** the DATA pad keeps its input enabled, with the reset pull-down. When no EXI device drives the line, RP2350-E9 (A2 chips, the owner's) could hold it at about 2.2 V through a ~120 µA leakage the pull-down can't overcome. Those figures are the erratum's (datasheet Appendix D.5.1, quoted in phase 0); phase 1 saw its effect on CS, which read high, but no voltage was measured.
- **Harmless:** whichever device the console selects drives the line easily. PicoBoot and PicoBoot-DX have always left the pad like this.
- **Turning the input off** (`gpio_set_input_enabled(INJECTION_PIN_DATA, false)`) removes that leakage path. Nothing reads DATA after the injection: `injection_complete()` reads the state machine's program counter, not the pin.
- **Optional check:** with a multimeter, the DATA line's idle voltage with the console in a game, on 1.0.0 and on 1.0.1.

## 5. Conclusions for release 1.0.1

The owner's choices (2026-11-14): the tidy end with DATA's input off, and a focused re-test.

1. **Stop the counters right after their snapshot** (`counters_stop()`, from `inject()` in `main.c`). It brings the code in line with D31.
2. **In `injection_finish()`:** set the bus priority back to 0 once the DMA is done, and turn DATA's input off once the state machines are disabled.
3. **Left as they are:** the loaded programs, the claimed state machines and DMA channel, the CS pull-up, and DATA's slew, drive strength and pull-down. They have no effect, and leaving them keeps the change small.
4. **The re-test** covers both paths through `injection_finish()`: a complete injection (a console boot) and an incomplete one (the Pico on USB alone: "no console seen"), plus Wi-Fi and the page.
