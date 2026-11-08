"""
Builds and runs the host C tests in tests/host/ (plain C from src/common, without the Pico SDK).
Uses the compiler in $CC, else cc; skipped if there is none.

    python3 -m unittest discover -s tests -p 'test_host.py' -v
"""

import os
import shutil
import subprocess
import tempfile
import unittest

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COMMON = os.path.join(REPO, "src", "common")
HOST = os.path.join(REPO, "tests", "host")
CC = os.environ.get("CC", "cc")

# Each test program and the src/common sources it needs
PROGRAMS = {
    "test_records.c": ["records.c"],
    "test_bootdecide.c": ["bootdecide.c"],
}


@unittest.skipUnless(shutil.which(CC), f"no host C compiler ({CC})")
class HostCTest(unittest.TestCase):
    def build_and_run(self, test, sources):
        with tempfile.TemporaryDirectory() as tmp:
            exe = os.path.join(tmp, "test")
            cmd = [CC, "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", COMMON, "-o", exe,
                   os.path.join(HOST, test)] + [os.path.join(COMMON, s) for s in sources]
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
