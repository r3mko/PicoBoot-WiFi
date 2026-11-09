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
