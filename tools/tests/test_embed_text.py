# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import tempfile
import unittest
from pathlib import Path

import embed_text


class RenderTest(unittest.TestCase):
    def test_a_header_with_the_text_as_a_raw_string(self):
        header = embed_text.render("a\nb \"c\"\n", "ghost::query_filter", "kList",
                                   "gen/ghost/list.h", "data/list.txt")
        self.assertIn("namespace ghost::query_filter {\n", header)
        self.assertIn('inline constexpr char kList[] = R"embed(a\nb "c"\n)embed";\n', header)
        self.assertIn("#ifndef GEN_GHOST_LIST_H_\n", header)
        self.assertIn("data/list.txt", header)

    def test_refuses_text_that_would_end_the_raw_string(self):
        with self.assertRaises(ValueError):
            embed_text.render('x)embed"y', "n", "k", "o.h", "i.txt")


class MainTest(unittest.TestCase):
    def test_writes_lf_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            src, out = Path(tmp, "in.txt"), Path(tmp, "out.h")
            src.write_bytes(b"one\r\ntwo\n")
            embed_text.main(["--input", str(src), "--output", str(out), "--namespace", "n",
                             "--name", "kText"])
            data = out.read_bytes()
            self.assertNotIn(b"\r", data)
            self.assertIn(b'R"embed(one\ntwo\n)embed"', data)


if __name__ == "__main__":
    unittest.main()
