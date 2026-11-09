"""
Builds and runs the host C tests in tests/host/ (plain C from src/, without the Pico SDK; the few
SDK headers needed come from tests/host/stub).
Uses the compiler in $CC, else cc; skipped if there is none.

    python3 -m unittest discover -s tests -p 'test_host.py' -v
"""

import os
import shutil
import subprocess
import tempfile
import unittest

from helpers import load_tool

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(REPO, "src")
HOST = os.path.join(REPO, "tests", "host")
INCLUDES = [os.path.join(SRC, "common"), os.path.join(SRC, "firmware"), os.path.join(HOST, "stub")]
CC = os.environ.get("CC", "cc")

# Each test program and the sources in src/ it needs (test_wifiblob.c includes wifiblob.c itself,
# to reach its static header check)
PROGRAMS = {
    "test_records.c": ["common/records.c"],
    "test_bootdecide.c": ["common/bootdecide.c"],
    "test_ringlog.c": ["common/ringlog.c", "common/records.c"],
    "test_payload.c": ["firmware/payload.c"],
    "test_settings.c": ["firmware/settings.c", "common/records.c"],
    "test_json.c": ["firmware/json.c"],
    "test_update.c": ["firmware/update.c"],
    "test_wifiblob.c": [],
}


@unittest.skipUnless(shutil.which(CC), f"no host C compiler ({CC})")
class HostCTest(unittest.TestCase):
    def build_and_run(self, test, sources):
        with tempfile.TemporaryDirectory() as tmp:
            exe = os.path.join(tmp, "test")
            cmd = [CC, "-std=c11", "-Wall", "-Wextra", "-Werror", "-o", exe]
            for include in INCLUDES:
                cmd += ["-I", include]
            cmd += [os.path.join(HOST, test)] + [os.path.join(SRC, s) for s in sources]
            build = subprocess.run(cmd, capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([exe], capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_programs(self):
        for test, sources in PROGRAMS.items():
            with self.subTest(test):
                self.build_and_run(test, sources)


@unittest.skipUnless(shutil.which(CC), f"no host C compiler ({CC})")
class UpdateFileInteropTest(unittest.TestCase):
    """The device's update.c reads the files tools/mkupdate.py writes (docs/update-format.md)."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = os.path.join(cls.tmp.name, "feed_update")
        cmd = [CC, "-std=c11", "-Wall", "-Wextra", "-Werror", "-o", cls.exe]
        for include in INCLUDES:
            cmd += ["-I", include]
        cmd += [os.path.join(HOST, "feed_update.c"), os.path.join(SRC, "firmware", "update.c")]
        build = subprocess.run(cmd, capture_output=True, text=True)
        assert build.returncode == 0, build.stderr
        cls.mk = load_tool("mkupdate")

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def feed(self, data):
        path = os.path.join(self.tmp.name, "update.bin")
        with open(path, "wb") as f:
            f.write(data)
        run = subprocess.run([self.exe, path], capture_output=True, text=True, timeout=60)
        return run.returncode, run.stdout.strip()

    def parts(self):
        image = bytes((i * 7 + (i >> 9)) & 0xFF for i in range(150000))
        blob = b"W43B" + bytes(12) + (225304 - 64).to_bytes(4, "little") + bytes(44) + bytes(range(256)) * 880
        return image, blob[:225304]

    def test_a_file_from_mkupdate_installs(self):
        image, blob = self.parts()
        code, out = self.feed(self.mk.build_update(image, blob, "0.3.0-dev+test", "pico2_w"))
        self.assertEqual((code, out), (0, "ok 0.3.0-dev+test"))

    def test_a_damaged_file_is_refused(self):
        image, blob = self.parts()
        data = bytearray(self.mk.build_update(image, blob, "0.3.0-dev+test", "pico2_w"))
        data[256 + 1000] ^= 1
        self.assertEqual(self.feed(bytes(data))[1].split()[0], "bad_image")
        self.assertEqual(self.feed(self.mk.build_update(image, blob, "x", "pico_w"))[1].split()[0], "wrong_board")

    @unittest.skipUnless(os.environ.get("PICOBOOT_BUILD_DIR"), "PICOBOOT_BUILD_DIR not set")
    def test_the_built_update_file_installs(self):
        with open(os.path.join(os.environ["PICOBOOT_BUILD_DIR"], "picoboot-wifi-update.bin"), "rb") as f:
            code, out = self.feed(f.read())
        self.assertEqual(code, 0, out)


if __name__ == "__main__":
    unittest.main()
