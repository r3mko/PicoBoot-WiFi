#!/usr/bin/env python3

"""
Command-line client for the PicoBoot-WiFi JSON API (docs/api.md). Join the Pico's Wi-Fi first.

    python3 tools/picoboot-wifi.py status [--json]
    python3 tools/picoboot-wifi.py update build/picoboot-wifi-update.bin [--yes] [--no-wait]
    python3 tools/picoboot-wifi.py keep
    python3 tools/picoboot-wifi.py boot A|B [--no-wait]
    python3 tools/picoboot-wifi.py reboot [--no-wait]

Exit codes: 0 ok, 1 refused by the Pico or a bad file, 2 usage, 3 can't reach the Pico.
Standard library only; the tool works when copied on its own.
"""

import argparse
import http.client
import json
import os
import re
import struct
import sys
import time

DEFAULT_HOST = "192.168.4.1"
MAGIC = b"PBWU"
UF2_MAGIC = b"UF2\n"
HEADER_SIZE = 256
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


def upload(args, path, size):
    """POST the file as application/octet-stream in chunks, with progress on stderr."""
    conn = http.client.HTTPConnection(args.host, timeout=args.timeout)
    try:
        conn.putrequest("POST", "/api/firmware")
        conn.putheader("Content-Type", "application/octet-stream")
        conn.putheader("Content-Length", str(size))
        conn.putheader("Connection", "close")
        conn.endheaders()
        sent = 0
        try:
            with open(path, "rb") as f:
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
    s = sub.add_parser("status", help="show the firmware, slots, Wi-Fi and uptime")
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
