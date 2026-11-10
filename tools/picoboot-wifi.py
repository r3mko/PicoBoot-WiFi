#!/usr/bin/env python3

"""
Command-line client for the PicoBoot-WiFi JSON API (docs/api.md). Join the Pico's Wi-Fi first.

    python3 tools/picoboot-wifi.py status [--json]
    python3 tools/picoboot-wifi.py update build/picoboot-wifi-update.bin [--yes] [--no-wait]
    python3 tools/picoboot-wifi.py keep
    python3 tools/picoboot-wifi.py boot A|B [--no-wait]
    python3 tools/picoboot-wifi.py reboot [--no-wait]
    python3 tools/picoboot-wifi.py payload upload FILE [--name NAME] [--yes]    (UF2, raw payload or .dol)
    python3 tools/picoboot-wifi.py payload choose payload1|payload2|builtin
    python3 tools/picoboot-wifi.py payload rename payload1|payload2 NAME
    python3 tools/picoboot-wifi.py payload delete payload1|payload2 [--yes]
    python3 tools/picoboot-wifi.py payload convert FILE.dol [-o OUT] [--force]  (offline)
    python3 tools/picoboot-wifi.py ipl-once [--off]
    python3 tools/picoboot-wifi.py backup payload1|payload2|firmware|settings [-o FILE] [--force]
    python3 tools/picoboot-wifi.py restore-settings FILE

Exit codes: 0 ok, 1 refused by the Pico or a bad file, 2 usage, 3 can't reach the Pico.
Standard library only; the tool works when copied on its own.
"""

import argparse
import hashlib
import http.client
import io
import json
import os
import re
import struct
import sys
import tempfile
import time

DEFAULT_HOST = "192.168.4.1"
MAGIC = b"PBWU"
UF2_MAGIC = b"UF2\n"
HEADER_SIZE = 256
PAYLOAD_MAGIC = b"IPLBOOT "
PAYLOAD_BASE = 0x10080000       # where a payload UF2 holds the payload (D16)
NAME_MAX = 24
EXPECTED_ENTRY = 0x81300000     # of an IPL payload, for the .dol converter (a copy of tools/process_ipl.py's)
EXPECTED_BASE = 0x01300000
BOARD = "pico2_w"
CHUNK = 16384
UPLOAD_READ_TIMEOUT = 60        # after the last byte the Pico still writes and checks flash
WAIT_TIMEOUT = 60               # for the Pico to come back after a reboot
FIRST_DELAY = 1.5               # the Pico reboots half a second after its answer
POLL_INTERVAL = 1.0

EXIT_OK, EXIT_REFUSED, EXIT_USAGE, EXIT_UNREACHABLE = 0, 1, 2, 3


class Refused(Exception):
    """The Pico refused, or the file is bad (exit 1)."""


class Cancelled(Exception):
    """The user said no at the prompt (exit 1, no message)."""


class Unreachable(Exception):
    """No (good) answer from the Pico (exit 3)."""


# ---- Update file, local checks --------------------------------------------------------------------

def read_update_header(path):
    """Check the file's header like the Pico does; returns (version, total_size)."""
    try:
        with open(path, "rb") as f:
            data = f.read(HEADER_SIZE)
        size = os.path.getsize(path)
    except OSError as e:
        raise Refused(f"can't read {path}: {e.strerror}")
    if data[:4] == UF2_MAGIC:
        raise Refused(f"{path} is a UF2 file, for USB install; an update is the .bin file "
                      "(picoboot-wifi-update.bin)")
    if data[:4] != MAGIC or len(data) < HEADER_SIZE:
        raise Refused(f"{path} is not a PicoBoot-WiFi update file")
    image_size, blob_size = struct.unpack_from("<II", data, 20)
    board = data[32:48].split(b"\0", 1)[0].decode("ascii", "replace")
    version = data[48:80].split(b"\0", 1)[0].decode("ascii", "replace")
    if board != BOARD:
        raise Refused(f"the update is for board {board!r}, this is for {BOARD!r}")
    if size != HEADER_SIZE + image_size + blob_size:
        raise Refused(f"{path} is {size} bytes, its header says {HEADER_SIZE + image_size + blob_size}; "
                      "the file is cut short or damaged")
    return version, size


