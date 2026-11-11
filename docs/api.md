# PicoBoot-WiFi: JSON API

| | |
|---|---|
| **Status** | Approved with phase 2, 2026-11-09; the phase 3 additions with phase 3, 2026-11-10 |
| **Design** | [architecture.md](architecture.md) D18 (the API), D19 and D30 (security), D22 (features), D40 and D41 (settings) |

The web UI and the CLI tool use this API. Phase 2 defines status, settings, diagnostics and reboot; phase 3 adds the firmware upload and the slot choices; phase 4 the payloads, the payload and IPL choices, and backups.

## Common rules

- **Address:** `http://192.168.4.1/api/…`, over the access point. There is no other login: the Wi-Fi password is the gate (D19).
- **Responses** are JSON (`Content-Type: application/json`), never cached (`Cache-Control: no-store`), one per connection (`Connection: close`). The backups are files (below).
- **POST bodies** are JSON, sent with `Content-Type: application/json`; anything else gets 415.
  - **Why:** a page on another site can't send that content type to the Pico without a CORS preflight, which the Pico doesn't answer. So a web page a phone opened elsewhere can't change settings through it.
- **POSTs must be addressed to `Host: 192.168.4.1`** (or `192.168.4.1:80`); anything else gets 403.
  - **Why:** the Pico's DNS answers every name with its own address. A page opened elsewhere could otherwise point its own name at the Pico and send same-origin requests, which need no preflight (DNS rebinding).
- **Body size:** at most 1024 bytes for the endpoints here; more gets 413.
- **While an upload streams in,** other POSTs get 503 `busy`: a reboot or an access-point restart would cut it off.
- **One transfer of a file at a time:** while a backup downloads, uploads and payload deletes get 503 `busy`, so what it sends can't change under it; a backup asked for during an upload or another backup gets 503 `busy` too.
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
| `payloads` | The payloads (D15, D42), below |

`payloads`:

```json
"payloads": {
  "chosen": "payload1", "upload_target": "payload2", "original_ipl_once": false, "usb_install": false,
  "payload1": {"valid": true, "name": "Swiss r2119", "size": 1349956, "sha256": "9f2c…"},
  "payload2": {"valid": false, "name": "", "size": 0, "sha256": null}
}
```

