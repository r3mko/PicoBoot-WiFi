"""Tests for tools/picoboot-wifi.py (the command-line client of docs/api.md), against a mock Pico."""

import contextlib
import hashlib
import http.server
import io
import json
import os
import socket
import struct
import tempfile
import threading
import time
import unittest
from unittest import mock

from helpers import REPO, load_tool, run_tool

cli = load_tool("picoboot-wifi")
mk = load_tool("mkupdate")
mkuf2 = load_tool("mkuf2")
ipl = load_tool("process_ipl")

IMAGE = bytes(range(256)) * 700
BLOB = b"W43B" + bytes(60) + b"blob data" * 1000

STATUS = {
    "api": 1,
    "firmware": {"version": "0.3.0-dev+1a2b3c4", "slot": "A", "loader": 2, "pending": None,
                 "states": {"A": "kept", "B": "failed"},
                 "versions": {"A": "0.3.0-dev+1a2b3c4", "B": None}},
    "chip": {"stepping": 2, "rom": 2},
    "uptime_s": 3725,
    "temperature_c": 38.4,
    "wifi": {"ssid": "PicoBoot-C2C4", "clients": 1, "password_is_default": True, "reverted": False},
}
TRIAL_STATUS = {**STATUS, "firmware": {**STATUS["firmware"], "version": "0.4.0", "slot": "B", "pending": "B"}}


class Mock:
    """A Pico on 127.0.0.1: records requests, answers from `routes` {(method, path): [(status, body), ...]}."""

    def __init__(self):
        self.requests = []
        self.routes = {}
        mock_ = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def handle_one(self):
                length = int(self.headers.get("Content-Length") or 0)
                body = self.rfile.read(length)
                mock_.requests.append({"method": self.command, "path": self.path,
                                       "headers": dict(self.headers), "body": body})
                answers = mock_.routes.get((self.command, self.path))
                if answers is None:
                    status, payload = 404, {"error": "not_found", "message": "no such endpoint"}
                else:
                    answer = answers.pop(0) if len(answers) > 1 else answers[0]
                    if answer == ("hang",):
                        time.sleep(1.0)     # Takes the body, never answers (the client times out)
                        return
                    status, payload, *extra = answer
                if isinstance(payload, bytes):      # A file download
                    data, content_type = payload, "application/octet-stream"
                else:
                    data, content_type = json.dumps(payload).encode(), "application/json"
                self.send_response(status)
                self.send_header("Content-Type", content_type)
                for name, value in (extra[0] if extra else {}).items():
                    self.send_header(name, value)
                self.send_header("Content-Length", str(len(data)))
                self.send_header("Connection", "close")
                self.end_headers()
                self.wfile.write(data)

            do_GET = do_POST = handle_one

        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.host = f"127.0.0.1:{self.server.server_address[1]}"
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def close(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()

    def route(self, method, path, *answers):
        self.routes[(method, path)] = list(answers)


def run(host, *argv, stdin=""):
    """Run main() in-process; returns (exit code, stdout, stderr)."""
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err), \
            mock.patch("sys.stdin", io.StringIO(stdin)):
        code = cli.main(["--host", host, *argv])
    return code, out.getvalue(), err.getvalue()


def free_port_host():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    host = f"127.0.0.1:{s.getsockname()[1]}"
    s.close()
    return host


class CliTestCase(unittest.TestCase):
    def setUp(self):
        self.pico = Mock()
        self.addCleanup(self.pico.close)
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        for name, value in (("POLL_INTERVAL", 0.01), ("FIRST_DELAY", 0), ("WAIT_TIMEOUT", 3)):
            patcher = mock.patch.object(cli, name, value)
            patcher.start()
            self.addCleanup(patcher.stop)

    def write(self, data, name="update.bin"):
        path = os.path.join(self.tmp.name, name)
        with open(path, "wb") as f:
            f.write(data)
        return path

    def update_file(self, version="0.4.0", board="pico2_w"):
        return self.write(mk.build_update(IMAGE, BLOB, version, board, True))

    def cli(self, *argv, **kw):
        return run(self.pico.host, *argv, **kw)


class VersionTest(unittest.TestCase):
    def test_compare(self):
        table = [
            ("0.4.0", "0.3.0", 1),
            ("0.3.1", "0.3.0", 1),
            ("1.0.0", "0.9.9", 1),
            ("0.10.0", "0.9.0", 1),                 # numeric, not text
            ("0.3.0+abc", "0.3.0+def", 0),          # the build is ignored
            ("0.3.0-dev+abc", "0.3.0-dev+def", 0),
            ("0.3.0", "0.3.0-dev+abc", 1),          # release after its -dev
            ("0.3.0-dev+abc", "0.3.0", -1),
            ("0.3.0-dev+abc", "0.2.9", 1),
            ("0.2.0", "0.3.0-dev+abc", -1),
            ("0.3.0-dev+9f8e7d6-crash-5s", "0.3.0-dev+1a2b3c4", 0),
            ("0.3.0-alpha", "0.3.0-beta", -1),
            ("garbage", "0.3.0", None),
        ]
        for a, b, expected in table:
            with self.subTest(a=a, b=b):
                self.assertEqual(cli.compare_versions(a, b), expected)


class StatusTest(CliTestCase):
    def test_summary(self):
        self.pico.route("GET", "/api/status", (200, STATUS))
        code, out, err = self.cli("status")
        self.assertEqual(code, 0, err)
        for text in ("0.3.0-dev+1a2b3c4", "slot A", "loader 2", "kept", "failed", "PicoBoot-C2C4",
                     "1 client", "default", "1 h 2 min 5 s", "38.4"):
            self.assertIn(text, out)
        req = self.pico.requests[0]
        self.assertEqual((req["method"], req["path"]), ("GET", "/api/status"))

    def test_pending_slot_is_marked(self):
        self.pico.route("GET", "/api/status", (200, TRIAL_STATUS))
        code, out, _ = self.cli("status")
        self.assertIn("(pending)", out)

    def test_json(self):
        self.pico.route("GET", "/api/status", (200, STATUS))
        code, out, _ = self.cli("status", "--json")
        self.assertEqual(code, 0)
        self.assertEqual(json.loads(out), STATUS)