def version_key(text):
    """Sort key for X.Y.Z[-pre][+build]: a pre-release is before its release, the build is ignored."""
    m = re.match(r"^(\d+)\.(\d+)\.(\d+)(?:-([^+]*))?(?:\+.*)?$", text or "")
    if not m:
        return None
    pre = m.group(4)
    return (int(m.group(1)), int(m.group(2)), int(m.group(3)), pre is None, pre or "")


def compare_versions(a, b):
    """-1, 0 or 1 by semver; None if either doesn't parse."""
    ka, kb = version_key(a), version_key(b)
    if ka is None or kb is None:
        return None
    return (ka > kb) - (ka < kb)


# ---- Payload files, local checks ------------------------------------------------------------------

def check_name(name):
    """A payload's name: at most 24 printable ASCII characters (docs/api.md)."""
    if len(name) > NAME_MAX or any(not 0x20 <= ord(c) <= 0x7E for c in name):
        raise Refused(f"the name must be at most {NAME_MAX} printable ASCII characters")
    return name


def name_from_file(path):
    """D42: the base name without its last extension; _ to space; printable ASCII only; spaces
    collapsed and trimmed; first character upper-case; cut to 24 characters and trimmed."""
    name = os.path.splitext(os.path.basename(path))[0].replace("_", " ")
    name = "".join(c for c in name if 0x20 <= ord(c) <= 0x7E)
    name = re.sub(r" +", " ", name).strip()
    return (name[:1].upper() + name[1:])[:NAME_MAX].rstrip()


def scramble(data):
    """The GameCube bootrom's scrambling, in place (a copy of tools/process_ipl.py's)."""
    acc = 0
    nacc = 0
    t, u, v = 0x2953, 0xD9C2, 0x3FF1
    x = 1
    it = 0
    while it < len(data):
        t0 = t & 1
        t1 = (t >> 1) & 1
        u0 = u & 1
        u1 = (u >> 1) & 1
        v0 = v & 1
        x ^= t1 ^ v0
        x ^= u0 | u1
        x ^= (t0 ^ u1 ^ v0) & (t0 ^ u0)
        if t0 == u0:
            v >>= 1
            if v0:
                v ^= 0xB3D0
        if t0 == 0:
            u >>= 1
            if u0:
                u ^= 0xFB10
        t >>= 1
        if t0:
            t ^= 0xA740
        nacc = (nacc + 1) % 256
        acc = (acc * 2 + x) % 256
        if nacc == 8:
            data[it] ^= acc
            nacc = 0
            it += 1
    return data


def flatten_dol(data):
    """A .dol as one memory image: (base address, entry point, bytes) (a copy of tools/process_ipl.py's)."""
    if len(data) < 256:
        raise ValueError(f"File is {len(data)} bytes, smaller than a DOL header (256 bytes)")
    header = struct.unpack(">64I", data[:256])
    offsets, addresses, sizes = header[:18], header[18:36], header[36:54]
    entry = header[56]
    if not any(sizes):
        raise ValueError("DOL contains no sections")
    for num, (offset, address, size) in enumerate(zip(offsets, addresses, sizes)):
        if size > 0 and address == 0:
            raise ValueError(f"Section {num} has data but no load address")
        if size > 0 and offset + size > len(data):
            raise ValueError(f"Section {num} ends at 0x{offset + size:X}, past the end of the file (0x{len(data):X} bytes), file truncated?")
    dol_min = min(addr for addr in addresses if addr)
    dol_max = max(addr + size for addr, size in zip(addresses, sizes))
    if dol_max - dol_min > 0x01800000:
        raise ValueError(f"DOL sections span 0x{dol_max - dol_min:X} bytes, more than the 24 MB of GameCube main memory")
    img = bytearray(dol_max - dol_min)
    for offset, address, size in zip(offsets, addresses, sizes):
        if size > 0:
            start = address - dol_min
            img[start:start + size] = data[offset:offset + size]
    return dol_min, entry, img


