# Release 1.2.0 results

The console test of release 1.2.0 (the plan's step R5), run by the owner on 2026-11-24 on both consoles, on clean wiring. **[inf]** marks a conclusion rather than a measured or sourced fact.

**Status:** R5 passed. Approved by the owner with plan v1.35.

**Setup:**
- **The dev console:** the owner's DOL-001 with the quick-solder flex cable ([main-console-tv.md](main-console-tv.md) §6). Before the test: 1.1.2 in slot A, and L8's length test build in slot B.
- **The main console:** the second DOL-001, with the other Pico, new wires and the CubeSB board at U10 ([main-console-tv.md](main-console-tv.md)). Before the test: 1.1.2 in slot A, and L4's capture test build in slot B [inf].
- **The payloads:** Swiss r2174 in payload 2 on both (885 transfers, so 1110 CS transfers expected), and the built-in gekkoboot (60 transfers, so 285).

Boot numbers are each console's boot log, saved after the boots (`/api/diagnostics`).

**Build:** `1.2.0` from `74c76f6`, the build after the code review (R4):

| File | SHA-256 |
|---|---|
| `picoboot-wifi-1.2.0-update.bin` | `1d36be404b992e304d09f1458c9346b122f1319500fccaf5ed5a4ca2cc69f48c` |
| `picoboot-wifi-1.2.0.uf2` | `f7968576266396f4bc98a2bcd2e588e1fb4e8367a043bee94ecaf18c291a8a4d`, from the same build. R5 didn't install it |
| `picoboot-wifi.py` | `cc1b04f685aee500f7109a389d0b73da1ae31565ac1756797da2116d9b37ed0b` |

## The checks

| Check | Boots | Result |
|---|---|---|
| The update over Wi-Fi from 1.1.2 | dev 40, 41; main 3, 4 | **Passed:** into slot B, then its restart, a warm boot, "not run", on trial. The next power-on, with the built-in, kept it |
| Standard, with the new waits before each block | dev 41–42, main 4–5 | **Passed:** the built-in and Swiss on each console, complete, 285 and 1110 raw and clocked |
| Length-based, chosen on the Payloads tab | main 6–10 | **Passed:** 3 with the built-in and 2 with Swiss, complete, with the same counts as standard |
| The setting kept by 1.1.2 | main 11–12 | **Passed:** *Boot slot* to 1.1.2, a warm boot into slot A. Then a power-on with Swiss: complete, 1110/1110, by count, since 1.1.2 doesn't know the setting |
| The setting held on the way back | main 13–14 | **Passed:** *Boot slot* back to 1.2.0, kept, so no trial. The Payloads tab still said length-based, and the power-on with Swiss ran length-based: complete, 1110/1110 |
| Back to standard | both | **Passed:** `/api/status` says `"injection": "standard"` on both consoles at the end |
| Every power-on started its payload | all | **Passed:** gekkoboot's menu or Swiss every time; no console menu and no black screen (the owner) |
| The page | both | **Passed** (the owner): the Payloads tab's row and its button, "This boot" naming the method, and the boot log's "length" marks |

**The numbers:**

| | Dev console | Main console |
|---|---|---|
| Margin (clocked), 1.2.0 | 161.6–162.7 ms | 154.8–155.8 ms |
| Injection time | 19,421 µs with the built-in, 273,282 µs with Swiss | 19,420–19,421 µs, and 273,281–273,282 µs |
| `main()` to "PIO ready" | 598–607 µs | 602–611 µs, the length-based boots included |

All as on 1.1.2, and as L4 and L8 measured. The injection times are the count's own, whichever method ran.

**The logs:**
- **The dev console's log** still holds L8's length test build boots, 25–37, read by 1.2.0 with their method: "length". Its boots 38–39 are 1.1.2's, after L8.
- **The main console's log starts at boot 1,** a warm boot into 1.1.2 after L4's test build. That test build wrote its own log format (`0x8104`), which a release doesn't read, so 1.1.2 began a log of its own, as in release 1.1.2's test ([release-1.1.2-results.md](release-1.1.2-results.md)) [inf].

## What it shows

- **The waits before each block,** for the block's last clock and then CS high, ran on both consoles with the standard method, and on the main console with the length method. Every payload arrived whole. This is their first run on hardware.
- **Length-based in a release build** boots the main console. L8 showed it on the dev console, with the test build's program without R4's wait for the last clock.
- **The setting lives in boot control as designed:** 1.1.2 ignored it and kept it, and 1.2.0 found it again.

**Not shown, as planned:**
- **Noise:** both consoles have clean wiring, so neither the length method's benefit nor the waits' is tested here. The simulator covers them (`tests/test_length_injection.py`).
- **The dev console with length-based on 1.2.0:** the owner's lighter test (D48's plan), since the dev console had L8.
