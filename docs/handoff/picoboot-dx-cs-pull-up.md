# Handoff to PicoBoot-DX: drop the CS pull-up from v0.11

**From:** PicoBoot-WiFi (`~/GitHub/PicoBoot-WiFi`), release 1.1.1, 2026-11-20.
**For:** whoever works on PicoBoot-DX (`~/GitHub/PicoBoot-DX`, v0.12). It follows the phase 0 handoff, [picoboot-dx-cs-glitch.md](picoboot-dx-cs-glitch.md), whose two fixes went into PicoBoot-DX v0.11.

**In short:**
- **The failure:** on the owner's second DOL-001, PicoBoot-DX **v0.9 and v0.10 boot, v0.11 and v0.12 don't**. The console starts its own IPL menu instead of the payload.
- **The cause, very likely [inf: v0.11 without the pull-up wasn't tested]:** the CS pull-up from v0.11 (`0158695`, `gpio_pull_up(cs_pin)` in `src/pio.c`). With it, that console shows one to four extra CS transfers on about half the boots. They're clocked, so the clock-qualified count from v0.11 (`0be9e59`) counts them too, and the payload goes out early.
- **The change:** remove the pull-up, so CS keeps the pad's reset pull-down as before v0.11. **Keep the clock-qualified count:** on the phase 0 console it ignores the false edge that the pull-up was added for.
- **Status:** PicoBoot-WiFi 1.1.1 makes this change. Nothing has changed in PicoBoot-DX; whether and how to take it is up to the owner.

## 1. The evidence

**The second console:** the owner's main DOL-001, with a GC2SD in a memory-card slot and an HDMI adapter. It now has a new Pico 2 W (RP2350 A2), on the wires of an earlier Pico that ran PicoBoot-DX, on 3V3. Full research in `~/GitHub/PicoBoot-WiFi/docs/research/cs-pull-up.md`.

- **The bisect, by the owner:** PicoBoot-DX v0.9, v0.10, v0.11 and v0.12, on the same Pico, after a ground-wire fix (§2). Only v0.9 and v0.10 boot.
- **PicoBoot-WiFi test builds on the same console**, with a switch between the pull-up and the reset pull-down. The built-in gekkoboot gives 285 CS transfers on a good boot:

  | Skip count | CS | Boots | CS transfers | Booted |
  |---|---|---|---|---|
  | every CS edge (as v0.10) | pull-up | 7 | 289, 285, 286, 285, 285, 285, 286 | only the 285s |
  | every CS edge (as v0.10) | reset pull-down | 7 | 285 every time | 7 of 7 |
  | clock-qualified (as v0.11) | reset pull-down | 7 | 285 every time | 7 of 7 |
  | clock-qualified (as v0.11) | pull-up | 6 | 286, 287, 285, 285, 286, 286 | 2 of 6, the 285s |

  The extra transfers with the pull-up are clocked: a passive raw counter and a passive clock-qualified counter agree on every one. So no skip count can tell them from real transfers. How the pull-up produces them on this console isn't known. CS reads high when the PIO starts on every boot there, in both modes.

**The phase 0 console without the pull-up:** the clock-qualified count alone catches the false edge the pull-up was meant to prevent.
- Phase 1: 2 glitches in 40 boots, 36–37 ms after the PIO started. Both were ignored, and Swiss started.
- PicoBoot-WiFi's 1.1.1 series: 51 boots, all starting their payload. One glitch at 36.5 ms was ignored (raw 1545, clocked 1544).

## 2. Also learned: a long ground wire

Before the bisect, the second console also failed with the pull-down. The CS counters read tens or hundreds of thousands of edges on every boot, rising with the data pin's drive strength (8 mA to 12 mA: two to six times more). Shortening the Pico's ground wire to a point near U10 brought them back to 285 [inf: the data line's switching bounced the Pico's ground]. PicoBoot-DX has no counters to show this, so a short ground wire is worth a line in its install notes.

## 3. The change in PicoBoot-DX

- **`src/pio.c`:** remove `gpio_pull_up(cs_pin);` and its comment from `on_transfer_program_init()`, which `0158695` added. Keep `sm_config_set_jmp_pin(&c, cs_pin);` and the `.pio` changes from `0be9e59`.
- **Tests:** the PIO simulator doesn't model pad pulls, so its tests pass unchanged. But its console starts CS high (`tests/pio_sim.py`), as the pull-up left it. A case with CS low at the start, then driven high, with and without a clockless glitch, covers the start the pull-down gives; PicoBoot-WiFi 1.1.1 added these (`cs_low_until` in its `tests/pio_sim.py`).
- **On hardware:** cold boots on a console like the phase 0 one, where CS reads low when the PIO starts, should still start every time. On one like the second console, they should now start too.

## 4. Also learned: CS at start-up

From `~/GitHub/PicoBoot-WiFi/docs/research/cs-at-start-up.md`:
- **The false edge isn't a release, on A2 chips [inf]:** RP2350-E9 holds a line released from high at about 2.2 V, which reads high. The edge needs CS to start low (5 in 144 boots, against 0 in 90 starting high), so the console's first rise of CS most likely isn't clean. The comment on v0.11's pull-up ("a brief release by the console reads as a CS pulse") goes with the pull-up.
- **Where 224 comes from:** the boot ROM reads the IPL's first 0x700 bytes in 8-byte transfers (CCC 2004, "Gamecube Hacking"); 0x700 ÷ 8 = 224. The 225th transfer is the first 1024-byte block.
