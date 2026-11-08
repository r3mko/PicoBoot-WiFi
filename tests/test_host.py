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


if __name__ == "__main__":
    unittest.main()