class SimpleCommandsTest(CliTestCase):
    def assert_post(self, req, path, body):
        self.assertEqual((req["method"], req["path"]), ("POST", path))
        self.assertEqual(req["headers"]["Content-Type"], "application/json")
        self.assertEqual(req["headers"]["Host"], self.pico.host)
        self.assertEqual(json.loads(req["body"]), body)

    def test_keep(self):
        self.pico.route("POST", "/api/boot", (200, {"kept": True}))
        code, out, err = self.cli("keep")
        self.assertEqual(code, 0, err)
        self.assertEqual(len(self.pico.requests), 1)
        self.assert_post(self.pico.requests[0], "/api/boot", {"keep": True})

    def test_boot_waits_for_the_pico(self):
        self.pico.route("POST", "/api/boot", (200, {"rebooting": True}))
        self.pico.route("GET", "/api/status", (200, TRIAL_STATUS))
        code, out, err = self.cli("boot", "B")
        self.assertEqual(code, 0, err)
        self.assert_post(self.pico.requests[0], "/api/boot", {"slot": "B"})
        self.assertEqual(self.pico.requests[1]["path"], "/api/status")
        self.assertIn("Running slot B", out)
        self.assertIn("on trial", out)

    def test_boot_the_running_slot(self):
        self.pico.route("POST", "/api/boot", (200, {"rebooting": False}))
        code, out, _ = self.cli("boot", "A")
        self.assertEqual(code, 0)
        self.assertEqual(len(self.pico.requests), 1)
        self.assertIn("already", out)

    def test_boot_no_wait(self):
        self.pico.route("POST", "/api/boot", (200, {"rebooting": True}))
        code, _, _ = self.cli("boot", "A", "--no-wait")
        self.assertEqual(code, 0)
        self.assertEqual(len(self.pico.requests), 1)

    def test_boot_bad_slot_is_usage(self):
        code, _, err = self.cli("boot", "C")
        self.assertEqual(code, 2)
        self.assertEqual(self.pico.requests, [])

    def test_reboot_then_wait(self):
        self.pico.route("POST", "/api/reboot", (200, {"rebooting": True}))
        self.pico.route("GET", "/api/status", (200, STATUS))
        code, out, err = self.cli("reboot")
        self.assertEqual(code, 0, err)
        self.assert_post(self.pico.requests[0], "/api/reboot", {})
        self.assertEqual(self.pico.requests[1]["path"], "/api/status")
        self.assertIn("Running slot A", out)

    def test_reboot_no_wait(self):
        self.pico.route("POST", "/api/reboot", (200, {"rebooting": True}))
        code, _, _ = self.cli("reboot", "--no-wait")
        self.assertEqual(code, 0)
        self.assertEqual(len(self.pico.requests), 1)

    def test_wait_survives_503_and_refused_connections(self):
        self.pico.route("POST", "/api/reboot", (200, {"rebooting": True}))
        self.pico.route("GET", "/api/status", (503, {"error": "busy", "message": "busy"}),
                        (503, {"error": "busy", "message": "busy"}), (200, STATUS))
        code, out, err = self.cli("reboot")
        self.assertEqual(code, 0, err)
        self.assertEqual(len([r for r in self.pico.requests if r["path"] == "/api/status"]), 3)

    def test_wait_over_a_closed_port(self):
        # The Pico is down: connections are refused until the wait runs out
        self.pico.route("POST", "/api/reboot", (200, {"rebooting": True}))
        real = cli.request
        calls = []

        def flaky(args, method, path, body=None, timeout=None):
            if path == "/api/status":
                calls.append(1)
                if len(calls) < 3:
                    raise cli.Unreachable("refused")
            return real(args, method, path, body, timeout)

        self.pico.route("GET", "/api/status", (200, STATUS))
        with mock.patch.object(cli, "request", flaky):
            code, out, err = self.cli("reboot")
        self.assertEqual(code, 0, err)
        self.assertEqual(len(calls), 3)

    def test_wait_gives_up(self):
        self.pico.route("POST", "/api/reboot", (200, {"rebooting": True}))
        self.pico.route("GET", "/api/status", (503, {"error": "busy", "message": "busy"}))
        with mock.patch.object(cli, "WAIT_TIMEOUT", 0.1):
            code, _, err = self.cli("reboot")
        self.assertEqual(code, 3)
        self.assertIn("didn't answer", err)