def dol_to_payload(dol):
    """The raw payload of a .dol, as PicoBoot payloads hold it (D16): "IPLBOOT ", the total size
    big-endian, the image scrambled from offset 0x720, and a "PICO" footer (a copy of
    tools/process_ipl.py's). Raises ValueError for a DOL that isn't an IPL payload."""
    load, entry, img = flatten_dol(bytearray(dol))
    base = load & 0x017FFFFF
    entry = (entry & 0x017FFFFF) | 0x80000000
    if entry != EXPECTED_ENTRY or base != EXPECTED_BASE:
        raise ValueError(f"Invalid entry point or base address: 0x{entry:08X} / 0x{base:08X}, "
                         f"expected 0x{EXPECTED_ENTRY:08X} / 0x{EXPECTED_BASE:08X}")
    img += bytearray(-len(img) % 4)
    scrambled = scramble(bytearray(0x720) + img)[0x720:]
    size = 32 + len(scrambled) + 4
    return b"IPLBOOT " + struct.pack(">I", size) + bytes(20) + bytes(scrambled) + b"PICO"


def uf2_payload(data, path):
    """The payload bytes in a UF2: one family's blocks in order from 0x10080000, as the Pico takes them."""
    if len(data) % 512:
        raise Refused(f"{path} is not a whole number of UF2 blocks; the file is damaged")
    family, next_address, out = None, PAYLOAD_BASE, bytearray()
    for off in range(0, len(data), 512):
        block = data[off:off + 512]
        magic0, magic1, flags, address, length = struct.unpack_from("<5I", block, 0)
        if magic0 != 0x0A324655 or magic1 != 0x9E5D5157 or struct.unpack_from("<I", block, 508)[0] != 0x0AB16F30:
            raise Refused(f"{path} has a broken UF2 block at offset {off}")
        block_family = struct.unpack_from("<I", block, 28)[0] if flags & 0x2000 else 0
        if flags & 1 or address < PAYLOAD_BASE:
            continue
        if length > 476:
            raise Refused(f"{path} has a broken UF2 block at offset {off}")
        if family is None:
            if address != PAYLOAD_BASE:
                continue
            family = block_family
        elif block_family != family:
            continue
        if address != next_address:
            raise Refused(f"{path}: the payload's UF2 blocks are not in order")
        next_address += length
        out += block[32:32 + length]
    if not out:
        raise Refused(f"{path} holds no payload at 0x{PAYLOAD_BASE:08X}; it is not a payload UF2 "
                      "(the USB install file is not one)")
    return bytes(out)


def raw_payload(data, path):
    """Check a payload's header and footer; returns the payload, cut at the size its header gives: a
    flash dump has padding after it, which mustn't count against the slot's size."""
    if data[:8] != PAYLOAD_MAGIC or len(data) < 12:
        raise Refused(f"{path} is not a payload")
    size = struct.unpack_from(">I", data, 8)[0]
    if size < 36 or size % 4:
        raise Refused(f"{path} is not a good payload: its size field says {size}")
    if len(data) < size:
        raise Refused(f"{path} is {len(data)} bytes, its header says {size}; the file is cut short")
    if data[size - 4:size] != b"PICO":
        raise Refused(f"{path} is not a good payload: it doesn't end with its footer")
    return data[:size]


def convert_dol(data, path):
    try:
        return dol_to_payload(data)
    except ValueError as e:
        raise Refused(f"{path}: {e}")


def load_payload(path):
    """Read a payload UF2, raw payload or .dol; returns (what to upload, the payload's bytes, how it came)."""
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError as e:
        raise Refused(f"can't read {path}: {e.strerror}")
    if data[:4] == UF2_MAGIC:
        return data, raw_payload(uf2_payload(data, path), path), "UF2"
    if data[:8] == PAYLOAD_MAGIC:
        payload = raw_payload(data, path)
        return payload, payload, "raw payload" + (", without the padding after it" if len(payload) < len(data) else "")
    if path.lower().endswith(".dol"):
        payload = convert_dol(data, path)
        return payload, payload, ".dol, converted"
    raise Refused(f"{path} is not a payload: expected a payload UF2, a raw payload "
                  "(starts with IPLBOOT) or a .dol")


# ---- HTTP -----------------------------------------------------------------------------------------

def decode(response):
    raw = response.read()
    try:
        body = json.loads(raw)
    except ValueError:
        body = None
    return response.status, body if isinstance(body, dict) else {}


def request(args, method, path, body=None, timeout=None):
    """One JSON request; returns (status, dict). Raises Unreachable."""
    conn = http.client.HTTPConnection(args.host, timeout=timeout or args.timeout)
    headers = {"Connection": "close"}
    data = None
    if body is not None:
        data = json.dumps(body).encode()
        headers["Content-Type"] = "application/json"
    try:
        conn.request(method, path, data, headers)
        return decode(conn.getresponse())
    except (OSError, http.client.HTTPException) as e:
        raise Unreachable(f"can't reach the Pico at {args.host}: {e}")
    finally:
        conn.close()


