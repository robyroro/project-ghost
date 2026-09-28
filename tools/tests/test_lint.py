# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import unittest

import lint
from tests.gitutil import GitTestCase

NOTICE_LINES = [
    "This Source Code Form is subject to the terms of the Mozilla Public",
    "License, v. 2.0. If a copy of the MPL was not distributed with this",
    "file, You can obtain one at https://mozilla.org/MPL/2.0/.",
]


def commented(prefix: str, suffix: str = "") -> str:
    return "\n".join(f"{prefix}{line}{suffix}" for line in NOTICE_LINES) + "\n"


class NoticeTest(unittest.TestCase):
    def test_line_comment_styles(self):
        for prefix in ("# ", "// "):
            self.assertTrue(lint.has_mpl_notice(commented(prefix) + "code\n"), prefix)

    def test_block_comment_styles(self):
        self.assertTrue(lint.has_mpl_notice("/* " + "\n * ".join(NOTICE_LINES) + " */\n"))
        self.assertTrue(lint.has_mpl_notice("<!-- " + "\n".join(NOTICE_LINES) + " -->\n"))

    def test_shebang_before_notice(self):
        self.assertTrue(lint.has_mpl_notice("#!/usr/bin/env python3\n" + commented("# ")))

    def test_missing_or_altered_notice(self):
        self.assertFalse(lint.has_mpl_notice("print('hi')\n"))
        self.assertFalse(lint.has_mpl_notice(commented("# ").replace("v. 2.0", "v. 1.1")))

    def test_notice_too_far_down(self):
        self.assertFalse(lint.has_mpl_notice("\n" * lint.HEADER_SEARCH_LINES + commented("# ")))


class LintRepoTest(GitTestCase):
    def setUp(self):
        super().setUp()
        self.repo = self.init_repo("r")
        (self.repo / "patches").mkdir()
        (self.repo / "docs").mkdir()

    def problems(self, files):
        self.commit(self.repo, files, "files")
        return lint.lint(self.repo)

    def test_clean_repo(self):
        self.assertEqual(self.problems({
            "tools/a.py": commented("# ") + "x = 1\n",
            "docs/a.md": "# Top\nSee [b](b.md), [site](https://example.org), [top](#top).\n",
            "docs/b.md": "# B\n",
            "data.json": "{}\n",
        }), [])

    def test_missing_header(self):
        self.assertEqual(self.problems({"browser/x.cc": "int x;\n"}),
                         [f"browser/x.cc: missing the MPL-2.0 notice in the first "
                          f"{lint.HEADER_SEARCH_LINES} lines"])

    def test_vendored_code_is_exempt(self):
        self.assertEqual(self.problems({"third_party/foo/x.cc": "int x;\n"}), [])

    def test_crlf(self):
        self.assertEqual(self.problems({"docs/a.md": b"# A\r\n"}),
                         ["docs/a.md: CRLF line endings (the repo is LF-only; see .gitattributes)"])

    def test_broken_link_and_line_suffix(self):
        problems = self.problems({"docs/a.md": "[x](missing.md) [y](b.md:12) [z](b.md#part)\n",
                                  "docs/b.md": "# B\n## Part\n"})
        self.assertEqual(problems, ["docs/a.md: broken relative link missing.md"])

    def test_anchors_are_checked(self):
        problems = self.problems({
            "docs/a.md": "[ok](b.md#identities-phase-6) [bad](b.md#nope) [self](#local) "
                         "[dup](b.md#notes-1)\n\n## Local\n",
            "docs/b.md": "# B\n## Identities (Phase 6)\n## Notes\n## Notes\n",
        })
        self.assertEqual(problems, ["docs/a.md: broken relative link b.md#nope"])

    def test_code_fences_are_ignored(self):
        self.assertEqual(self.problems({
            "docs/a.md": "```\n[x](missing.md)\n# not a heading\n```\n[y](#real)\n## Real\n",
        }), [])

    def test_links_may_not_escape_the_repo(self):
        self.assertEqual(self.problems({"docs/a.md": "[x](../../outside.md)\n"}),
                         ["docs/a.md: broken relative link ../../outside.md"])


if __name__ == "__main__":
    unittest.main()