class ErrorsTest(CliTestCase):
    def test_refusal_prints_the_message(self):
        self.pico.route("POST", "/api/boot", (409, {"error": "not_on_trial", "message": "the running firmware is not on trial"}))
        code, out, err = self.cli("keep")
        self.assertEqual(code, 1)
        self.assertIn("the running firmware is not on trial", err)

    def test_not_bootable(self):
        self.pico.route("POST", "/api/boot", (409, {"error": "not_bootable", "message": "slot B is empty"}))
        code, _, err = self.cli("boot", "B")
        self.assertEqual(code, 1)
        self.assertIn("slot B is empty", err)

    def test_forbidden_without_json_body(self):
        self.pico.route("POST", "/api/reboot", (403, {}))
        code, _, err = self.cli("reboot")
        self.assertEqual(code, 1)
        self.assertIn("403", err)

    def test_unreachable(self):
        for argv in (["status"], ["keep"], ["reboot"], ["boot", "A"]):
            with self.subTest(argv=argv):
                code, _, err = run(free_port_host(), *argv)
                self.assertEqual(code, 3)
                self.assertIn("can't reach", err)

    def test_no_command_is_usage(self):
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(cli.main([]), 2)

    def test_help_mentions_the_host_rule(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(cli.main(["--help"]), 0)
        self.assertIn("192.168.4.1", out.getvalue())
        self.assertIn("Host", out.getvalue())

    def test_script_runs(self):
        result = run_tool("picoboot-wifi", "--help")
        self.assertEqual(result.returncode, 0)
        self.assertIn("COMMAND", result.stdout)


class UpdateTest(CliTestCase):
    INSTALLED = {"installed": True, "slot": "B", "version": "0.4.0", "blob_written": False, "rebooting": True}

    def setUp(self):
        super().setUp()
        self.pico.route("GET", "/api/status", (200, STATUS))
        self.pico.route("POST", "/api/firmware", (200, self.INSTALLED))

    def uploads(self):
        return [r for r in self.pico.requests if r["path"] == "/api/firmware"]

    def test_upload_is_exactly_the_file(self):
        self.pico.route("GET", "/api/status", (200, STATUS), (200, TRIAL_STATUS))
        path = self.update_file()
        code, out, err = self.cli("update", path, "--yes")
        self.assertEqual(code, 0, err)
        up = self.uploads()[0]
        with open(path, "rb") as f:
            self.assertEqual(up["body"], f.read())
        self.assertEqual(up["method"], "POST")
        self.assertEqual(up["headers"]["Content-Type"], "application/octet-stream")
        self.assertEqual(up["headers"]["Host"], self.pico.host)
        self.assertEqual(int(up["headers"]["Content-Length"]), len(up["body"]))
        self.assertIn("Uploading 100%", err)
        self.assertIn("Installed 0.4.0 in slot B", out)

    def test_waits_and_prints_the_trial(self):
        self.pico.route("GET", "/api/status", (200, STATUS), (503, {"error": "busy", "message": "x"}),
                        (200, TRIAL_STATUS))
        code, out, err = self.cli("update", self.update_file(), "--yes")
        self.assertEqual(code, 0, err)
        self.assertIn("Running slot B, version 0.4.0, on trial", out)

    def test_a_new_firmware_that_crashed_is_reported(self):
        # Phase 3 review: after the reboot the Pico is back on the old slot (the trial failed)
        self.pico.route("GET", "/api/status", (200, STATUS))
        code, out, err = self.cli("update", self.update_file(), "--yes")
        self.assertEqual(code, 1)
        self.assertIn("didn't come up", err)
        self.assertIn("slot A", err)

    def test_a_lost_answer_is_checked(self):
        # Phase 3 bench: the whole file went in, but the answer never came back
        patcher = mock.patch.object(cli, "UPLOAD_READ_TIMEOUT", 0.3)
        patcher.start()
        self.addCleanup(patcher.stop)
        self.pico.route("POST", "/api/firmware", ("hang",))
        self.pico.route("GET", "/api/status", (200, STATUS), (200, TRIAL_STATUS))
        code, out, err = self.cli("--timeout", "0.5", "update", self.update_file(), "--yes")
        self.assertEqual(code, 0, err)
        self.assertIn("checking what happened", err)
        self.assertIn("went through", out)
        self.pico.route("POST", "/api/firmware", ("hang",))
        self.pico.route("GET", "/api/status", (200, STATUS),
                        (200, {**STATUS, "firmware": {**STATUS["firmware"], "states": {"A": "kept", "B": "empty"}}}))
        code, out, err = self.cli("--timeout", "0.5", "update", self.update_file(), "--yes")
        self.assertEqual(code, 1)
        self.assertIn("slot B is empty", err)

    def test_no_wait(self):
        code, out, _ = self.cli("update", self.update_file(), "--yes", "--no-wait")
        self.assertEqual(code, 0)
        self.assertEqual([r["path"] for r in self.pico.requests], ["/api/status", "/api/firmware"])

    def test_prompt_yes_and_no(self):
        self.pico.route("GET", "/api/status", (200, STATUS), (200, TRIAL_STATUS))
        path = self.update_file()
        code, out, err = self.cli("update", path, stdin="y\n")
        self.assertEqual(code, 0, err)
        self.assertEqual(len(self.uploads()), 1)
        self.pico.requests.clear()
        for answer in ("n\n", "\n", ""):
            code, out, err = self.cli("update", path, stdin=answer)
            self.assertEqual(code, 1)
            self.assertIn("Cancelled", out)
        self.assertEqual(self.uploads(), [])

    def test_warnings(self):
        table = [
            ("0.5.0", "Install it?"),
            ("0.3.0-dev+other", "already running"),
            ("0.2.0", "older version"),
            ("0.3.0", "Install it?"),       # the release is newer than 0.3.0-dev
        ]
        for version, text in table:
            with self.subTest(version=version):
                code, out, err = self.cli("update", self.update_file(version), stdin="n\n")
                self.assertIn(text, err)
        self.pico.route("GET", "/api/status", (200, {**STATUS, "firmware": {**STATUS["firmware"], "version": "0.3.0"}}))
        code, out, err = self.cli("update", self.update_file("0.3.0-dev+abc"), stdin="n\n")
        self.assertIn("older version", err)

    def test_local_checks_refuse_before_any_request(self):
        good = mk.build_update(IMAGE, BLOB, "0.4.0", "pico2_w", True)
        bad = {
            "uf2": (b"UF2\n" + bytes(1000), "UF2"),
            "board": (mk.build_update(IMAGE, BLOB, "0.4.0", "pico_w", True), "pico_w"),
            "short": (good[:-1], "cut short"),
            "long": (good + b"x", "cut short"),
            "not an update": (b"hello" * 100, "not a PicoBoot-WiFi update"),
            "tiny": (b"PBWU", "not a PicoBoot-WiFi update"),
            "empty": (b"", "not a PicoBoot-WiFi update"),
        }
        for name, (data, text) in bad.items():
            with self.subTest(name=name):
                code, _, err = self.cli("update", self.write(data), "--yes")
                self.assertEqual(code, 1)
                self.assertIn(text, err)
        code, _, err = self.cli("update", os.path.join(self.tmp.name, "missing.bin"), "--yes")
        self.assertEqual(code, 1)
        self.assertEqual(self.pico.requests, [])

    def test_on_trial_is_explained(self):
        self.pico.route("POST", "/api/firmware", (409, {"error": "on_trial", "message": "the running firmware is on trial"}))
        code, _, err = self.cli("update", self.update_file(), "--yes")
        self.assertEqual(code, 1)
        self.assertIn("on trial", err)
        self.assertIn("keep", err)

    def test_refusal_from_the_pico(self):
        self.pico.route("POST", "/api/firmware", (400, {"error": "needs_usb_install", "message": "needs a newer loader"}))
        code, _, err = self.cli("update", self.update_file(), "--yes")
        self.assertEqual(code, 1)
        self.assertIn("needs a newer loader", err)

    def test_unreachable(self):
        code, _, err = run(free_port_host(), "update", self.update_file(), "--yes")
        self.assertEqual(code, 3)


def synthetic_dol(entry=0x81300000, base=0x81300000, text=bytes(range(200)) * 3):
    """A one-section DOL with the IPL's addresses (or others)."""
    header = [0x100] + [0] * 17 + [base] + [0] * 17 + [len(text)] + [0] * 17 + [0, 0, entry] + [0] * 7
    return struct.pack(">64I", *header) + text


def payload_status(chosen="payload1", target="payload2", p1=None, p2=None, once=False):
    empty = {"valid": False, "name": "", "size": 0, "sha256": None}
    return {**STATUS, "payloads": {"chosen": chosen, "upload_target": target, "original_ipl_once": once,
                                   "usb_install": False, "payload1": p1 or empty, "payload2": p2 or empty}}


def slot_of(payload, name):
    return {"valid": True, "name": name, "size": len(payload), "sha256": hashlib.sha256(payload).hexdigest()}


PAYLOAD = ipl.dol_to_payload(synthetic_dol())
PAYLOAD_SHA = hashlib.sha256(PAYLOAD).hexdigest()


def payload_uf2(payload, *families):
    region = [("payload", 0x80000, payload, 0x180000)]
    return b"".join(mkuf2.build_uf2(region, family) for family in families)


class NameRuleTest(unittest.TestCase):
    def test_examples(self):
        table = [
            ("swiss_r2119.dol", "Swiss r2119"),
            ("gekkoboot_pico.uf2", "Gekkoboot pico"),
            ("/some/dir/my__game  v2.1.tar.bin", "My game v2.1.tar"),   # only the last extension goes
            ("  _x_  .bin", "X"),
            ("café_menu.dol", "Caf menu"),                         # Not printable ASCII: dropped
            ("élan.dol", "Lan"),
            ("äö.dol", ""),
            ("a" * 30 + ".dol", "A" + "a" * 23),
            ("abcdefghijklmnopqrstuvw_xyz.dol", "Abcdefghijklmnopqrstuvw"),     # cut at 24: no trailing space
            ("abcdefghijklmnopqrstuv_wxyz.dol", "Abcdefghijklmnopqrstuv w"),
            ("abcdefghijklmnopqrstuvw__x.dol", "Abcdefghijklmnopqrstuvw"),
        ]
        for path, expected in table:
            with self.subTest(path=path):
                self.assertEqual(cli.name_from_file(path), expected)

    def test_check_name(self):
        self.assertEqual(cli.check_name("Swiss r2119"), "Swiss r2119")
        self.assertEqual(cli.check_name(""), "")
        self.assertEqual(cli.check_name("a" * 24), "a" * 24)
        for bad in ("a" * 25, "café", "tab\there", "new\nline"):
            with self.subTest(bad=bad), self.assertRaises(cli.Refused):
                cli.check_name(bad)


class ConverterCopyTest(unittest.TestCase):
    """The CLI carries a copy of tools/process_ipl.py's converter; the copy must stay identical."""

    def test_bundled_ipl(self):
        with open(os.path.join(REPO, "assets", "ipl", "gekkoboot_r9_4.dol"), "rb") as f:
            dol = f.read()
        self.assertEqual(cli.dol_to_payload(dol), ipl.dol_to_payload(dol))
        self.assertEqual(len(cli.dol_to_payload(dol)), 60548)       # As gekkoboot_pico.uf2 holds it

    def test_synthetic_dols(self):
        for text in (b"\0", b"x", bytes(range(256)) * 5, b"abc" * 1001):
            with self.subTest(size=len(text)):
                dol = synthetic_dol(text=text)
                self.assertEqual(cli.dol_to_payload(dol), ipl.dol_to_payload(dol))
        self.assertEqual(cli.scramble(bytearray(range(100))), ipl.scramble(bytearray(range(100))))

    def test_the_same_errors(self):
        for dol in (synthetic_dol(entry=0x80003100), synthetic_dol(base=0x80003100), b"\0" * 10,
                    bytes(256), synthetic_dol()[:-5]):
            with self.subTest(size=len(dol)):
                with self.assertRaises(ValueError) as want:
                    ipl.dol_to_payload(dol)
                with self.assertRaises(ValueError) as got:
                    cli.dol_to_payload(dol)
                self.assertEqual(str(got.exception), str(want.exception))


class PayloadStatusTest(CliTestCase):
    def test_status_shows_the_payloads(self):
        st = payload_status(p1=slot_of(PAYLOAD, "Swiss r2119"), once=True)
        self.pico.route("GET", "/api/status", (200, st))
        code, out, err = self.cli("status")
        self.assertEqual(code, 0, err)
        for text in ("payload 1 chosen", "original IPL once: yes", "Payload 1: Swiss r2119", "KB",
                     "Payload 2: empty, next upload goes here"):
            self.assertIn(text, out)

    def test_status_without_payloads_is_as_before(self):
        self.pico.route("GET", "/api/status", (200, STATUS))
        code, out, err = self.cli("status")
        self.assertEqual(code, 0, err)
        self.assertNotIn("Payload", out)


class PayloadFileTest(CliTestCase):
    def test_formats(self):
        uf2 = payload_uf2(PAYLOAD, 0xe48bff56, 0xe48bff59)
        for name, data, sent, kind in (("p.bin", PAYLOAD, PAYLOAD, "raw"), ("p.uf2", uf2, uf2, "UF2"),
                                       ("p.dol", synthetic_dol(), PAYLOAD, "converted"),
                                       ("dump.bin", PAYLOAD + b"\xff" * 4096, PAYLOAD, "without the padding")):
            with self.subTest(name=name):
                body, payload, how = cli.load_payload(self.write(data, name))
                self.assertEqual((body, payload), (sent, PAYLOAD))
                self.assertIn(kind, how)

    def test_refused_locally(self):
        usb = mkuf2.build_uf2([("slot A", 0x200000, bytes(range(256)) * 16, 0x40000)])
        uf2 = payload_uf2(PAYLOAD, 0xe48bff56)
        bad = {
            "notes.txt": (b"hello" * 100, "not a payload"),
            "empty.bin": (b"", "not a payload"),
            "install.uf2": (usb, "not a payload UF2"),
            "damaged.uf2": (uf2[:-7], "UF2 blocks"),
            "cut.bin": (PAYLOAD[:-8], "cut short"),
            "footer.bin": (PAYLOAD[:-1] + b"X", "footer"),
            "size.bin": (PAYLOAD[:8] + b"\0\0\0\x10" + PAYLOAD[12:], "size"),
            "not_ipl.dol": (synthetic_dol(entry=0x80003100), "Invalid entry point"),
            "short.dol": (b"\0" * 10, "DOL header"),
        }
        for name, (data, text) in bad.items():
            with self.subTest(name=name):
                code, _, err = self.cli("payload", "upload", self.write(data, name), "--yes")
                self.assertEqual(code, 1)
                self.assertIn(text, err)
        self.assertEqual(self.pico.requests, [])


class PayloadUploadTest(CliTestCase):
    ANSWER = {"installed": True, "slot": "payload2", "name": "Gekkoboot pico", "size": len(PAYLOAD),
              "sha256": PAYLOAD_SHA}

    def setUp(self):
        super().setUp()
        self.pico.route("GET", "/api/status", (200, payload_status(p1=slot_of(b"old", "Swiss r2119"))))
        self.pico.route("POST", "/api/payload", (200, self.ANSWER))

    def uploads(self):
        return [r for r in self.pico.requests if r["path"] == "/api/payload"]

    def test_raw_upload(self):
        path = self.write(PAYLOAD, "gekkoboot_pico.bin")
        code, out, err = self.cli("payload", "upload", path)
        self.assertEqual(code, 0, err)
        up = self.uploads()[0]
        self.assertEqual(up["body"], PAYLOAD)
        self.assertEqual(up["headers"]["Content-Type"], "application/octet-stream")
        self.assertEqual(up["headers"]["X-Name"], "Gekkoboot pico")
        self.assertEqual(up["headers"]["Host"], self.pico.host)
        self.assertEqual(int(up["headers"]["Content-Length"]), len(PAYLOAD))
        self.assertIn("Uploading into payload 2; payload 1 stays as it is", out)
        self.assertIn(PAYLOAD_SHA, out)
        self.assertIn("Uploading 100%", err)

    def test_uf2_is_sent_as_it_is(self):
        uf2 = payload_uf2(PAYLOAD, 0xe48bff56, 0xe48bff59)
        code, out, err = self.cli("payload", "upload", self.write(uf2, "swiss_r2119.uf2"), "--name", "Mine")
        self.assertEqual(code, 0, err)
        self.assertEqual(self.uploads()[0]["body"], uf2)
        self.assertEqual(self.uploads()[0]["headers"]["X-Name"], "Mine")
        self.assertIn(PAYLOAD_SHA, out)

    def test_dol_is_converted(self):
        code, out, err = self.cli("payload", "upload", self.write(synthetic_dol(), "swiss_r2119.dol"), "--yes")
        self.assertEqual(code, 0, err)
        self.assertEqual(self.uploads()[0]["body"], PAYLOAD)
        self.assertEqual(self.uploads()[0]["headers"]["X-Name"], "Swiss r2119")

    def test_the_target_comes_from_the_status(self):
        self.pico.route("GET", "/api/status", (200, payload_status("payload2", "payload1")))
        code, out, err = self.cli("payload", "upload", self.write(PAYLOAD, "a.bin"))
        self.assertEqual(code, 0, err)
        self.assertIn("into payload 1; payload 2 stays", out)

    def test_replacing_asks(self):
        self.pico.route("GET", "/api/status", (200, payload_status("payload1", "payload2",
                                                                 p2=slot_of(b"old", "Old one"))))
        path = self.write(PAYLOAD, "a.bin")
        code, out, err = self.cli("payload", "upload", path, stdin="n\n")
        self.assertEqual(code, 1)
        self.assertIn("Old one", err)
        self.assertEqual(self.uploads(), [])
        code, out, err = self.cli("payload", "upload", path, stdin="y\n")
        self.assertEqual(code, 0, err)
        self.assertEqual(len(self.uploads()), 1)

    def test_bad_name_before_any_request(self):
        code, _, err = self.cli("payload", "upload", self.write(PAYLOAD, "a.bin"), "--name", "x" * 25)
        self.assertEqual(code, 1)
        self.assertIn("24", err)
        self.assertEqual(self.pico.requests, [])

    def test_a_different_sha_in_the_answer_is_an_error(self):
        self.pico.route("POST", "/api/payload", (200, {**self.ANSWER, "sha256": "0" * 64}))
        code, _, err = self.cli("payload", "upload", self.write(PAYLOAD, "a.bin"))
        self.assertEqual(code, 1)
        self.assertIn("not the one sent", err)

    def test_refusals_from_the_pico(self):
        self.pico.route("POST", "/api/payload", (400, {"error": "too_large", "message": "the payload is too large"}))
        code, _, err = self.cli("payload", "upload", self.write(PAYLOAD, "a.bin"))
        self.assertEqual(code, 1)
        self.assertIn("too large", err)
        self.pico.route("POST", "/api/payload", (503, {"error": "busy", "message": "a transfer is running"}))
        code, _, err = self.cli("payload", "upload", self.write(PAYLOAD, "a.bin"))
        self.assertEqual(code, 1)
        self.assertIn("a transfer is running", err)

    def test_a_lost_answer_is_checked(self):
        patcher = mock.patch.object(cli, "UPLOAD_READ_TIMEOUT", 0.3)
        patcher.start()
        self.addCleanup(patcher.stop)
        uf2 = payload_uf2(PAYLOAD, 0xe48bff56)
        for name, data in (("a.bin", PAYLOAD), ("a.uf2", uf2), ("a.dol", synthetic_dol())):
            with self.subTest(name=name):
                self.pico.route("POST", "/api/payload", ("hang",))
                self.pico.route("GET", "/api/status", (200, payload_status()),
                                (200, payload_status("payload2", "payload1", p2=slot_of(PAYLOAD, "A"))))
                code, out, err = self.cli("--timeout", "0.5", "payload", "upload", self.write(data, name), "--yes")
                self.assertEqual(code, 0, err)
                self.assertIn("checking what happened", err)
                self.assertIn("went through", out)
                self.assertIn(PAYLOAD_SHA, out)

    def test_a_lost_answer_and_nothing_there(self):
        patcher = mock.patch.object(cli, "UPLOAD_READ_TIMEOUT", 0.3)
        patcher.start()
        self.addCleanup(patcher.stop)
        for after in (payload_status(), payload_status("payload1", "payload2", p2=slot_of(b"other", "Other"))):
            self.pico.route("POST", "/api/payload", ("hang",))
            self.pico.route("GET", "/api/status", (200, payload_status()), (200, after))
            code, _, err = self.cli("--timeout", "0.5", "payload", "upload", self.write(PAYLOAD, "a.bin"), "--yes")
            self.assertEqual(code, 1)
            self.assertIn("didn't go through", err)

    def test_unreachable(self):
        code, _, err = run(free_port_host(), "payload", "upload", self.write(PAYLOAD, "a.bin"), "--yes")
        self.assertEqual(code, 3)

    def test_old_firmware_without_payloads(self):
        self.pico.route("GET", "/api/status", (200, STATUS))
        code, _, err = self.cli("payload", "upload", self.write(PAYLOAD, "a.bin"), "--yes")
        self.assertEqual(code, 1)
        self.assertIn("no payload support", err)


class PayloadCommandsTest(CliTestCase):
    def assert_post(self, path, body):
        req = self.pico.requests[-1]
        self.assertEqual((req["method"], req["path"]), ("POST", path))
        self.assertEqual(req["headers"]["Content-Type"], "application/json")
        self.assertEqual(json.loads(req["body"]), body)

    def test_choose(self):
        self.pico.route("POST", "/api/boot", (200, {"payload": "builtin"}))
        code, out, err = self.cli("payload", "choose", "builtin")
        self.assertEqual(code, 0, err)
        self.assert_post("/api/boot", {"payload": "builtin"})
        self.pico.route("POST", "/api/boot", (409, {"error": "not_valid", "message": "payload 2 holds no valid payload"}))
        code, _, err = self.cli("payload", "choose", "payload2")
        self.assertEqual(code, 1)
        self.assertIn("holds no valid payload", err)
        self.assertEqual(self.cli("payload", "choose", "payload3")[0], 2)

    def test_rename(self):
        self.pico.route("POST", "/api/payload/name", (200, {"slot": "payload1", "name": "Swiss"}))
        code, out, err = self.cli("payload", "rename", "payload1", "Swiss")
        self.assertEqual(code, 0, err)
        self.assert_post("/api/payload/name", {"slot": "payload1", "name": "Swiss"})
        count = len(self.pico.requests)
        code, _, err = self.cli("payload", "rename", "payload1", "x" * 25)
        self.assertEqual(code, 1)
        self.assertEqual(len(self.pico.requests), count)

    def test_delete(self):
        self.pico.route("POST", "/api/payload/delete", (200, {"slot": "payload2", "deleted": True}))
        code, out, err = self.cli("payload", "delete", "payload2", "--yes")
        self.assertEqual(code, 0, err)
        self.assert_post("/api/payload/delete", {"slot": "payload2"})
        self.pico.requests.clear()
        code, out, err = self.cli("payload", "delete", "payload2", stdin="n\n")
        self.assertEqual(code, 1)
        self.assertEqual(self.pico.requests, [])
        code, out, err = self.cli("payload", "delete", "payload2", stdin="y\n")
        self.assertEqual(code, 0, err)

    def test_delete_the_chosen_one(self):
        self.pico.route("POST", "/api/payload/delete",
                        (409, {"error": "chosen", "message": "payload 1 is the choice; choose another first"}))
        code, _, err = self.cli("payload", "delete", "payload1", "--yes")
        self.assertEqual(code, 1)
        self.assertIn("is the choice", err)

    def test_ipl_once(self):
        self.pico.route("POST", "/api/boot", (200, {"original_ipl_once": True}))
        code, out, err = self.cli("ipl-once")
        self.assertEqual(code, 0, err)
        self.assert_post("/api/boot", {"original_ipl_once": True})
        code, out, err = self.cli("ipl-once", "--off")
        self.assertEqual(code, 0, err)
        self.assert_post("/api/boot", {"original_ipl_once": False})

    def test_convert_needs_no_pico(self):
        path = self.write(synthetic_dol(), "swiss_r2119.dol")
        code, out, err = run(free_port_host(), "payload", "convert", path)
        self.assertEqual(code, 0, err)
        with open(os.path.join(self.tmp.name, "swiss_r2119.bin"), "rb") as f:
            self.assertEqual(f.read(), PAYLOAD)
        self.assertIn(PAYLOAD_SHA, out)

    def test_convert_does_not_overwrite(self):
        path = self.write(synthetic_dol(), "a.dol")
        target = self.write(b"mine", "a.bin")
        code, _, err = self.cli("payload", "convert", path)
        self.assertEqual(code, 1)
        self.assertIn("--force", err)
        with open(target, "rb") as f:
            self.assertEqual(f.read(), b"mine")
        code, _, err = self.cli("payload", "convert", path, "--force")
        self.assertEqual(code, 0, err)
        with open(target, "rb") as f:
            self.assertEqual(f.read(), PAYLOAD)
        other = os.path.join(self.tmp.name, "other.bin")
        self.assertEqual(self.cli("payload", "convert", path, "-o", other)[0], 0)
        self.assertTrue(os.path.exists(other))

    def test_convert_refuses_a_non_ipl_dol(self):
        path = self.write(synthetic_dol(entry=0x80003100), "game.dol")
        code, _, err = self.cli("payload", "convert", path)
        self.assertEqual(code, 1)
        self.assertIn("Invalid entry point", err)
        self.assertFalse(os.path.exists(os.path.join(self.tmp.name, "game.bin")))


class BackupTest(CliTestCase):
    def disposition(self, name):
        return {"Content-Disposition": f'attachment; filename="{name}"'}

    def setUp(self):
        super().setUp()
        self.cwd = os.getcwd()
        os.chdir(self.tmp.name)
        self.addCleanup(os.chdir, self.cwd)

    def read(self, name):
        with open(os.path.join(self.tmp.name, name), "rb") as f:
            return f.read()

    def test_payload(self):
        self.pico.route("GET", "/api/status", (200, payload_status(p1=slot_of(PAYLOAD, "Swiss r2119"))))
        self.pico.route("GET", "/api/backup/payload1", (200, PAYLOAD, self.disposition("Swiss r2119.bin")))
        code, out, err = self.cli("backup", "payload1")
        self.assertEqual(code, 0, err)
        self.assertEqual(self.read("Swiss r2119.bin"), PAYLOAD)
        self.assertIn(PAYLOAD_SHA, out)
        self.assertEqual(self.pico.requests[-1]["headers"]["Host"], self.pico.host)

    def test_only_the_base_name_of_the_header_is_used(self):
        self.pico.route("GET", "/api/status", (200, payload_status(p1=slot_of(PAYLOAD, "x"))))
        for evil in ("../../evil.bin", "/tmp/evil.bin", "..\\..\\evil.bin"):
            with self.subTest(evil=evil):
                self.pico.route("GET", "/api/backup/payload1", (200, PAYLOAD, self.disposition(evil)))
                code, out, err = self.cli("backup", "payload1", "--force")
                self.assertEqual(code, 0, err)
                self.assertEqual(self.read("evil.bin"), PAYLOAD)
        self.assertEqual(os.listdir(self.tmp.name), ["evil.bin"])

    def test_output_overrides_the_name(self):
        self.pico.route("GET", "/api/status", (200, payload_status(p1=slot_of(PAYLOAD, "x"))))
        self.pico.route("GET", "/api/backup/payload1", (200, PAYLOAD, self.disposition("x.bin")))
        out_path = os.path.join(self.tmp.name, "mine.bin")
        code, _, err = self.cli("backup", "payload1", "-o", out_path)
        self.assertEqual(code, 0, err)
        self.assertEqual(self.read("mine.bin"), PAYLOAD)
        self.assertFalse(os.path.exists(os.path.join(self.tmp.name, "x.bin")))

    def test_a_wrong_sha256_is_not_saved(self):
        self.pico.route("GET", "/api/status", (200, payload_status(p1=slot_of(PAYLOAD, "x"))))
        damaged = bytearray(PAYLOAD)
        damaged[100] ^= 1
        self.pico.route("GET", "/api/backup/payload1", (200, bytes(damaged), self.disposition("x.bin")))
        code, _, err = self.cli("backup", "payload1")
        self.assertEqual(code, 1)
        self.assertIn("doesn't match", err)
        self.assertEqual(os.listdir(self.tmp.name), [])

    def test_an_empty_slot_and_busy(self):
        self.pico.route("GET", "/api/status", (200, payload_status()))
        self.pico.route("GET", "/api/backup/payload2", (409, {"error": "not_valid", "message": "payload 2 is empty"}))
        code, _, err = self.cli("backup", "payload2")
        self.assertEqual(code, 1)
        self.assertIn("payload 2 is empty", err)
        self.pico.route("GET", "/api/backup/settings", (503, {"error": "busy", "message": "an upload is running"}))
        code, _, err = self.cli("backup", "settings")
        self.assertEqual(code, 1)
        self.assertIn("an upload is running", err)
        self.assertEqual(os.listdir(self.tmp.name), [])

    def test_firmware(self):
        update = mk.build_update(IMAGE, BLOB, "0.4.0", "pico2_w", True)
        self.pico.route("GET", "/api/backup/firmware", (200, update, self.disposition("picoboot-wifi-0.4.0-update.bin")))
        code, out, err = self.cli("backup", "firmware")
        self.assertEqual(code, 0, err)
        self.assertEqual(self.read("picoboot-wifi-0.4.0-update.bin"), update)
        self.assertIn("0.4.0", out)

    def test_a_damaged_update_file_is_not_saved(self):
        update = mk.build_update(IMAGE, BLOB, "0.4.0", "pico2_w", True)

        def flip(offset):
            data = bytearray(update)
            data[offset] ^= 1
            return bytes(data)

        bad = {
            "image": (flip(300), "image SHA-256"),
            "blob": (flip(len(update) - 5), "blob SHA-256"),
            "header": (flip(60), "header SHA-256"),
            "short": (update[:-1], "bytes, its header says"),
            "long": (update + b"x", "bytes, its header says"),
            "not an update": (b"hello" * 100, "not a PicoBoot-WiFi update"),
        }
        for name, (data, text) in bad.items():
            with self.subTest(name=name):
                self.pico.route("GET", "/api/backup/firmware", (200, data, self.disposition("f.bin")))
                code, _, err = self.cli("backup", "firmware")
                self.assertEqual(code, 1)
                self.assertIn(text, err)
        self.assertEqual(os.listdir(self.tmp.name), [])

    def test_settings(self):
        settings = {"ssid": "", "country": "XX", "channel": 6, "tx_power": "medium", "auto_off_minutes": 10, "led": True}
        self.pico.route("GET", "/api/backup/settings",
                        (200, json.dumps(settings).encode(), self.disposition("picoboot-wifi-settings.json")))
        code, out, err = self.cli("backup", "settings")
        self.assertEqual(code, 0, err)
        self.assertEqual(json.loads(self.read("picoboot-wifi-settings.json")), settings)
        for body in (b"not json {", b"[1, 2]"):
            self.pico.route("GET", "/api/backup/settings", (200, body, self.disposition("s.json")))
            code, _, err = self.cli("backup", "settings", "--force")
            self.assertEqual(code, 1)
            self.assertIn("not a JSON object", err)

    def test_no_overwrite_without_force(self):
        settings = b'{"led": true}'
        self.pico.route("GET", "/api/backup/settings", (200, settings, self.disposition("picoboot-wifi-settings.json")))
        existing = self.write(b"mine", "picoboot-wifi-settings.json")
        code, _, err = self.cli("backup", "settings")
        self.assertEqual(code, 1)
        self.assertIn("--force", err)
        self.assertEqual(self.read("picoboot-wifi-settings.json"), b"mine")
        self.pico.requests.clear()
        code, _, err = self.cli("backup", "settings", "-o", existing)
        self.assertEqual(code, 1)
        self.assertEqual(self.pico.requests, [])        # -o is checked before the download
        code, _, err = self.cli("backup", "settings", "--force")
        self.assertEqual(code, 0, err)
        self.assertEqual(self.read("picoboot-wifi-settings.json"), settings)

    def test_without_content_disposition(self):
        self.pico.route("GET", "/api/backup/settings", (200, b"{}"))
        code, _, err = self.cli("backup", "settings")
        self.assertEqual(code, 0, err)
        self.assertEqual(self.read("picoboot-wifi-settings.json"), b"{}")

    def test_unreachable(self):
        code, _, err = run(free_port_host(), "backup", "settings", "-o", os.path.join(self.tmp.name, "s.json"))
        self.assertEqual(code, 3)


class RestoreSettingsTest(CliTestCase):
    def test_restore(self):
        settings = {"ssid": "Mine", "channel": 11}
        self.pico.route("POST", "/api/settings", (200, {"saved": True, "wifi_restart": True, "ssid_in_use": "Mine"}))
        code, out, err = self.cli("restore-settings", self.write(json.dumps(settings).encode(), "s.json"))
        self.assertEqual(code, 0, err)
        req = self.pico.requests[0]
        self.assertEqual((req["method"], req["path"]), ("POST", "/api/settings"))
        self.assertEqual(req["headers"]["Content-Type"], "application/json")
        self.assertEqual(json.loads(req["body"]), settings)
        self.assertIn("restarts", out)
        self.assertIn("Mine", out)

    def test_no_restart(self):
        self.pico.route("POST", "/api/settings", (200, {"saved": True, "wifi_restart": False, "ssid_in_use": "x"}))
        code, out, err = self.cli("restore-settings", self.write(b'{"led": false}', "s.json"))
        self.assertEqual(code, 0, err)
        self.assertNotIn("restarts", out)

    def test_refused_and_bad_files(self):
        self.pico.route("POST", "/api/settings", (400, {"error": "bad_value", "message": "channel must be 1 to 11"}))
        code, _, err = self.cli("restore-settings", self.write(b'{"channel": 99}', "s.json"))
        self.assertEqual(code, 1)
        self.assertIn("channel must be", err)
        count = len(self.pico.requests)
        for data in (b"not json", b"[1]"):
            code, _, err = self.cli("restore-settings", self.write(data, "t.json"))
            self.assertEqual(code, 1)
        code, _, err = self.cli("restore-settings", os.path.join(self.tmp.name, "missing.json"))
        self.assertEqual(code, 1)
        self.assertEqual(len(self.pico.requests), count)


if __name__ == "__main__":
    unittest.main()