def check(status, body):
    """Return body for 2xx, else raise Refused with the server's message."""
    if 200 <= status < 300:
        return body
    text = body.get("message") or f"HTTP {status}"
    if body.get("error") == "on_trial":
        text += ("\nThe running firmware is on trial and the other slot holds the one to fall back to. "
                 "Run 'keep' to keep this firmware, or 'boot' the other slot to go back, then try again.")
    raise Refused(text)


def post(args, path, body):
    return check(*request(args, "POST", path, body))


def get_status(args):
    return check(*request(args, "GET", "/api/status"))


def download(args, path):
    """GET a file; returns (status, headers, body bytes). Raises Unreachable."""
    conn = http.client.HTTPConnection(args.host, timeout=args.timeout)
    try:
        conn.request("GET", path, headers={"Connection": "close"})
        response = conn.getresponse()
        return response.status, response.headers, response.read()
    except (OSError, http.client.HTTPException) as e:
        raise Unreachable(f"can't reach the Pico at {args.host}: {e}")
    finally:
        conn.close()


def upload(args, path, size, endpoint="/api/firmware", data=None, name=None):
    """POST the file (or `data`) as application/octet-stream in chunks, with progress on stderr."""
    conn = http.client.HTTPConnection(args.host, timeout=args.timeout)
    try:
        conn.putrequest("POST", endpoint)
        conn.putheader("Content-Type", "application/octet-stream")
        if name:
            conn.putheader("X-Name", name)
        conn.putheader("Content-Length", str(size))
        conn.putheader("Connection", "close")
        conn.endheaders()
        sent = 0
        try:
            with (io.BytesIO(data) if data is not None else open(path, "rb")) as f:
                while True:
                    chunk = f.read(CHUNK)
                    if not chunk:
                        break
                    conn.send(chunk)
                    sent += len(chunk)
                    sys.stderr.write(f"\rUploading {sent * 100 // size:3d}% ({sent // 1024} of {size // 1024} KB)")
                    sys.stderr.flush()
        except OSError:
            # The Pico may refuse from the header and close early; its answer is still there to read
            if sent == 0:
                raise
        finally:
            sys.stderr.write("\n")
        sys.stderr.write("Waiting for the Pico to write and check flash...\n")
        conn.sock.settimeout(max(args.timeout, UPLOAD_READ_TIMEOUT))
        return decode(conn.getresponse())
    except (OSError, http.client.HTTPException) as e:
        raise Unreachable(f"can't reach the Pico at {args.host}: {e}")
    finally:
        conn.close()


def wait_for_pico(args):
    """Poll /api/status until the Pico answers again; returns the status."""
    sys.stderr.write("Waiting for the Pico to reboot...\n")
    time.sleep(FIRST_DELAY)
    end = time.monotonic() + WAIT_TIMEOUT
    while True:
        try:
            status, body = request(args, "GET", "/api/status", timeout=min(args.timeout, 3))
            if status == 200 and body:
                return body
        except Unreachable:
            pass
        if time.monotonic() >= end:
            raise Unreachable(f"the Pico didn't answer again within {WAIT_TIMEOUT} s; "
                              "check that you are still on its Wi-Fi")
        time.sleep(POLL_INTERVAL)


def after_reboot(args):
    if args.no_wait:
        return
    print(status_line(wait_for_pico(args)))


# ---- Output ---------------------------------------------------------------------------------------

def status_line(st):
    fw = st.get("firmware", {})
    slot = fw.get("slot")
    line = f"Running slot {slot}, version {fw.get('version')}"
    if fw.get("pending") is not None and fw.get("pending") == slot:
        line += ", on trial (run 'keep' to keep it)"
    return line


