"""Tests for tools/picoboot-wifi.py (the command-line client of docs/api.md), against a mock Pico."""

import contextlib
import http.server
import io
import json
import os
import socket
import tempfile
import threading
import unittest
from unittest import mock

from helpers import load_tool, run_tool

cli = load_tool("picoboot-wifi")
mk = load_tool("mkupdate")

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
                    status, payload = answers.pop(0) if len(answers) > 1 else answers[0]
                data = json.dumps(payload).encode()
                self.send_response(status)
                self.send_header("Content-Type", "application/json")
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

    def test_no_wait(self):
        code, out, _ = self.cli("update", self.update_file(), "--yes", "--no-wait")
        self.assertEqual(code, 0)
        self.assertEqual([r["path"] for r in self.pico.requests], ["/api/status", "/api/firmware"])

    def test_prompt_yes_and_no(self):
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


if __name__ == "__main__":
    unittest.main()
