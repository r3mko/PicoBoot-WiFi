# PicoBoot-WiFi: JSON API

| | |
|---|---|
| **Status** | Approved with phase 2, 2026-11-09; the phase 3 additions with phase 3, 2026-11-10 |
| **Design** | [architecture.md](architecture.md) D18 (the API), D19 and D30 (security), D22 (features), D40 and D41 (settings) |

The web UI and the CLI tool use this API. Phase 2 defines status, settings, diagnostics and reboot; phase 3 adds the firmware upload and the slot choices. The other endpoints of D18 come with phase 4: payloads, the payload and IPL choices, and backups.

## Common rules

- **Address:** `http://192.168.4.1/api/…`, over the access point. There is no other login: the Wi-Fi password is the gate (D19).
- **Responses** are JSON (`Content-Type: application/json`), never cached (`Cache-Control: no-store`), one per connection (`Connection: close`).
- **POST bodies** are JSON, sent with `Content-Type: application/json`; anything else gets 415.
  - **Why:** a page on another site can't send that content type to the Pico without a CORS preflight, which the Pico doesn't answer. So a web page a phone opened elsewhere can't change settings through it.
- **POSTs must be addressed to `Host: 192.168.4.1`** (or `192.168.4.1:80`); anything else gets 403.
  - **Why:** the Pico's DNS answers every name with its own address. A page opened elsewhere could otherwise point its own name at the Pico and send same-origin requests, which need no preflight (DNS rebinding).
- **Body size:** at most 1024 bytes for the endpoints here; more gets 413.
- **While an upload streams in,** other POSTs get 503 `busy`: a reboot or an access-point restart would cut it off.
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
  "firmware": {"version": "0.3.0-dev+1a2b3c4", "slot": "A", "loader": 2, "pending": null,
               "states": {"A": "kept", "B": "failed"},
               "versions": {"A": "0.3.0-dev+1a2b3c4", "B": "0.3.0-dev+9f8e7d6-crash-5s"}},
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
| `firmware.loader` | The installed loader's version (1 for loaders that don't report one); an update needing a newer one is refused (D11) |
| `firmware.pending` | The slot with a new firmware on trial (D12), or `null`. When it equals `firmware.slot`, the running firmware is the one on trial |
| `firmware.states` | Per slot: `installed` (from a USB install), `empty` (being written, or an upload failed), `trial`, `kept`, `failed` |
| `firmware.versions` | Per slot: the firmware's version, read from its image; `null` if the slot is empty or holds no PicoBoot-WiFi image |
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
| `loader` | The loader's version |
| `trial` | The loader started this slot on trial (D12) |
| `trial_failed` | The other slot's trial failed in this session: it crashed, or held no image |
| `crash_fallback` | The other slot crashed 3 times in a row in this session, so this one took over for good |

`wifi` says where the Wi-Fi blob came from (`"A"` or `"B"`), and how long Wi-Fi took to come up after the injection.

## POST /api/firmware

An over-the-air update (D11). The body is an update file ([update-format.md](update-format.md)), sent with `Content-Type: application/octet-stream`; the Host rule applies as for every POST. The 1024-byte body limit doesn't apply.

- **The Pico** streams it into the slot that isn't running, erasing and writing flash as it goes, then reads it back. An upload takes a few seconds (4 s on the bench in phase 3), most of it erasing flash.
- **The answer:**

```json
{"installed": true, "slot": "B", "version": "0.3.0-dev+1a2b3c4", "blob_written": false, "rebooting": true}
```

  - `slot` now holds the new firmware, on trial (D12); `blob_written` says whether its Wi-Fi area needed the file's blob (D14).
  - Half a second later the Pico reboots into the trial (D11). The reboot is warm: a running game keeps running (D8). The UI waits for the Pico to come back.
- **Refusals** answer 400 with the codes of [update-format.md](update-format.md): `not_an_update`, `unsupported_format`, `bad_header`, `wrong_board`, `needs_usb_install`, `bad_size`, `needs_blob`, `bad_image`, `bad_blob`, `incomplete`. A refusal that comes from the header arrives before any flash is touched; the rest leave the other slot empty, never half a firmware.
- **When the answer comes:** a refusal from the file itself (the header or a hash) comes after the whole body, which the Pico reads and drops. One from the request alone (the type, the Host, `busy`, `on_trial`, `needs_usb_install` without a loader) comes at once, and the Pico may close the connection while the client is still sending: a client should then still read the answer.
- **Also:**
  - 409 `on_trial` while the running firmware is itself on trial: the other slot holds the one to fall back to, so it isn't overwritten. Keep the new firmware first, or switch back to the other slot (a reboot alone continues the trial);
  - 409 `not_active` while the running firmware isn't the active one (it started in place of it, or a switch is under way): the update would go into the active slot without a trial;
  - 503 `busy` while the Pico is about to reboot;
  - 415 without the octet-stream type, 503 `busy` while another upload runs, 500 `flash_write` if boot control can't be written.

## POST /api/boot

The firmware page's choices (D12, D13). The body holds exactly one of:

| Body | What it does | Answer |
|---|---|---|
| `{"keep": true}` | Keeps the running firmware, which is on trial: it becomes the active one (D12) | `{"kept": true}`; 409 `not_on_trial` if it isn't on trial |
| `{"slot": "A"}` or `"B"` | Boots that slot (D13) | `{"rebooting": true}`, then a warm reboot half a second later; `{"rebooting": false}` if it's the running slot already; 409 `not_bootable` if it's empty or holds no PicoBoot-WiFi firmware |

- **Back to the active slot from a firmware on trial:** the trial is abandoned. That slot stays unkept and is never a fallback.
- **To a kept slot:** it becomes the active one. A running firmware from a USB install counts as kept from then on.
- **To any other slot with a firmware** (from a USB install, on trial, unkept or failed): it goes on trial (D12). After a USB install the other slot may hold anything, so it's never made active straight away.
- Phase 4 adds the payload and "original IPL once" choices to this endpoint.

## POST /api/reboot

Body `{}`. The answer is `{"rebooting": true}`, then a warm reboot half a second later. A warm reboot doesn't inject (D8): a running game keeps running.
