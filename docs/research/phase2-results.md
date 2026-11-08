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
