"""
A syntax check of the page's own script in src/web/index.html, run in Node. The page's warnings
(D45's noise warning among them) are checked by hand, on the console and with made-up answers.
    python3 -m unittest discover -s tests -p 'test_page_js.py' -v
"""

import os
import re
import shutil
import subprocess
import tempfile
import unittest

from helpers import REPO

NODE = shutil.which("node")


@unittest.skipUnless(NODE, "node not found")
class PageScriptTest(unittest.TestCase):
    def test_the_pages_script_parses(self):
        """index.html's own script (the one without src) parses as a browser's classic script does:
        Node's vm.Script, not `node --check`, which reads a file as a module and accepts a top-level
        return that a browser rejects (R3 review of release 1.1.2)."""
        with open(os.path.join(REPO, "src", "web", "index.html"), encoding="utf-8") as f:
            scripts = re.findall(r"<script>(.*?)</script>", f.read(), re.S)
        self.assertEqual(len(scripts), 1)
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "page.js")
            with open(path, "w", encoding="utf-8") as f:
                f.write(scripts[0])
            parse = "new (require('vm').Script)(require('fs').readFileSync(process.argv[1], 'utf8'), {filename: 'index.html'})"
            run = subprocess.run([NODE, "-e", parse, path], capture_output=True, text=True, timeout=30)
            self.assertEqual(run.returncode, 0, run.stderr)


if __name__ == "__main__":
    unittest.main()
