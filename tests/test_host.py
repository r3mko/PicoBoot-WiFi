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
    "test_payload.c": ["firmware/payload.c", "common/records.c"],
    "test_settings.c": ["firmware/settings.c", "common/records.c"],
    "test_json.c": ["firmware/json.c"],
    "test_update.c": ["firmware/update.c"],
    "test_slots.c": ["firmware/slots.c", "common/bootdecide.c"],
    "test_bininfo.c": ["firmware/bininfo.c"],
    "test_payloadup.c": ["firmware/payloadup.c", "firmware/payload.c", "common/records.c"],
    "test_wifiblob.c": [],
    "test_dhcpserver.c": ["firmware/net/dhcpserver/dhcpserver.c"],
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


@unittest.skipUnless(shutil.which(CC), f"no host C compiler ({CC})")
class PayloadFileInteropTest(unittest.TestCase):
    """The device's payloadup.c takes the payloads the tools and the payload projects make (D16).

    With PICOBOOT_PAYLOAD_DIR set to a folder holding swiss_r2119.uf2 and gekkoboot_pico.uf2 (the
    projects' release files), those are fed too, and gekkoboot's is compared with our converter."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = os.path.join(cls.tmp.name, "feed_payload")
        cmd = [CC, "-std=c11", "-Wall", "-Wextra", "-Werror", "-o", cls.exe]
        for include in INCLUDES:
            cmd += ["-I", include]
        cmd += [os.path.join(HOST, "feed_payload.c")] + [os.path.join(SRC, s) for s in
                                                          ("firmware/payloadup.c", "firmware/payload.c", "common/records.c")]
        build = subprocess.run(cmd, capture_output=True, text=True)
        assert build.returncode == 0, build.stderr
        cls.ipl = load_tool("process_ipl")
        cls.uf2 = load_tool("mkuf2")
        with open(os.path.join(REPO, "assets", "ipl", "gekkoboot_r9_4.dol"), "rb") as f:
            cls.payload = cls.ipl.dol_to_payload(f.read())

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def feed(self, data):
        path = os.path.join(self.tmp.name, "payload.bin")
        with open(path, "wb") as f:
            f.write(data)
        run = subprocess.run([self.exe, path], capture_output=True, text=True, timeout=60)
        return run.stdout.split()

    def payload_uf2(self, payload, *families):
        region = [("payload", 0x80000, payload, 0x180000)]
        return b"".join(self.uf2.build_uf2(region, family) for family in families)

    def test_gekkoboot_as_raw_and_uf2(self):
        import hashlib
        want = ["ok", hashlib.sha256(self.payload).hexdigest(), str(len(self.payload))]
        self.assertEqual(len(self.payload), 60548)      # As gekkoboot_pico.uf2 holds it (ecosystem.md)
        self.assertEqual(self.feed(self.payload), want)
        self.assertEqual(self.feed(self.payload_uf2(self.payload, 0xe48bff56)), want)               # rp2040 only
        self.assertEqual(self.feed(self.payload_uf2(self.payload, 0xe48bff56, 0xe48bff59)), want)   # both

    def test_not_payloads(self):
        self.assertEqual(self.feed(b"\x55" * 4096)[0], "not_a_payload")
        firmware_uf2 = self.uf2.build_uf2([("slot A", 0x200000, bytes(range(256)) * 16, 0x40000)])
        self.assertEqual(self.feed(firmware_uf2)[0], "not_a_payload")
        damaged = bytearray(self.payload)
        damaged[-1] ^= 1
        self.assertEqual(self.feed(bytes(damaged))[0], "bad_payload")

    @unittest.skipUnless(os.environ.get("PICOBOOT_BUILD_DIR"), "PICOBOOT_BUILD_DIR not set")
    def test_the_install_uf2_is_not_a_payload(self):
        with open(os.path.join(os.environ["PICOBOOT_BUILD_DIR"], "picoboot-wifi-install.uf2"), "rb") as f:
            self.assertEqual(self.feed(f.read())[0], "not_a_payload")

    @unittest.skipUnless(os.environ.get("PICOBOOT_PAYLOAD_DIR"), "PICOBOOT_PAYLOAD_DIR not set")
    def test_the_projects_release_uf2s(self):
        folder = os.environ["PICOBOOT_PAYLOAD_DIR"]
        for name, size in (("swiss_r2119.uf2", 1349956), ("gekkoboot_pico.uf2", 60548)):
            with self.subTest(name):
                with open(os.path.join(folder, name), "rb") as f:
                    result = self.feed(f.read())
                self.assertEqual((result[0], result[2]), ("ok", str(size)))
        # Our converter frames and scrambles as these projects do: a release payload, descrambled and
        # wrapped as a .dol (as PicoBoot-DX's uf2dol.py does), comes back byte for byte. (Their .dol
        # files aren't the builds in these UF2s, so the round trip is the comparison there is.)
        import struct
        for name in ("gekkoboot_pico.uf2", "swiss_r2119.uf2"):
            with self.subTest(name), open(os.path.join(folder, name), "rb") as f:
                blocks = {}
                data = f.read()
                for off in range(0, len(data), 512):
                    addr, size, family = struct.unpack_from("<I I 8x I", data, off + 12)
                    blocks.setdefault(family, {})[addr] = data[off + 32:off + 32 + size]
                first = blocks[next(iter(blocks))]
                image = b"".join(first[a] for a in sorted(first))
                payload = image[:struct.unpack(">I", image[8:12])[0]]
                plain = bytes(self.ipl.scramble(bytearray(0x720) + payload[32:-4])[0x720:])
                header = [0x100] + [0] * 17 + [0x81300000] + [0] * 17 + [len(plain)] + [0] * 17 + [0, 0, 0x81300000] + [0] * 7
                dol = struct.pack(">64I", *header) + plain
                self.assertEqual(self.ipl.dol_to_payload(dol), payload)
