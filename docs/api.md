# PicoBoot-WiFi: JSON API

| | |
|---|---|
| **Status** | Draft for phase 2 (plan step 2.3); approved with the phase |
| **Design** | [architecture.md](architecture.md) D18 (the API), D19 and D30 (security), D22 (features), D40 and D41 (settings) |

The web UI and the CLI tool use this API. Phase 2 defines status, settings, diagnostics and reboot. The other endpoints of D18 come with their phases: firmware upload (phase 3), and payloads, boot choices and backups (phase 4).

## Common rules

- **Address:** `http://192.168.4.1/api/…`, over the access point. There is no other login: the Wi-Fi password is the gate (D19).
- **Responses** are JSON (`Content-Type: application/json`), never cached (`Cache-Control: no-store`), one per connection (`Connection: close`).
- **POST bodies** are JSON, sent with `Content-Type: application/json`; anything else gets 415.
  - **Why:** a page on another site can't send that content type to the Pico without a CORS preflight, which the Pico doesn't answer. So a web page a phone opened elsewhere can't change settings through it.
- **POSTs must be addressed to `Host: 192.168.4.1`** (or `192.168.4.1:80`); anything else gets 403.
  - **Why:** the Pico's DNS answers every name with its own address. A page opened elsewhere could otherwise point its own name at the Pico and send same-origin requests, which need no preflight (DNS rebinding).
- **Body size:** at most 1024 bytes for the endpoints here; more gets 413.
- **Compatibility:**
  - Later phases add fields; clients ignore fields they don't know.
  - `api` in the status counts up only when a field changes meaning or goes away.
- **Unknown paths:**
  - under `/api/`, 404;
  - elsewhere, a 302 to the start page (the captive portal, D18).

**Errors** have a status code and a body:

```json
{"error": "bad_value", "message": "channel must be 1 to 11", "field": "channel"}
```

| Status | `error` | When |
|---|---|---|
| 400 | `bad_json` | The body isn't a JSON object of the expected shape |
| 400 | `unknown_field` | A field this endpoint doesn't have (`field` names it) |
| 400 | `bad_value` | A value of the wrong type or outside the allowed values (`field` names it) |
| 403 | `forbidden` | A POST not addressed to `Host: 192.168.4.1` |
| 404 | `not_found` | No such endpoint |
| 405 | `method_not_allowed` | GET on a POST endpoint or the other way round |
| 413 | `too_large` | The body is over the limit |
| 415 | `unsupported_type` | A POST without `Content-Type: application/json` |
| 500 | `flash_write` | Writing flash failed; nothing changed |
| 500 | `internal` | The answer didn't fit its buffer (a firmware bug) |
| 503 | `busy` | Too many requests at once, or out of memory; try again |

## GET /api/status

What the status page shows (D22).

```json
{
  "api": 1,
  "firmware": {"version": "0.2.0-dev+a75e9b7", "slot": "A"},
  "chip": {"stepping": 2, "rom": 2},
  "uptime_s": 34,
  "temperature_c": 38.4,
  "wifi": {
    "ssid": "PicoBoot-C2C4",
    "clients": 1,
    "password_is_default": true,
    "reverted": false
  }
}
```

| Field | Meaning |
|---|---|
| `firmware.version` | Semver plus the git hash for test builds (D24) |
| `firmware.slot` | `"A"` or `"B"`: the slot this firmware runs from |
| `chip.stepping` | RP2350 stepping: 2 is A2, 3 is A3, 4 is A4 |
| `chip.rom` | Boot ROM version |
| `temperature_c` | RP2350 sensor; it reads low on a low 3.3 V rail |
| `wifi.ssid` | The SSID in use: the setting, or `PicoBoot-XXXX` (D30) |
| `wifi.clients` | Clients joined to the access point |
| `wifi.password_is_default` | Still `picoboot`: the UI shows the D19 banner |
| `wifi.reverted` | At this start, the firmware undid a Wi-Fi change that no client confirmed (D41) |

## GET /api/settings

```json
{
  "ssid": "",
  "ssid_in_use": "PicoBoot-C2C4",
  "password_is_default": true,
  "country": "XX",
  "channel": 6,
  "tx_power": "medium",
  "auto_off_minutes": 10,
  "led": true,
  "choices": {
    "countries": ["XX", "AU", "AT", "…"],
    "channels": [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11],
    "tx_power": ["low", "medium", "high", "max"],
    "auto_off_minutes": [2, 5, 10, 30, 0]
  }
}
```

