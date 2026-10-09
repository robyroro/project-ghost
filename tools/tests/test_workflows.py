# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The scheduled upstream workflow: what it may do, when, and with what."""

import re
import unittest

import repo

UPSTREAM = repo.REPO_ROOT / ".github" / "workflows" / "upstream.yml"


class UpstreamWorkflowTest(unittest.TestCase):
    def setUp(self):
        self.text = UPSTREAM.read_text(encoding="utf-8")

    def test_reads_the_repository_and_writes_issues_only(self):
        block = re.search(r"^permissions:\n((?:  \S.*\n)+)", self.text, re.MULTILINE)
        self.assertIsNotNone(block, "a top-level permissions block listing each permission")
        self.assertEqual(sorted(line.strip() for line in block.group(1).splitlines()),
                         ["contents: read", "issues: write"])
        self.assertEqual(len(re.findall(r"^\s*permissions:", self.text, re.MULTILINE)), 1,
                         "no job widens the permissions")

    def test_runs_every_three_hours_and_on_demand(self):
        self.assertIn('- cron: "17 */3 * * *"', self.text)
        self.assertIn("workflow_dispatch:", self.text)

    def test_actions_are_pinned_to_commits(self):
        uses = re.findall(r"uses:\s*(\S+)", self.text)
        self.assertTrue(uses)
        for action in uses:
            self.assertRegex(action, r"@[0-9a-f]{40}$")

    def test_checks_then_reports(self):
        check = self.text.find("tools/upstream.py check --json")
        report = self.text.find("tools/upstream.py report")
        self.assertGreater(check, 0)
        self.assertGreater(report, check)


if __name__ == "__main__":
    unittest.main()
