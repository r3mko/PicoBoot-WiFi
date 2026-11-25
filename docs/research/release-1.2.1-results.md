# Release 1.2.1 results

The console test of release 1.2.1 (the plan's step R4), run by the owner on 2026-11-25 on the dev console, on clean wiring. The main console isn't in the test, the owner's choice. **[inf]** marks a conclusion rather than a measured or sourced fact.

**Status:** R4 passed. Approved by the owner with plan v1.38.

**Setup:**
- **The dev console:** the owner's DOL-001 with the quick-solder flex cable ([main-console-tv.md](main-console-tv.md) §6). Before the test: 1.2.0 in slot B, the active one, and 1.1.2 in slot A.
- **The payloads:** Swiss r2174 in payload 2 (885 transfers, so 1110 CS transfers expected), and the built-in gekkoboot (60 transfers, so 285).

Boot numbers are the console's boot log, saved after the boots (`/api/diagnostics`).

**Build:** `1.2.1` from `9f90fa1`, the build after the code review (R3):

| File | SHA-256 |
|---|---|
| `picoboot-wifi-1.2.1-update.bin` | `98093f6d906646c36013ac34ad3a8c511aee608aa35e9a0f74117efa3bc263b6` |
| `picoboot-wifi-1.2.1.uf2` | `77f96cd43ff67350a651a4a111fa127b38cc0975593a5cf0335b1c47140a08e7`, from the same build. R4 didn't install it |

## The checks

| Check | Boots | Result |
|---|---|---|
| The update over Wi-Fi | 44–46 | **Passed,** from 1.1.2 rather than 1.2.0. *Boot slot* to 1.1.2 in slot A, a warm boot (44). The update then went into slot B, replacing 1.2.0, and its restart came up on trial (45). The next power-on, with the built-in, kept it (46) |
| Standard, with the moved waits | 46–47 | **Passed:** the built-in and Swiss, complete, 285 and 1110 raw and clocked |
| Length-based, with the moved waits and the detector's restart | 48–49 | **Passed:** the built-in and Swiss, complete, with the same counts as standard |
| Back to standard | — | **Passed:** `/api/status` says `"injection": "standard"` at the end |
| Every power-on started its payload | 46–49 | **Passed:** gekkoboot's menu or Swiss every time (the owner) |

**The numbers:**

| | 1.2.1 |
|---|---|
| Margin (clocked) | 159.8–161.8 ms, as on 1.2.0 (161.6–162.7 ms in its R5) |
| Injection time | 19,421 µs with the built-in, 273,283 µs with Swiss, both methods: 1.2.0's |
| `main()` to "PIO ready" | 601–608 µs |

**Boot 43,** on 1.2.0 before the update:
- **The edge:** it shows the dev console's known false CS edge at start-up (D36), 41.8 ms after "PIO ready".
- **The count:** 286 raw against 285 clocked, so the clock-qualified count ignored the edge, and the boot completed and started its payload (the owner).
- **The injection time:** 136 ms, because it's measured from that first raw edge.

## What it shows

- **The moved waits for a block's last clock run on hardware with both methods,** now also at the payload's end, together with the wait for the next transfer's CS fall. So does the detector's restart. Every payload arrived whole.
- **Clean wiring can't show more:** the noise cases these changes are for are the simulator's ([block-noise.md](block-noise.md)).

**Not shown, as planned:**
- **The main console:** the owner's choice. It stays on 1.2.0 until it takes the release.
- **The update from 1.2.0 itself:** it came from 1.1.2 here. 1.2.0's R5 already showed the update from 1.1.2, and the update path doesn't change in 1.2.1 [inf].
