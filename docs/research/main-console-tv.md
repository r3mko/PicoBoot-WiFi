# The second console at its TV

Why the owner's second DOL-001 booted only now and then once it was back at its TV after release 1.1.1, what didn't help, and what did. 2026-11-20 and 21. The firmware is release 1.1.1 throughout, and nothing in it changes.

**Setup:**
- **The console:** the second console from [cs-pull-up.md](cs-pull-up.md), the owner's main DOL-001, with a GC2SD. Its board is DOL-CPU(P)-01, the first PAL revision.
- **At the TV:** its own GCVideo HDMI adapter in the digital AV port. Release 1.1.1's console test at the bench used the dev console's adapter, from the same vendor.
- **The Pico, at first:** `PicoBoot-AE52`, on loose wires to U10 with the short ground wire, as in release 1.1.1's test.
- **The payloads:**
  - the built-in gekkoboot, 60 transfers, so 285 CS transfers on a clean boot;
  - Swiss r2174 in payload 2, 885 transfers, so 1110;
  - later, cubiboot's `ipl.dol` with D43's stub in payload 1, 717 transfers, so 942.

Boot numbers are each Pico's own boot log, and the two Picos' numbers overlap. The owner pasted the logs into the session; this note keeps their numbers. **[inf]** marks a conclusion rather than a measured fact.

## 1. The symptom

| Where | Pico, boots | Payload | CS transfers (raw = clocked) |
|---|---|---|---|
| Bench, release 1.1.1's test | `AE52`, 32–52 | Swiss r2119, the built-in | exact on 21 of 21 ([release-1.1.1-results.md](release-1.1.1-results.md)) |
| TV | `AE52`, 81 and 90–101 | the built-in (285) | 285–292, once 362; exact only on boot 95 |
| TV | `AE52`, 82–89 | Swiss r2174 (1110) | 1119–1135, once 1251 |

- **Extra transfers on 20 of 21 boots at the TV,** all clocked. So CS crossed its threshold during real transfers, while the clock ran.
- **The first CS edge came on time** (157.9–159.8 ms after "PIO ready"), so this wasn't a start-up glitch.
- **The outcome:** boot 95, the exact one, started its payload, and "a couple" of the 21 did (the owner).
- **Why only sometimes [inf]:** an extra transfer among the 224 skipped ones sends the payload early, and the console starts its own menu.
- **Where the noise came from [inf]:** suppose each extra transfer falls at a random point of the read. The chance that none falls in the 224 skipped transfers then predicts about 2 starts in these 21 boots, which matches.
  - The skipped transfers are 79% of the built-in's read and 20% of Swiss's.
  - If the extras came only during the payload transfers, which is when the Pico drives DATA (the long ground wire's case, [cs-pull-up.md](cs-pull-up.md) §1), all 21 would have started.
  - So the noise was also there while the Pico drove nothing: it came from outside the Pico.

## 2. What didn't help

The owner tried each of these at the TV, one at a time, and none fixed it (no logs kept):
- the HDMI cable unplugged from the TV, the adapter still in the console;
- the bench adapter in place of the TV's;
- the bench power brick.

## 3. The fix

The owner replaced the Pico and all the wires, and fitted a CubeSB board at U10.
- **The CubeSB board:** a quick-solder board for the DOL-001 (PicoBoot-DX's `CubeSB_DOL-001` folder, after KunaiGC's QSB design). Its connector brings out U10's +3.3V, GND, `EXI.CLK`, `EXI.~{CS}`, `EXI.OUT` and `EXI.IN`, so the Pico's ground is taken at U10. `EXI.IN` stays unconnected.
- **Three changes at once,** so which of them mattered isn't known.

The results at the TV, with the new Pico (an RP2350 A2 too):

| Boots | Payload | Result |
|---|---|---|
| 76–96 | the built-in (payload 1 still empty) | 21 of 21 exact: 285 |
| 105, 106, 112–114 | Swiss r2174 | 5 of 5 exact: 1110, which confirms r2174's clean count |
| 107, 109–111 | the built-in | exact: 285 |
| 115, 116 | cubiboot | exact: 942 |
| 101–104 | cubiboot | stalled (§4) |
| 108 | the built-in | complete, but 5368 transfers (§4) |

- **Exact on 32 of 33 complete boots,** with raw equal to clocked.
- **Margins of 155.3–156.7 ms,** about 3 ms below the old Pico's 157.9–160.1 ms in this console [inf: the new Pico starts a little later after power-on]. The lowest seen before was 154.7 ms, once, on the dev console in phase 3.
- **CS reads high at "PIO ready" on every power-on,** as with the old Pico ([cs-at-start-up.md](cs-at-start-up.md)).
- **Between boots 96 and 101,** the owner put the firmware into slot B and switched back to slot A, twice (boots 97–100, warm restarts). The log shows each update on trial in B and each switch back.

## 4. Left over

- **Boots 101–104 stalled:** quick power cycles, and a black screen (the owner).
  - The console stopped two transfers after the 224 skipped ones.
  - The Pico delivered 9 words, which only fill its PIO FIFO and OSR, so fewer than 32 bits of the payload went out.
  - [inf] The console's own start went wrong on a very quick power cycle, before the payload could matter: cubiboot booted exactly on 115 and 116.
  - Not tested yet: equally quick cycles with another payload, or with "original IPL once".
- **Boot 108:** the built-in, complete, but 5368 transfers over 1.36 s, against 285 in 19.4 ms normally.
  - It looks like the console restarting its reads in the middle of the injection [inf].
  - Not a bounce of the switch or a reset, as far as the owner knows. One boot, not explained.
- **"main() to PIO ready"** is 320–327 µs while payload 1 is empty, and 593–607 µs once it holds cubiboot. That's payload 1's fingerprint before the injection (`main.c`, D17), not the hardware. It explains the old Pico's 591–604 µs at the TV against 382–396 µs at the bench.

## 5. Conclusions

1. **At its TV, the loose wires to U10 picked up enough noise to add clocked CS transfers on nearly every boot,** though the same wires were clean at the bench.
   - A CubeSB board, a new Pico and new wires removed it.
   - Which of the three mattered isn't known.
2. **The noise came from outside the Pico [inf, §1],** but not from the HDMI link, the adapter or the power brick alone (§2). What at the TV made the difference isn't known.
3. **The boot log's CS counts showed the problem every time,** and showed when it was gone: exact counts are clean, and extras point at the wiring. That is the README's check.
4. **For the README, the owner's call:** a quick-solder board at U10 as the better way to wire the Pico.
5. **Quick power cycles:** give the console a few seconds off before switching it on again (§4) [inf].