def print_status(st):
    fw, wifi = st.get("firmware", {}), st.get("wifi", {})
    pending = fw.get("pending")
    print(f"Firmware:    {fw.get('version')} (running from slot {fw.get('slot')}, loader {fw.get('loader')})")
    for slot in ("A", "B"):
        version = (fw.get("versions") or {}).get(slot) or "-"
        state = (fw.get("states") or {}).get(slot, "?")
        print(f"  Slot {slot}:    {version}, {state}{' (pending)' if pending == slot else ''}")
    print(f"Wi-Fi:       {wifi.get('ssid')}, {wifi.get('clients')} client(s)")
    if wifi.get("password_is_default"):
        print("             The password is still the default; change it in the settings.")
    pl = st.get("payloads")
    if pl:
        line = f"Payloads:    {slot_label(pl.get('chosen') or '?')} chosen; original IPL once: {'yes' if pl.get('original_ipl_once') else 'no'}"
        print(line + (" (a USB install changed payload 1)" if pl.get("usb_install") else ""))
        for slot in ("payload1", "payload2"):
            p = pl.get(slot) or {}
            if p.get("valid"):
                what = f"{p.get('name') or '(no name)'}, {p.get('size', 0) / 1024:.0f} KB"
            else:
                what = "empty"
            note = ", next upload goes here" if pl.get("upload_target") == slot else ""
            print(f"  Payload {slot[-1]}: {what}{note}")
    up = st.get("uptime_s")
    if up is not None:
        print(f"Uptime:      {up // 3600} h {up % 3600 // 60} min {up % 60} s")
    if st.get("temperature_c") is not None:
        print(f"Temperature: {st['temperature_c']:.1f} C")


# ---- Commands -------------------------------------------------------------------------------------

def cmd_status(args):
    st = get_status(args)
    if args.json:
        print(json.dumps(st, indent=2))
    else:
        print_status(st)


def confirm(question, yes):
    if yes:
        return True
    sys.stderr.write(f"{question} [y/N] ")
    sys.stderr.flush()
    return sys.stdin.readline().strip().lower() in ("y", "yes")


def cmd_update(args):
    version, size = read_update_header(args.file)       # before any request
    before = get_status(args).get("firmware", {})
    running = before.get("version")
    print(f"Update file: {version}\nRunning:     {running}")
    cmp = compare_versions(version, running)
    if cmp is None:
        question = "Can't compare the versions; install anyway?"
    elif cmp == 0:
        question = "This version is already running; install it again?"
    elif cmp < 0:
        question = "This is an older version than the one running; install it anyway?"
    else:
        question = "Install it?"
    if not confirm(question, args.yes):
        print("Cancelled.")
        raise Cancelled()
    try:
        body = check(*upload(args, args.file, size))
    except Unreachable:
        # The whole file may have gone through with only the answer lost: ask the Pico what it did
        if args.no_wait:
            raise
        sys.stderr.write("No answer from the Pico; checking what happened...\n")
        report_unanswered(wait_for_pico(args), before)
        return
    print(f"Installed {body.get('version')} in slot {body.get('slot')}, on trial.")
    if body.get("rebooting") and not args.no_wait:
        st = wait_for_pico(args)
        print(status_line(st))
        fw = st.get("firmware", {})
        if fw.get("slot") != body.get("slot") or fw.get("pending") != body.get("slot"):
            # It crashed on trial and the loader went back (D12)
            raise Refused(f"the new firmware didn't come up: the Pico went back to slot {fw.get('slot')}, "
                          f"and slot {body.get('slot')} is marked failed")


def report_unanswered(st, before):
    """After an upload without an answer: say what the status shows, by the slot it would go into."""
    fw = st.get("firmware", {})
    target = "B" if before.get("slot") == "A" else "A"
    print(status_line(st))
    if fw.get("slot") == target and fw.get("pending") == target:
        print(f"The update went through: slot {target} runs it on trial.")
        return
    state = (fw.get("states") or {}).get(target)
    raise Refused(f"the upload's answer was lost, and slot {target} is {state}: the update didn't go through, "
                  "or its trial failed; try again")


def cmd_keep(args):
    post(args, "/api/boot", {"keep": True})
    print("Kept: the running firmware is now the active one.")


def cmd_boot(args):
    body = post(args, "/api/boot", {"slot": args.slot})
    if body.get("rebooting"):
        print(f"Booting slot {args.slot}.")
        after_reboot(args)
    else:
        print(f"Slot {args.slot} is running already.")


def cmd_reboot(args):
    post(args, "/api/reboot", {})
    print("Rebooting.")
    after_reboot(args)


def slot_label(slot):
    return f"payload {slot[-1]}" if slot.startswith("payload") else "the built-in gekkoboot" if slot == "builtin" else slot


