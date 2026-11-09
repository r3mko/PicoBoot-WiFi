"""Tests for tools/mkfsdata.py (the web UI packed for lwIP httpd, design D18)."""

import gzip
import os
import re
import tempfile
import unittest

from helpers import REPO, load_tool, run_tool

mk = load_tool("mkfsdata")
WEB = os.path.join(REPO, "src", "web")
MAX_GZIPPED = 30 * 1024


def split_response(response):
    head, body = response.split(b"\r\n\r\n", 1)
    lines = head.decode("ascii").split("\r\n")
    return lines[0], dict(line.split(": ", 1) for line in lines[1:]), body


def parse_c(text):
    """{name: complete response bytes} from the generated C file."""
    arrays = {}
    for ident, body in re.findall(r"data_(\w+)\[\] = \{(.*?)\};", text, re.S):
        arrays[ident] = bytes(int(h, 16) for h in re.findall(r"0x([0-9a-f]{2})", body))
    files = {}
    for ident, name in re.findall(r'file_(\w+) = \{\s*[^,]+,\s*\(const unsigned char \*\)"([^"]+)"', text):
        files[name] = arrays[ident]
    return files


class MkfsdataTest(unittest.TestCase):
    def test_round_trip_and_headers(self):
        files = [("index.html", b"<p>caf\xc3\xa9</p>" * 50), ("a.js", b"x=1;\n" * 30),
                 ("s.css", b"p{}"), ("i.svg", b"<svg/>")]
        text = mk.generate(files)
        parsed = parse_c(text)
        types = {"index.html": "text/html; charset=utf-8", "a.js": "text/javascript; charset=utf-8",
                 "s.css": "text/css; charset=utf-8", "i.svg": "image/svg+xml"}
        for name, content in files:
            status, headers, body = split_response(parsed["/" + name])
            self.assertEqual(status, "HTTP/1.1 200 OK")
            self.assertEqual(headers["Content-Type"], types[name])
            self.assertEqual(headers["Content-Encoding"], "gzip")
            self.assertEqual(headers["Cache-Control"], "no-store")
            self.assertEqual(headers["Connection"], "close")
            self.assertEqual(int(headers["Content-Length"]), len(body))
            self.assertEqual(gzip.decompress(body), content)
        self.assertIn(f"#define FS_NUMFILES {len(files)}", text)
        self.assertIn("#define FS_ROOT (&file_index_html)", text)
        self.assertIn("FS_FILE_FLAGS_HEADER_INCLUDED", text)
        self.assertIn("static const unsigned char data_index_html", text)

    def test_unknown_extension_fails(self):
        with self.assertRaises(ValueError):
            mk.generate([("x.bin", b"1")])

    def test_hidden_files_and_backups_are_skipped(self):
        # Finder's .DS_Store or an editor's backup in src/web/ mustn't stop the build (phase 2 review)
        with tempfile.TemporaryDirectory() as d:
            for name in ("index.html", ".DS_Store", "index.html~"):
                with open(os.path.join(d, name), "wb") as f:
                    f.write(b"x")
            self.assertEqual([name for name, _ in mk.web_files(d)], ["index.html"])

    def test_reproducible(self):
        with tempfile.TemporaryDirectory() as d:
            outs = []
            for n in range(2):
                out = os.path.join(d, f"out{n}.c")
                self.assertEqual(run_tool("mkfsdata", WEB, out).returncode, 0)
                with open(out, "rb") as f:
                    outs.append(f.read())
            self.assertEqual(outs[0], outs[1])
            self.assertEqual(mk.gzip_bytes(b"abc")[4:8], b"\0\0\0\0")   # mtime 0

    def test_web_ui_packs_and_stays_small(self):
        files = mk.web_files(WEB)
        self.assertIn("index.html", [n for n, _ in files])
        total = 0
        for name, content in files:
            _, headers, body = split_response(mk.http_response(name, content))
            self.assertEqual(gzip.decompress(body), content)
            total += len(body)
        self.assertLess(total, MAX_GZIPPED, "the gzipped web UI must stay under 30 KB")


if __name__ == "__main__":
    unittest.main()
