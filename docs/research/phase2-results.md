# Phase 2 results

Hardware tests from implementation plan phase 2, run by the owner on the `phase-2-wifi` builds.

**Setup:** the Pico 2 W from phases 0 and 1 (SSID suffix C2C4, RP2350 A2).

## Bench check of steps 2.1 and 2.2 (2026-11-08, USB power, no console)

Build `a75e9b7`: settings in flash (D40), lwIP httpd with captive-portal redirects (D18), and test buttons for the channel.

| Check | Result |
|---|---|
| Settings after the install | Defaults: channel 6, country XX, Tx power medium, Wi-Fi off after 10 min, LED on |
| Channel 11 survives a watchdog reboot (boot 109) | **Passed** |
| ...and a power cycle (boot 110) | **Passed** |
| Back to channel 6 (boot 111) | **Passed**; the page shows a stored record, not the defaults |
| iPhone: the page opens by itself after joining | **Passed:** the captive-portal pop-up shows the page |
| Android: "Sign in to network" opens the page | **Passed** (with build `9312572`) |

## Bench check of step 2.3 (2026-11-08, USB power, no console)

Build `9312572`: the JSON API (`docs/api.md`) and the D41 settings trial, tried with `curl` from a Mac joined to the access point.

| Check | Result |
|---|---|
| `GET /api/status`, `/api/settings`, `/api/diagnostics` | **Passed:** answers as specified; diagnostics shows boot 112 (USB, "no_cs") |
| POST without `Content-Type: application/json` | **Passed:** 415 `unsupported_type` |
| `{"channel":12}` / `{"colour":1}` | **Passed:** 400 `bad_value` and 400 `unknown_field`, each naming the field |
| `GET /api/nothing` | **Passed:** 404 `not_found` |
| LED and auto-off change | **Passed:** saved, no access-point restart |
| Channel 11 | **Passed:** saved with a restart; the Mac rejoined by itself |
| Channel 11 kept after the restart | **Passed:** the Mac's rejoin confirmed it; it stayed through the undo of the password change and the reboot |
| Back to channel 6 | **Passed** |
| Password `wrongpass1`, no client joins, power cycle | **Passed:** the default password works again, and the status says `"reverted":true` |
| `POST /api/reboot` | **Passed:** `{"rebooting":true}` |

**Steps 2.1–2.3 done:** settings survive reboots and power cycles; iPhone and Android open the page by themselves; the API answers as specified; a Wi-Fi change is kept once a client comes back and undone if none does.

## Bench check of step 2.4 (2026-11-09, USB power, no console)

Build `8616bc7`: the web UI (one page with Status, Diagnostics and Settings tabs, 6.4 KB gzipped), the test page at `/test`.

| Check | Result |
|---|---|
| iPhone: the captive-portal pop-up shows the new UI; all three tabs work | **Passed** |
| Android: the same through "Sign in to network" | **Passed** |
| Password change in the UI: restart notice, rejoin with the new password, back to `picoboot` | **Passed** |
| Reboot from the UI (inline confirm) | **Passed** |
| Test page at `/test` | **Passed** |
| Readable in the phone's dark mode | **Passed** |

**Owner's finding:** the Diagnostics row "main() to PIO ready" isn't user friendly. Renamed "Start-up time" ("from the firmware's start until it can answer the console"); the Margin note no longer says "PIO ready" either.

## Bench check of steps 2.5 and 2.6 (2026-11-09, USB power, no console)

Build `bff4a6c`: why a payload was used (D22), auto-off (D20), Tx power levels (D40), LED patterns (D26). The boot log format changed, so the log started again at boot 1.

| Check | Result |
|---|---|
| LED steady on once Wi-Fi is up (no console, no injection) | **Passed** |
| LED setting off and on: applies at once | **Passed** |
| Payload 2 chosen but empty: diagnostics say it isn't valid and the built-in one went | **Passed** |
| Auto-off set to 2 minutes: Wi-Fi stays on while a client is joined | **Passed** |
| ...and goes off after the last client left | **Passed**, but after about 3.5 minutes; see below |

**Tx power** (the Mac 1–2 m from the Pico, RSSI from the Wi-Fi menu):

| Level | The chip reports | RSSI | Step |
|---|---|---|---|
| Max | 31.75 dBm | −38 dBm | |
| High | 15.00 dBm | −42 dBm | 4 dB |
| Medium (default) | 10.00 dBm | −47 dBm | 5 dB |
| Low | 5.00 dBm | −52 dBm | 5 dB |

- The levels change the signal as intended: 5 dB per 5 dB step.
- For max the chip just echoes the request (127 quarter-dBm); its own limit applies, about 4 dB above high.