def cmd_payload_upload(args):
    name = check_name(args.name) if args.name is not None else name_from_file(args.file)
    body, payload, kind = load_payload(args.file)       # before any request
    digest = hashlib.sha256(payload).hexdigest()
    pl = get_status(args).get("payloads")
    if not pl or not pl.get("upload_target"):
        raise Refused("the Pico's firmware has no payload support; update it first")
    target = pl["upload_target"]
    other = "payload2" if target == "payload1" else "payload1"
    print(f"Payload:     {name or '(no name)'}, {len(payload)} bytes ({kind})")
    print(f"Uploading into {slot_label(target)}; {slot_label(other)} stays as it is. "
          "It becomes the choice once the Pico has verified it.")
    if (pl.get(target) or {}).get("valid"):
        if not confirm(f"{slot_label(target).capitalize()} holds '{pl[target].get('name')}'; replace it?", args.yes):
            print("Cancelled.")
            raise Cancelled()
    try:
        answer = check(*upload(args, args.file, len(body), "/api/payload", body, name))
    except Unreachable:
        # The whole payload may have gone through with only the answer lost: ask the Pico what it holds
        sys.stderr.write("No answer from the Pico; checking what happened...\n")
        st = wait_for_pico(args)
        slot = (st.get("payloads") or {}).get(target) or {}
        if slot.get("valid") and slot.get("sha256") == digest:
            print(f"The upload went through: {slot_label(target)} holds it.\nSHA-256: {digest}")
            return
        raise Refused(f"the upload's answer was lost, and {slot_label(target)} doesn't hold this payload: "
                      "the upload didn't go through; try again")
    if answer.get("sha256") and answer["sha256"] != digest:
        raise Refused(f"the Pico stored a payload with SHA-256 {answer['sha256']}, not the one sent ({digest})")
    print(f"Installed in {slot_label(answer.get('slot', target))} as '{answer.get('name', name)}', "
          f"{answer.get('size', len(payload))} bytes; it is the choice now.\nSHA-256: {digest}")


def cmd_payload_choose(args):
    post(args, "/api/boot", {"payload": args.payload})
    print(f"The next power-on injects {slot_label(args.payload)}.")


def cmd_payload_rename(args):
    post(args, "/api/payload/name", {"slot": args.slot, "name": check_name(args.name)})
    print(f"{slot_label(args.slot).capitalize()} is named '{args.name}'.")


def cmd_payload_delete(args):
    if not confirm(f"Delete {slot_label(args.slot)}?", args.yes):
        print("Cancelled.")
        raise Cancelled()
    post(args, "/api/payload/delete", {"slot": args.slot})
    print(f"Deleted {slot_label(args.slot)}.")


def write_new(path, data, force):
    if os.path.exists(path) and not force:
        raise Refused(f"{path} exists already; use --force to overwrite it")
    try:
        with open(path, "wb") as f:
            f.write(data)
    except OSError as e:
        raise Refused(f"can't write {path}: {e.strerror}")


def cmd_payload_convert(args):
    try:
        with open(args.file, "rb") as f:
            dol = f.read()
    except OSError as e:
        raise Refused(f"can't read {args.file}: {e.strerror}")
    out = args.output or os.path.splitext(args.file)[0] + ".bin"
    if os.path.exists(out) and not args.force:
        raise Refused(f"{out} exists already; use --force to overwrite it")
    payload = convert_dol(dol, args.file)
    write_new(out, payload, True)
    print(f"Wrote {out}, {len(payload)} bytes.\nSHA-256: {hashlib.sha256(payload).hexdigest()}")


def cmd_ipl_once(args):
    post(args, "/api/boot", {"original_ipl_once": not args.off})
    if args.off:
        print("The next power-on injects the chosen payload.")
    else:
        print("The next power-on boots the console's own IPL, then this clears itself.")


