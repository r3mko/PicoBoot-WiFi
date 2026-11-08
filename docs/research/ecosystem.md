# Ecosystem: payloads, upstream, OTA and web building blocks

Research done on 2026-11-08 for PicoBoot-WiFi (Pico 2 W only). **[inf]** marks a conclusion rather than a quoted fact.

## 1. Upstream and related projects

- **webhdx/PicoBoot:**
  - Latest release is v0.5.0 (2025-05-18), with Pico W, Pico 2 and Pico 2 W support; last commit 2025-11-09.
  - The payload format is the one PicoBoot-DX uses: `IPLBOOT ` + size header, scrambled image, `PICO` footer, at `0x10080000` with a 1.5 MiB area.
  - Wi-Fi exists only as open issues. #65 asks for a web status/config page, with authentication and showing the upload's SHA. On 2025-10-28 webhdx wrote: "WiFi and BT will be definitely utilized".
- **Forks:**
  - **NeoPicoBoot:** Pico W access point with a captive portal inside the console; open SSID `GC_RGB`, fed from 5 V through VSYS. It configures a video mod; no firmware OTA.
  - **PicoBootW** (maxbit89): says "with wifi", but nothing in it shows that.
  - **PicoBoot-BT** (HandHeldLegend): Bluetooth.
- **Worth learning from:**
  - **PicoLoader** (makeo): converts payloads in the browser.
  - **MemCard PRO GC:**
    - sets up through its own access point, then joins home Wi-Fi;
    - web UI, FTP and Wi-Fi updates;
    - a move of its update server stranded users, who had to update from SD;
    - turning Wi-Fi off resets all its settings.
  - **gbs-control:** access point by default, falls back to it if joining home Wi-Fi fails, reachable by `.local` name.
  - **arduino-pico HTTPUpdateServer:** browser OTA that survives power loss, accepts gzip, optional signing.

## 2. Payload sizes

Read from the release UF2 files (`IPLBOOT` size field, checked with our own UF2 parser):

| Payload | Size | UF2 families | Address range |
|---|---|---|---|
| Swiss r2119 (2026-09-25), `PicoBoot/swiss_r2119.uf2` | **1,349,956 B (1.29 MiB)** | rp2040 + rp2350-arm-s | `0x10080000`–`0x101C9A00` |
| Swiss r2092 | 790,660 B | rp2040 + rp2350-arm-s | `0x10080000`–`0x10141100` |
| Swiss r2018 | 777,476 B | | |
| gekkoboot r9.4, `gekkoboot_pico.uf2` | 60,548 B | rp2040 only | `0x10080000`–`0x1008ED00` |

- **Growth:** Swiss grew 71% in one release [inf: commit 163f8b1d adds `iconv`].
- **Largest IPL the console reads:** Dolphin copies BS2 from ROM 0x820, up to 0x1AFE00 bytes (≈1.69 MiB).
- **Headroom for Swiss r2119:**

  | Payload area | Headroom |
  |---|---|
  | 1 MB | **301 KB short** |
  | 1.5 MB | 223 KB to spare |
  | 1.69 MiB ceiling | 419 KB to spare |

- **gekkoboot on Pico 2:** its `dol2ipl.py` still writes only RP2040-family blocks, and PR #53 (universal UF2) has been open since 2025-05.
  - Without a partition table the RP2350 ROM ignores rp2040 blocks, so gekkoboot's own UF2 doesn't install on a Pico 2 at all.
  - PicoBoot's `process_ipl.py` writes both families.
  - A payload upload over Wi-Fi that accepts any family fixes this for our users.
- **Conclusion:** the payload area has to stay 1.5 MB, at `0x80000`.

## 3. OTA approaches on Pico W / Pico 2 W

