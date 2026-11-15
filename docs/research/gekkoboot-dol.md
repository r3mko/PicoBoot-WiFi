# The built-in gekkoboot: which r9.4 build

Whether the built-in gekkoboot (`assets/ipl/gekkoboot_r9_4.dol`) is the official r9.4 release, what it is if not, and how to rebuild the official one. For release 1.0.3 (the plan's "Release 1.0.3"); the owner chose its scope from this note.

**Sources:**
- the asset at `1.0.2` and `tools/process_ipl.py`;
- [gekkoboot](https://github.com/redolution/gekkoboot): tags `r9.3` (`d363ae4`) and `r9.4` (`dbf12ff`), `master` (`28a1bf2`), `meson.build`, `packer/`, `stub/`, `buildtools/dol2ipl.py`, `.github/workflows/continuous-integration-workflow.yml`;
- the release zips `gekkoboot-r9.3.zip` and `gekkoboot-r9.4.zip`, and the GitHub API's release data;
- PicoBoot-DX's git history.

Nothing here was measured on hardware.

## 1. What the asset is

- **Where it came from:**
  - PicoBoot-WiFi took it from PicoBoot-DX, which added it on 2025-05-20 (`3d6835a`; `iplboot.dol` there is a symlink to it).
  - The owner took it from a GitHub Actions artifact of gekkoboot's build: the only workflow builds every push in the `devkitpro/devkitppc:latest` container and uploads the `build` directory.
  - The release zip has no `.dol`.
- **Not the release's build:**
  - The r9.4 zip was uploaded by hand by the maintainer (9ary) on 2025-04-30, 40 seconds after the release was published and two days after the tagged commit. So its files were built outside that workflow [inf].
  - gekkoboot's run logs and artifacts from then are no longer listed by the API.
- **Compared at the level PicoBoot uses.** Our DOL through `process_ipl.py`, against the payload in the release's `gekkoboot_pico.uf2` (scrambling is an XOR with a fixed keystream, so the images can be compared directly). gekkoboot packs its program with xz (7-Zip, PowerPC filter) behind a small unpacker, the packer (`packer/`), so both were unpacked too:

  | | Our asset | Official r9.4 | Official r9.3 |
  |---|---|---|---|
  | Image | 60,512 B | 60,512 B | 59,104 B |
  | Packer | 0x2580 B | 0x2580 B | 0x25a0 B |
  | xz stream | 50,796 B | 50,776 B | |
  | Program, unpacked | 128,352 B | 128,352 B | 125,792 B |
  | Version string | `undefined` | `r9.4` | `r9.3` |
  | Our program's 16-byte windows (at 4-byte steps) found anywhere in it | | 94.8% | 39.1% |

- **The same source, built differently:**
  - All 25 strings new in r9.4 are in ours, and none of the 20 it removed are (for example libogc2's crash dump, `SRR0 %08X` instead of r9.3's `%08x`, and no `Abort called.`).
  - Every message string matches r9.4's except the version.
  - Between the r9.4 tag and 2025-05-20, `master` has one more commit, `c8f7e10`, which only touches `buildtools/qoob_injector.py`. So the program's source is exactly r9.4's: FatFs R0.15a-p1, the libogc2 updates, "Populate lowmem metadata", the aligned FatFs buffers, and the packer synced from Swiss.
- **Two differences:**
  - **The version:** gekkoboot's version comes from `git describe` through meson's `vcs_tag()`. It reads `undefined` when that fails, so the CI run's git didn't work; why can't be checked any more. gekkoboot prints it at the top of its screen (`gekkoboot %s`), which stays up while D-Pad Down is held.
  - **The code layout:** 5,051 of 32,088 words differ at the same offset, 3,807 of them in the opcode. A sequence alignment matches 28,988 words in equal runs, so it's mostly the same code and data in a different place. That points to a different devkitPPC or libogc ("latest" in a container), or a different link order [inf].
- **The packer's code is the same.** The official xz stream is 20 bytes shorter, which moves the packer's small-data base by 0x14. All 196 words that differ in the packer are the same instruction with its offset moved by 20 bytes: 191 accesses through `r13` at +0x14, and five `addi` at −0x14 [inf: references past the xz stream]. The padding to 32 bytes keeps both images the same total size.

## 2. Rebuilding the official DOL

- **The image:** the release's Swiss IGR `apploader.img` is a 32-byte header plus the unscrambled image. Unscrambling `gekkoboot_pico.uf2`'s payload gives the same bytes.
- **The layout, from the image itself** (packer `link.ld` and `crt0.S` at `r9.4`):
  - one text section, the packer's code, at `0x81300000`, up to the xz stream, which starts the data section (`.rodata` begins with `executable_xz`);
  - one data section with the rest;
  - entry `0x81300000`;
  - BSS: the packer's `.sbss`, which its `crt0` clears itself. Its bounds are in `crt0` as `addi r3, r13, __sbss_start@sdarel` and `addi r4, r13, __sbss_end@sdarel`. Both builds clear 0x7820 bytes from `0x8130ec60`, the end of the image. The official build's offsets are 20 bytes larger, matching its 20-byte smaller-data shift.
- **The check:**
  - gekkoboot r9.4's own `buildtools/dol2ipl.py` makes `gekkoboot_pico.uf2`, `gekkoboot_viper.vgc` and `apploader.img` from the official DOL, and with an IPL ROM `qoob_pro/gekkoboot_qoob_pro.gcb`, from which `buildtools/qoob_injector.py` makes `qoob_pro_gekkoboot_upgrade.elf` (`meson.build`).
  - Run on the rebuilt DOL, they reproduce all five release files **byte for byte**; the Qoob Pro pair with the IPL ROM gekkoboot's CI fetches (r5.2's `iplboot.gcb`). Run on our asset, all five differ.
  - `process_ipl.py` on the rebuilt DOL gives `gekkoboot_pico.uf2`'s payload exactly. The unpacked program reads `r9.4`.
- **The rebuilt DOL:** 60,768 B, SHA-256 `6172f4a802968fb819b0a595812ec8c0495ef7c40cc3e1c734253b384a15fe62`.
- **Caveat:** the maintainer's own `.dol` was never published, so its header can't be compared directly. Everything the release made from it is reproduced; only the `.gci` (the lowmem build) and the Qoob SX file (the `gekkoboot_sx` build) come from other build variants. Our old asset is `elf2dol`'s output, and the tool rebuilds it byte for byte from its own image, so the tool writes the header `elf2dol` does for this layout.

## 3. Conclusions for release 1.0.3

The owner's choices (2026-11-15): use the rebuilt DOL; a tool that rebuilds it and a local test that ties it to the release; a focused re-test.

1. **The asset is r9.4's source but not the official binary.** It works; it reports "gekkoboot undefined" and has the CI toolchain's code layout.
2. **The official DOL can be rebuilt** from the release's `apploader.img`, with its layout read from the image itself. gekkoboot's own tools confirm the rebuild against five release files.
3. **The same size** (image 60,512 B, payload 60,548 B, the built-in's 61,440 B scrambled) means the firmware's layout, its start-up time and the margin shouldn't move.
4. **A tool** (`tools/gekkoboot_dol.py`) keeps the rebuild reproducible, and works for a later gekkoboot release with the same packer. **A local test** checks that the asset's payload equals the release's `gekkoboot_pico.uf2`, like the other payload tests (skipped in CI, which has no release files).