- The values and defaults are those of D40.
- `ssid` is `""` when the default `PicoBoot-XXXX` is in use.
- The password itself is never returned (D41).
- `auto_off_minutes` 0 means Wi-Fi stays on.
- `choices` lists the allowed values, so the UI doesn't keep its own copy. `countries` is in the firmware's order, worldwide (`XX`) first.

## POST /api/settings

The body holds only the settings to change:

```json
{"password": "a new password", "channel": 11}
```

| Field | Type | Allowed |
|---|---|---|
| `ssid` | string | 0–32 printable ASCII characters; `""` for the default |
| `password` | string | 8–63 printable ASCII characters |
| `country` | string | One of `choices.countries` |
| `channel` | number | 1–11 |
| `tx_power` | string | `low`, `medium`, `high`, `max` |
| `auto_off_minutes` | number | 2, 5, 10, 30, or 0 (always on) |
| `led` | boolean | |

**The answer:**

```json
{"saved": true, "wifi_restart": true, "ssid_in_use": "PicoBoot-C2C4"}
```

- **All or nothing:** one wrong field and nothing is saved.
- **`ssid_in_use`:** the SSID to join after the restart (the default name when `ssid` was set to `""`).
- **`wifi_restart: true`:** the SSID, password, channel or country changed.
  - About a second after this answer, the access point restarts with them, and every client drops off (D41).
  - The change is on trial until a client reaches the web server through the restarted access point; phones' captive-portal probes do that by themselves. If the firmware starts again first, the previous Wi-Fi settings return.
- **The other settings** apply at once, without a restart.

## GET /api/diagnostics

The last boot and the boot log (D22, D39).

```json
{
  "boot": {"boot": 111, "power_on": false, "reset": "POR", "result": "not_run", "…": "…"},
  "wifi": {"blob": "A", "up_ms": 784, "cyw43_init_ms": 0, "ap_start_ms": 775},
  "log": [
    {"boot": 111, "power_on": false, "reset": "POR", "result": "not_run", "…": "…"},
    {"boot": 110, "power_on": true, "reset": "POR", "result": "no_cs", "…": "…"}
  ]
}
```

`boot` is this boot's report; `log` holds up to the last 32, newest first, the first being this boot. A report has these fields:

| Field | Meaning |
|---|---|
| `boot` | Boot number from the log (its sequence number); 0 if this boot couldn't be logged |
| `power_on` | The D8 decision: a power-on (inject) or a warm boot |
| `reset` | `POR`, `BOR`, `RUN`, `WDOG` or `other`, from `chip_reset` |
| `chip_reset` | POWMAN `CHIP_RESET` as read at start-up |
| `from_loader`, `slot`, `fallback`, `bootctl_default` | The loader's decision: started by the loader, from slot `"A"` or `"B"`, because the first choice held no image, with boot control empty |
| `payload` | `none`, `payload1`, `payload2` or `builtin` |
| `payload_chosen` | Boot control's choice, in the same names; `none` without an injection. If it differs from `payload`, the chosen payload wasn't valid and the built-in one went instead |
| `payload_words`, `words_delivered` | Words to send (256 per 1K transfer) and words sent |
| `result` | `not_run` (warm boot), `no_cs` (no console), `complete` or `stalled` |
| `main_to_ready_us` | `main()` to "PIO ready" |
| `margin_raw_us`, `margin_clocked_us` | "PIO ready" to the first CS edge, and to the first real transfer; −1 if none (D31, D36) |
| `injection_us` | From the first CS to the end of the injection |
| `cs_raw`, `cs_clocked` | CS edges and real transfers counted; more edges than transfers is a glitch the injection ignored (D36) |
| `cs_high_at_ready` | CS level at "PIO ready" |
| `cs_pulldown_test` | The boot ran in the CS pull-down test mode |
| `chip_stepping`, `rom` | As in the status |

`wifi` says where the Wi-Fi blob came from (`"A"` or `"B"`), and how long Wi-Fi took to come up after the injection.

## POST /api/reboot

Body `{}`. The answer is `{"rebooting": true}`, then a warm reboot half a second later. A warm reboot doesn't inject (D8): a running game keeps running.
