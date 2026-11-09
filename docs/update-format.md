# PicoBoot-WiFi: update file format

| | |
|---|---|
| **Status** | Draft for phase 3 (plan step 3.1), for the owner's review |
| **Design** | [architecture.md](architecture.md) D6 (layout), D11 (update file), D12 (trial), D14 (Wi-Fi blob), D24 (release) |

An over-the-air firmware update is one file, `picoboot-wifi-X.Y.Z-update.bin`. CI makes it with `tools/mkupdate.py` (plan step 3.2) from the same build as the USB install UF2. The web UI and the CLI upload it to the Pico, which writes it into the slot that isn't running (D11).

## Layout

```
offset 0      header       256 bytes
offset 256    slot image   image_size bytes: the firmware, as it goes into slot A or B
then          Wi-Fi blob   blob_size bytes: a Wi-Fi area image, as it goes into Wi-Fi A or B
```

- **Nothing else:** the file is exactly `256 + image_size + blob_size` bytes long.
- **The Wi-Fi blob is always included** in release files (the owner's choice). The Pico writes it only when the paired Wi-Fi area holds a different one, which is rare (D14).
  - **Why:** one file always works, and the Pico needs no path that copies the blob between the Wi-Fi areas.
  - **Cost:** the file is about 420 KB instead of 190 KB, about a second more to upload.
  - **Format allows a file without the blob** (flag bit 0 clear) for builds that don't change it. The Pico takes such a file only if the paired Wi-Fi area already holds the blob the header names; otherwise it refuses it (`needs_blob`).

## Header

All numbers are little-endian; strings are ASCII, padded with zero bytes.

| Offset | Size | Field | Value |
|---|---|---|---|
| 0 | 4 | `magic` | `"PBWU"` (PicoBoot-WiFi update) |
| 4 | 2 | `format` | 1: this document |
| 6 | 2 | `header_size` | 256 |
| 8 | 4 | `flags` | Bit 0: the Wi-Fi blob is included. The other bits are 0 |
| 12 | 4 | `layout` | The flash layout the firmware is built for: `LAYOUT_VERSION` in `layout.h` (1) |
| 16 | 4 | `loader` | The lowest loader version the firmware works with (see below) |
| 20 | 4 | `image_size` | Bytes of slot image: 1 to 262144 (`LAYOUT_SLOT_SIZE`) |
| 24 | 4 | `blob_size` | Bytes of Wi-Fi blob: 0 if not included, else 64 to 245760 (`LAYOUT_WIFI_SIZE`) |
| 28 | 4 | reserved | 0 |
| 32 | 16 | `board` | `"pico2_w"` (`PICO_BOARD`) |
| 48 | 32 | `version` | The firmware's version and build, as the status shows it: `"1.0.0"`, `"0.3.0-dev+1a2b3c4"` |
| 80 | 32 | `image_sha256` | SHA-256 of the slot image |
| 112 | 32 | `blob_sha256` | SHA-256 of the Wi-Fi area image this firmware needs (its 64-byte `W43B` header and the data, as `tools/mkwifiblob.py` makes it), whether included or not |
| 144 | 80 | reserved | 0 |
| 224 | 32 | `header_sha256` | SHA-256 of bytes 0 to 223 |

- **The slot image** is `firmware.bin` from the build: the image the loader chains with `rom_chain_image()` (D7). It's the same bytes the USB install writes into slot A.
- **The Wi-Fi blob** is `wifi-blob.bin` from the build, the same bytes the USB install writes into Wi-Fi A.

### Versions of the loader and the layout

The loader changes only with a USB install (D24), so an update must not bring a firmware the installed loader can't start or understand.

- **The loader version** counts up when the loader's side of boot info (`bootinfo.h`) or boot control (`bootctl.h`) changes in a way older loaders don't handle.
  - From phase 3, the loader reports its version in boot info, and the firmware passes it on.
  - Loaders from phases 1 and 2 report none: they count as version 1.
  - The phase 3 loader, with the trial and fallback of D12, is version 2. So the first phase 3 firmware needs a USB install once; after that, updates go over the air.
- **The layout version** counts up when `layout.h` moves an area. An update to another layout always needs a USB install, since the update itself can't move the areas.

## What the Pico checks

**Before it writes anything,** as soon as the header has arrived:

| Check | Refused with |
|---|---|
| `magic` is `"PBWU"`. A file that starts with `"UF2\n"` gets its own message: it's the USB install file (D11) | `not_an_update` |
| `format` is 1 and `header_size` is 256 | `unsupported_format` |
| `header_sha256` matches | `bad_header` |
| `board` is `"pico2_w"` | `wrong_board` |
| `layout` is this firmware's `LAYOUT_VERSION` | `needs_usb_install` |
| `loader` is at most the installed loader's version | `needs_usb_install` |
| `image_size` and `blob_size` are in range, `flags` has no unknown bits, the reserved bytes are 0 | `bad_header` |
| The upload's length (`Content-Length`) is `256 + image_size + blob_size` | `bad_size` |
| Without the blob: the paired Wi-Fi area holds the one `blob_sha256` names | `needs_blob` |

**While the file streams in:**
- the slot image goes into the slot that isn't running, a sector at a time, hashed on the way;
- the Wi-Fi blob goes into the paired Wi-Fi area (Wi-Fi A with slot A, Wi-Fi B with slot B), but only if that area doesn't already hold an image with `blob_sha256`; otherwise its bytes are hashed and dropped.

**After the last byte:**

| Check | Refused with |
|---|---|
| The image's SHA-256 matches `image_sha256`, as received, and again when read back from flash | `bad_image` |
| The blob's SHA-256 matches `blob_sha256`, as received, and again when read back if it was written | `bad_blob` |

**Only then** does the Pico mark the slot as pending in boot control (D12). It answers with the version it installed, then reboots into the trial once the answer is out (D11). That reboot is warm, so a running game isn't touched (D8).

**A refused or cut-off upload changes nothing that runs.**
- The running slot and its Wi-Fi area aren't touched.
- The other slot may be left half written. Before its first erase, the Pico marks it empty in boot control, so the loader never chains it as a new firmware or falls back to it (D12), and the firmware page shows it as empty.

## Versions: the downgrade warning

The UI reads the header itself, before uploading. A file is only 420 KB, and its first 256 bytes say what it is. It shows the version and compares it with the running one by semver:
- `X.Y.Z` numerically;
- a pre-release (`-dev`) before the release of the same number;
- the build (`+hash`) ignored.

| The file's version is… | The UI says |
|---|---|
| Newer | Nothing extra |
| The same | "This version is already running; install it again?" |
| Older | "This is an older version than the one running; install it anyway?" (the owner's choice: allowed, with a warning) |

The Pico itself accepts any version that passes the checks above: the trial and fallback of D12 protect a downgrade as they protect an upgrade.

## Not in the format

- **No signature:** the Wi-Fi password is the gate (D11, D19). A later release can add one in the reserved bytes, under a new `format` number.
- **No compression:** the Pico writes flash sector by sector as the file arrives, and the file is small.
- **No payloads:** payload uploads are a separate API (phase 4).