def verify_update_download(data):
    """The firmware backup is an update file: header, length and the SHA-256s. Returns its version."""
    if len(data) < HEADER_SIZE or data[:4] != MAGIC:
        raise Refused("the download is not a PicoBoot-WiFi update file")
    if hashlib.sha256(data[:224]).digest() != data[224:256]:
        raise Refused("the download's header SHA-256 is wrong")
    flags = struct.unpack_from("<I", data, 8)[0]
    image_size, blob_size = struct.unpack_from("<II", data, 20)
    if len(data) != HEADER_SIZE + image_size + blob_size:
        raise Refused(f"the download is {len(data)} bytes, its header says {HEADER_SIZE + image_size + blob_size}")
    if hashlib.sha256(data[HEADER_SIZE:HEADER_SIZE + image_size]).digest() != data[80:112]:
        raise Refused("the download's firmware image SHA-256 is wrong")
    if flags & 1 and hashlib.sha256(data[HEADER_SIZE + image_size:]).digest() != data[112:144]:
        raise Refused("the download's Wi-Fi blob SHA-256 is wrong")
    with tempfile.TemporaryDirectory() as tmp:          # The header checks of the update command
        path = os.path.join(tmp, "backup")
        with open(path, "wb") as f:
            f.write(data)
        try:
            return read_update_header(path)[0]
        except Refused as e:
            raise Refused(f"the download is bad: {e}".replace(path, "it"))


def backup_file_name(headers, what):
    """The base name from Content-Disposition, or a default."""
    m = re.search(r'filename\s*=\s*(?:"([^"]*)"|([^;\s]+))', headers.get("Content-Disposition") or "")
    name = (m.group(1) if m and m.group(1) is not None else m.group(2) if m else "").replace("\\", "/")
    name = os.path.basename(name)
    if name in ("", ".", ".."):
        name = {"settings": "picoboot-wifi-settings.json", "firmware": "picoboot-wifi-update.bin"}.get(what, what + ".bin")
    return name


def cmd_backup(args):
    what = args.what
    if args.output and os.path.exists(args.output) and not args.force:
        raise Refused(f"{args.output} exists already; use --force to overwrite it")
    want = None
    if what.startswith("payload"):
        want = (get_status(args).get("payloads") or {}).get(what) or {}
    status, headers, data = download(args, "/api/backup/" + what)
    if not 200 <= status < 300:
        try:
            body = json.loads(data)
        except ValueError:
            body = None
        check(status, body if isinstance(body, dict) else {})
    if want is not None:
        digest = hashlib.sha256(data).hexdigest()
        if not want.get("sha256"):
            raise Refused(f"the Pico gives no SHA-256 for {slot_label(what)} to check the download against")
        if digest != want["sha256"] or len(data) != want.get("size", len(data)):
            raise Refused(f"the download doesn't match: SHA-256 {digest}, the Pico says {want['sha256']}; try again")
        detail = f"SHA-256 {digest}"
    elif what == "firmware":
        detail = f"update file for version {verify_update_download(data)}, checked"
    else:
        try:
            settings = json.loads(data)
        except ValueError:
            settings = None
        if not isinstance(settings, dict):
            raise Refused("the downloaded settings are not a JSON object")
        detail = "valid JSON"
    path = args.output or backup_file_name(headers, what)
    write_new(path, data, args.force)
    print(f"Saved {path}, {len(data)} bytes ({detail}).")


def cmd_restore_settings(args):
    try:
        with open(args.file, "rb") as f:
            settings = json.loads(f.read())
    except OSError as e:
        raise Refused(f"can't read {args.file}: {e.strerror}")
    except ValueError:
        raise Refused(f"{args.file} is not JSON")
    if not isinstance(settings, dict):
        raise Refused(f"{args.file} is not a JSON object of settings")
    body = post(args, "/api/settings", settings)
    print("Settings saved.")
    if body.get("wifi_restart"):
        print(f"The access point restarts now, as '{body.get('ssid_in_use')}'. "
              "You may need to join its Wi-Fi again.")