| Project | How | Fit |
|---|---|---|
| pico-examples `ota_update` | Boot ROM A/B with TBYB, Wi-Fi firmware partition, TCP push from a Python script | Building blocks only. Uses a partition table, which breaks USB payload UF2s ([boot-rom.md](boot-rom.md) §5) |
| [pico_fota_bootloader](https://github.com/JZimnol/pico_fota_bootloader) | Own 36K bootloader; swaps download and app slots sector by sector at boot; rollback; SHA-256 | Doesn't use the ROM's A/B. The swap runs at boot, which delays the injection [inf] |
| [picowota](https://github.com/usedbytes/picowota) | Wi-Fi inside the bootloader, TCP port 4242 | Pico W only, last push 2024-07, Wi-Fi blob stored twice |
| arduino-pico HTTPUpdateServer | Browser upload, staged and copied | Arduino core; only the idea transfers |

**Conclusion:** none of these is a drop-in.
- What we take from the SDK and pico-examples: lwIP, the AP/DHCP/DNS code, `rom_chain_image()` and `flash_safe_execute()`.
- What we write ourselves: the loader, the slot logic and the web UI.

## 4. Web building blocks

- **lwIP httpd POST:**
  - needs a Content-Length (no chunked uploads);
  - delivers the body as raw pbufs, with no multipart parsing, so the browser should send `fetch()` with `application/octet-stream`;
  - request headers must fit in `LWIP_HTTPD_MAX_REQ_LENGTH` (≤1023 bytes by default);
  - the URI is limited to 63 characters;
  - `LWIP_HTTPD_POST_MANUAL_WND` can hold back the upload while a flash sector is erased.
- **Captive portal:**
  - Phones probe `captive.apple.com/hotspot-detect.html`, `connectivitycheck.gstatic.com/generate_204` and `msftconnecttest.com/connecttest.txt`. A DNS server that answers every name with our own IP, plus a redirect to the IP literal, triggers the portal.
  - DHCP option 114 (RFC 8908) needs HTTPS with a valid certificate, which is impractical offline.
  - Chrome's HTTPS-by-default exempts private IPs.
  - Phones may complain about a network without internet; NeoPicoBoot tells users to pick "Connect anyway".
- **mDNS:** lwIP's responder can run on the AP interface. Android resolves `.local` since November 2021.
- **AP security:**
  - cyw43-driver supports OPEN, WPA2-AES, WPA2 mixed, WPA3-SAE (with protected management frames) and WPA3/WPA2 transition.
  - Up to 4 clients.

## 5. What users want, and what went wrong elsewhere

- **Wanted:**
  - setup through the AP, optionally joining home Wi-Fi with a fallback to the AP;
  - OTA with a fallback path (SD or USB);
  - DOL conversion in the browser;
  - the upload's SHA shown before flashing;
  - gzip uploads.
- **Went wrong:**
  - **Open access:** NeoPicoBoot's open SSID; FlashAir's default credentials (CVE-2016-4863, CVE-2017-2162); the MemCard PRO exposes everything on the LAN without a login (criticised in PicoBoot #65).
  - **Interference:** gbs-control users see the video board interfere with Wi-Fi.
  - **Wi-Fi left on:** costs about 40 mA continuously [inf: an auto-off timer is worth having].
  - **Dependence on a server:** the MemCard PRO's server move stranded users. A design that needs no server avoids this.

## Sources

- [webhdx/PicoBoot](https://github.com/webhdx/PicoBoot) (releases, issues #57, #65, #66, #74), [NeoPicoBoot](https://github.com/paulmreese/NeoPicoBoot)
- [Swiss releases](https://github.com/emukidid/swiss-gc/releases) r2018, r2092, r2119; [gekkoboot](https://github.com/redolution/gekkoboot) r9.4, PR #53, `buildtools/dol2ipl.py`
- [pico-examples](https://github.com/raspberrypi/pico-examples) `pico_w/wifi/ota_update`, `access_point`, `httpd`
- [pico_fota_bootloader](https://github.com/JZimnol/pico_fota_bootloader), [picowota](https://github.com/usedbytes/picowota), [arduino-pico](https://github.com/earlephilhower/arduino-pico)
- lwIP `httpd.c`; cyw43-driver `cyw43.h`; MicroPython `extmod/network_cyw43.c`
- [Apple: captive network assistant / RFC 8908 notes](https://developer.apple.com/news/?id=q78sq5rv)