| Field | Meaning |
|---|---|
| `chosen` | What the next power-on injects: `payload1`, `payload2` or `builtin` (the built-in gekkoboot, which also boots when the chosen payload isn't valid) |
| `upload_target` | Where the next upload goes: the payload slot that isn't the choice; payload 2 while the built-in is the choice (D42) |
| `original_ipl_once` | The next power-on boots the console's own IPL, then this clears itself |
| `usb_install` | This power-on booted payload 1 because a USB install had changed it (D17). Every power-on says so until a complete injection, or a choice, rename or upload over Wi-Fi, records the new payload 1. Not on an "original IPL once" boot, which boots nothing |
| `payloadN.valid` | It holds a payload with a good header, size and footer |
| `payloadN.name` | The name you gave it (D42); `""` for none, e.g. after a USB install |
| `payloadN.sha256` | Of the payload's own bytes; `null` while an upload runs and it isn't known yet |

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
| `result` | `not_run` (warm boot), `skipped` ("original IPL once"), `no_cs` (no console), `complete` or `stalled` |
| `main_to_ready_us` | `main()` to "PIO ready" |
| `margin_raw_us`, `margin_clocked_us` | "PIO ready" to the first CS edge, and to the first real transfer; −1 if none (D31, D36) |
| `injection_us` | From the first CS to the end of the injection |
| `cs_raw`, `cs_clocked` | CS edges and real transfers counted; more edges than transfers is a glitch the injection ignored (D36) |
| `cs_high_at_ready` | CS level at "PIO ready" |
| `cs_pulldown_test` | The boot ran in the CS pull-down test mode |
| `chip_stepping`, `rom` | As in the status |
| `payload_usb_install` | Payload 1 had changed since it was recorded, as a USB install changes it, so it booted (D17); on a `skipped` boot, it boots at the next power-on |
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

The boot choices: the firmware slots (D12, D13), the payload (D15) and the console's own IPL once (D22). The body holds exactly one of:

| Body | What it does | Answer |
|---|---|---|
| `{"keep": true}` | Keeps the running firmware, which is on trial: it becomes the active one (D12) | `{"kept": true}`; 409 `not_on_trial` if it isn't on trial |
| `{"slot": "A"}` or `"B"` | Boots that slot (D13) | `{"rebooting": true}`, then a warm reboot half a second later; `{"rebooting": false}` if it's the running slot already; 409 `not_bootable` if it's empty or holds no PicoBoot-WiFi firmware |
| `{"payload": "payload1"}`, `"payload2"` or `"builtin"` | The payload for the next power-ons (D15) | `{"payload": ...}`; 409 `not_valid` if that slot holds no valid payload |
| `{"original_ipl_once": true}` (or `false`) | The next power-on boots the console's own IPL, e.g. to set its clock or language; then it clears itself | `{"original_ipl_once": true}` |

The slot choices:

- **Back to the active slot from a firmware on trial:** the trial is abandoned. That slot stays unkept and is never a fallback.
- **To a kept slot:** it becomes the active one. A running firmware from a USB install counts as kept from then on.
- **To any other slot with a firmware** (from a USB install, on trial, unkept or failed): it goes on trial (D12). After a USB install the other slot may hold anything, so it's never made active straight away.

Payload choices take effect at the next power-on; nothing reboots.

## POST /api/payload

A payload upload (D15, D16, D42). The body is a payload UF2 (any family: the Pico takes one family's blocks in order from `0x10080000`) or a raw payload (`IPLBOOT ` … `PICO`), sent as `application/octet-stream`. A `.dol` is converted first, by the UI or the CLI. A raw payload may have padding after its own size, as a flash dump has: the UI and the CLI send only the payload's own bytes (the Pico would take the padding too, up to the slot's size, and payload 2's slot is 4 KB smaller than payload 1's). An optional header `X-Name` gives the payload's name: at most 24 printable ASCII characters; without it, or empty, the payload has no name. The UI and the CLI pre-fill it from the file name (D42): the name without its last extension, `_` as a space, printable ASCII only, spaces collapsed and trimmed, the first letter upper-case, at most 24 characters (`swiss_r2119.dol` → `Swiss r2119`).

- **It goes into** the slot that isn't the choice (`upload_target`), and becomes the choice once it's verified. The payload that boots now is never touched.
- **The answer:**

```json
{"installed": true, "slot": "payload2", "name": "Swiss r2119", "size": 1349956, "sha256": "9f2c…"}
```

- **Refusals** answer 400: `not_a_payload` (neither format, no payload blocks at `0x10080000`, e.g. the USB install file), `bad_uf2` (a broken block, or the payload's blocks not in order), `too_large` (more than the slot: 1.5 MB for payload 1, 1.5 MB − 4 KB for payload 2), `bad_payload` (the size or footer, or cut short), `bad_readback`, `incomplete`, `bad_value` (the name). Refusals within the first 4 KB of the payload (the header's among them) come before any flash is touched: the slot and its name stay as they were. Later ones leave the slot without a valid payload, and the choice unchanged. Also 500 `flash_write`, 503 `busy`, 415 as for the firmware upload.

## POST /api/payload/name and /api/payload/delete

| Endpoint | Body | Answer |
|---|---|---|
| `/api/payload/name` | `{"slot": "payload1", "name": "Swiss r2119"}` | `{"slot": ..., "name": ...}`; 400 `bad_value` for a name that isn't 0–24 printable ASCII characters |
| `/api/payload/delete` | `{"slot": "payload2"}` | `{"slot": ..., "deleted": true}`; 409 `chosen` if it's the choice (D42: choose another one first) |

## GET /api/backup/…

Backups (D22, D42): plain downloads, streamed from flash, with `Content-Disposition: attachment` and a file name. Each one can go back the way the Pico takes it.

| Path | What | File name | Restore with |
|---|---|---|---|
| `/api/backup/payload1`, `/payload2` | The payload as it sits in flash (`IPLBOOT ` … `PICO`), of the size the status gives. Its SHA-256 is the status's `sha256`. 409 `not_valid` if the slot holds no valid payload | Its name and `.bin`: `Swiss r2119.bin`, or `payload1.bin` without a name. Characters other than letters, digits and ` ._()+-` become `_` | `POST /api/payload` |
| `/api/backup/firmware` | The running firmware as an update file (`docs/update-format.md`): the image in its slot, the Wi-Fi blob it runs with, and a header the Pico makes as `mkupdate.py` does. The same bytes as the update file of that build | `picoboot-wifi-` and the version and `-update.bin`: `picoboot-wifi-1.0.0-update.bin` | `POST /api/firmware`: into the other slot, on trial |
| `/api/backup/settings` | The settings as `POST /api/settings` takes them, without the password (D41): `{"ssid":"","country":"XX","channel":6,"tx_power":"medium","auto_off_minutes":10,"led":true}` | `picoboot-wifi-settings.json` | `POST /api/settings` with the file as its body |

Only the running firmware is offered: the image size of the other slot's isn't recorded. A POST to these paths gets 405.

## POST /api/reboot

Body `{}`. The answer is `{"rebooting": true}`, then a warm reboot half a second later. A warm reboot doesn't inject (D8): a running game keeps running.