**Auto-off timing:** 3.5 minutes from the moment the last device switched its Wi-Fi off, for the 2-minute setting.
- **Cause:** the countdown starts when the CYW43 drops the last client from its list. A device that switches Wi-Fi off doesn't always say goodbye, so the chip keeps it until its own inactivity timeout, about 1.5 minutes here.
- **The owner accepted this:** auto-off saves power and quiets the radio; it doesn't need to be exact. Noted in D20.

## 2.7 Console tests

**Setup:** the DOL-001 with SD2SP2 and HDMI adapter, the Pico fed through 3V3 as in phase 1; build `bff4a6c`, Swiss r2119 in payload 1.

### Part A: injection (2026-11-09)

| | Phase 1 | Phase 2 |
|---|---|---|
| Swiss r2119, cold boots | 10/10 (step 1.12), margin 159.8–161.7 ms | **5/5** (boots 5–9), margin 159.6–160.6 ms; CS 1544/1544 |
| gekkoboot (built-in), cold boots | 10/10 (step 1.10), margin 159.7–163.0 ms | **5/5** (boots 10–14), margin 159.8–160.6 ms; CS 285/285 |
| CS at "PIO ready" | high | high on every boot |
| Injection time (gekkoboot) | 19 ms | 19 ms |
| `main()` to "PIO ready" | 301–347 µs | 329 µs (boot 14) |

**Finding:** the injection is unchanged by phase 2. The owner did 5 Swiss boots rather than 10 and accepted that, since the gekkoboot series ran the same path.

### Part B: phones while a game runs (2026-11-09)

iPhone and Android joined with Swiss on screen; the UI opened by itself on both, and the tabs and repeated Diagnostics reloads worked. **No disturbance of the picture or sound** (risk R5), as in phase 0's stress test.

### Part C: settings on the console (2026-11-09)

The password change in the UI worked with the console running: the restart notice, the rejoin with the new password, the default-password banner gone, and back to `picoboot`. **Passed.**

### Parts D, E and F: range, rail voltage and auto-off (2026-11-09)

| Tx power | RSSI at the owner's usual distance (Mac) | 3V3 at the Pico |
|---|---|---|
| Max | −40 dBm | 3.14 V |
| Medium (default) | −45 dBm | 3.14 V |
| Low | −58 dBm | 3.14 V |
| Wi-Fi off (auto-off) | | 3.21 V |

**Findings:**
- **Range:** even Low gives −58 dBm at the usual distance, a strong signal, and the UI loads without delay; the default Medium leaves a wide margin.
- **Rail:** with the radio on, 3V3 reads 3.14 V at every Tx power level; the meter averages, and while idle the radio sends little. With Wi-Fi off it is 3.21 V. Both match phase 0 (3.15 V on, 3.21 V off), so the D38 advice (5 V wiring) stands, and auto-off gives the rail back its 0.07 V.
- **Auto-off at 10 minutes:** Wi-Fi went off after about **10.5 minutes** without a client, within the 10–11.5 minutes the bench check predicted (D20).

### Part G: warm reboot during a game (2026-11-09)

Reboot from the UI while a game ran: **the game kept running**, and Wi-Fi came back. Design D8 holds with the phase 2 firmware.

### Step 2.7 summary

| Part | Result |
|---|---|
| A: injection, 5 Swiss and 5 gekkoboot cold boots | 10/10; margin 159.6–160.6 ms; unchanged from phase 1 |
| B: phones while a game runs | UI opens by itself on iPhone and Android; no picture or sound disturbance |
| C: settings on the console | Password change, rejoin and banner work |
| D: range | −40 to −58 dBm at the usual distance from Max to Low; the UI works at Low |
| E: rail voltage | 3.14 V with the radio on at every level, 3.21 V with Wi-Fi off |
| F: auto-off at 10 minutes | Off after about 10.5 minutes without a client |
| G: warm reboot during a game | The game kept running |

**Done when (plan 2.7): results recorded.** Met. The phase's goal (D28), a phone reaching the status page from a closed console, is met.

## 2.8 Code review and re-test (2026-11-09)

**Code review** (a fresh Opus 5.5 reviewer): no high defects; one medium (DNS rebinding past the CSRF guard) and seven low findings, all fixed. They're listed in the implementation plan (phase 2, "Code review of phase 2").

**Re-test before the merge** (the owner's choice), build `90babff`, USB power:

| Check | Result |
|---|---|
| Both phones open the UI by themselves; a save from the UI goes through the new Host check | **Passed** |
| POST with `Host: evil.example` | **Passed:** 403 `{"error":"forbidden",...}`; the same POST to 192.168.4.1 is saved |
| Country change to Netherlands: the access point restarts, a phone rejoins, Settings shows NL; back to Worldwide | **Passed** (the review's one untested case) |
| Test page buttons (payload choice) | **Passed** |

**Step 2.8:** review fixes in, re-test passed.