def make_parser():
    p = argparse.ArgumentParser(
        prog="picoboot-wifi.py",
        description="Command-line client for PicoBoot-WiFi. Join the Pico's Wi-Fi first.")
    p.add_argument("--host", default=DEFAULT_HOST,
                   help="address of the Pico (default %(default)s). The Pico only answers to "
                        "192.168.4.1: every request carries the Host header of this address, and "
                        "the Pico refuses any other with 403 (another address only helps through "
                        "a forward to 192.168.4.1, or to a test server)")
    p.add_argument("--timeout", type=float, default=10, metavar="S",
                   help="seconds to wait for an answer (default %(default)s); the upload waits "
                        f"at least {UPLOAD_READ_TIMEOUT} s for the Pico to write flash")
    sub = p.add_subparsers(dest="command", required=True, metavar="COMMAND")
    s = sub.add_parser("status", help="show the firmware, slots, payloads, Wi-Fi and uptime")
    s.add_argument("--json", action="store_true", help="print the raw JSON")
    s.set_defaults(func=cmd_status)
    s = sub.add_parser("update", help="install an update file into the other slot; "
                                      "the Pico reboots into it on trial")
    s.add_argument("file")
    s.add_argument("--yes", "-y", action="store_true", help="don't ask for confirmation")
    s.add_argument("--no-wait", action="store_true", help="don't wait for the Pico to come back")
    s.set_defaults(func=cmd_update)
    s = sub.add_parser("keep", help="keep the running firmware that is on trial")
    s.set_defaults(func=cmd_keep)
    s = sub.add_parser("boot", help="boot a slot")
    s.add_argument("slot", choices=["A", "B"])
    s.add_argument("--no-wait", action="store_true", help="don't wait for the Pico to come back")
    s.set_defaults(func=cmd_boot)
    s = sub.add_parser("reboot", help="warm reboot (a running game keeps running)")
    s.add_argument("--no-wait", action="store_true", help="don't wait for the Pico to come back")
    s.set_defaults(func=cmd_reboot)
    s = sub.add_parser("payload", help="upload, choose, rename, delete or convert payloads")
    psub = s.add_subparsers(dest="payload_command", required=True, metavar="ACTION")
    slots = ["payload1", "payload2"]
    s = psub.add_parser("upload", help="upload a payload UF2, raw payload or .dol into the slot "
                                       "that isn't chosen; it becomes the choice")
    s.add_argument("file")
    s.add_argument("--name", help="name, up to 24 printable ASCII characters (default: from the file name)")
    s.add_argument("--yes", "-y", action="store_true", help="don't ask before replacing a payload")
    s.set_defaults(func=cmd_payload_upload)
    s = psub.add_parser("choose", help="choose what the next power-on injects")
    s.add_argument("payload", choices=slots + ["builtin"])
    s.set_defaults(func=cmd_payload_choose)
    s = psub.add_parser("rename", help="rename a payload")
    s.add_argument("slot", choices=slots)
    s.add_argument("name", help="up to 24 printable ASCII characters")
    s.set_defaults(func=cmd_payload_rename)
    s = psub.add_parser("delete", help="delete a payload that isn't chosen")
    s.add_argument("slot", choices=slots)
    s.add_argument("--yes", "-y", action="store_true", help="don't ask for confirmation")
    s.set_defaults(func=cmd_payload_delete)
    s = psub.add_parser("convert", help="convert a .dol into a raw payload file (no Pico needed)")
    s.add_argument("file")
    s.add_argument("-o", "--output", help="output file (default: the input's name with .bin)")
    s.add_argument("--force", action="store_true", help="overwrite an existing file")
    s.set_defaults(func=cmd_payload_convert)
    s = sub.add_parser("ipl-once", help="boot the console's own IPL at the next power-on, then go back")
    s.add_argument("--off", action="store_true", help="cancel it")
    s.set_defaults(func=cmd_ipl_once)
    s = sub.add_parser("backup", help="download a payload, the running firmware (an update file) "
                                      "or the settings, and check it")
    s.add_argument("what", choices=slots + ["firmware", "settings"])
    s.add_argument("-o", "--output", help="file name (default: the Pico's)")
    s.add_argument("--force", action="store_true", help="overwrite an existing file")
    s.set_defaults(func=cmd_backup)
    s = sub.add_parser("restore-settings", help="send a settings backup (JSON) to the Pico")
    s.add_argument("file")
    s.set_defaults(func=cmd_restore_settings)
    return p


def main(argv=None):
    try:
        args = make_parser().parse_args(argv)
    except SystemExit as e:
        return EXIT_USAGE if e.code else EXIT_OK
    try:
        args.func(args)
    except Cancelled:
        return EXIT_REFUSED
    except Refused as e:
        print(f"error: {e}", file=sys.stderr)
        return EXIT_REFUSED
    except Unreachable as e:
        print(f"error: {e}", file=sys.stderr)
        return EXIT_UNREACHABLE
    except KeyboardInterrupt:
        print(file=sys.stderr)
        return 130
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
