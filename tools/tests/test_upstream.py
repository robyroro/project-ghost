# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import contextlib
import datetime
import io
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import upstream

OLD = "152.0.7977.149"
NEW = "152.0.7977.158"
COMMIT = "9007248033443cbe529d18cf911d6954f5ce4f49"
PUBLISHED_MS = 1791306419521  # 2026-10-06 17:06:59 UTC
DETECTED = datetime.datetime(2026, 10, 9, 7, 17, tzinfo=datetime.timezone.utc)


def entry(version=NEW, milestone=152, commit=COMMIT, time=PUBLISHED_MS) -> dict:
    """A release as chromiumdash's fetch_releases lists it (fields we read, and some we don't)."""
    return {"channel": "Extended", "platform": "Windows", "version": version,
            "milestone": milestone, "time": time, "previous_version": OLD,
            "hashes": {"chromium": commit, "v8": "0" * 40}}


def fetcher(extended=(), stable=()):
    def fetch(url: str):
        if "channel=Extended" in url:
            return list(extended)
        if "channel=Stable" in url:
            return list(stable)
        raise AssertionError(url)
    return fetch


class PinnedTestCase(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.webops = Path(tmp.name)
        (self.webops / "CHROMIUM_VERSION").write_text(OLD + "\n", encoding="utf-8")

    def check(self, *entries):
        return upstream.check(self.webops, fetcher(extended=entries), now=DETECTED)


class CheckTest(PinnedTestCase):
    def test_the_pinned_version_is_current(self):
        result = self.check(entry(version=OLD))
        self.assertEqual(result["verdict"], "current")
        self.assertNotIn("issue", result)

    def test_an_older_reply_is_current(self):
        self.assertEqual(self.check(entry(version="152.0.7977.140"))["verdict"], "current")

    def test_a_newer_release_of_the_milestone_is_a_security_release(self):
        result = self.check(entry())
        self.assertEqual(result["verdict"], "security-release")
        self.assertEqual(result["version"], NEW)
        self.assertEqual(result["commit"], COMMIT)
        issue = result["issue"]
        self.assertEqual(issue["title"], "Security release: Chromium 152.0.7977.158")
        self.assertEqual(issue["labels"], ["security-release"])
        for text in ("Published: 2026-10-06 17:06 UTC", "Detected: 2026-10-09 07:17 UTC",
                     "Deadline: 2026-10-09 17:06 UTC",
                     "https://chromereleases.googleblog.com/search?q=152.0.7977.158",
                     "docs/build/security-release.md", COMMIT):
            self.assertIn(text, issue["body"])

    def test_a_new_milestone_is_its_own_verdict(self):
        result = self.check(entry(version="154.0.8037.100", milestone=154))
        self.assertEqual(result["verdict"], "milestone")
        self.assertEqual(result["issue"]["title"], "Milestone: Chromium 154 on Extended")
        self.assertEqual(result["issue"]["labels"], ["milestone"])

    def test_broken_replies_are_errors(self):
        for reply, why in (([], "no Extended"), ({"error": "x"}, "a list"),
                           ([{"version": NEW, "milestone": 152, "time": 1}], "hashes"),
                           ([entry(commit="xyz")], "commit")):
            with self.subTest(why), self.assertRaisesRegex(upstream.UpstreamError, why):
                upstream.check(self.webops, lambda url, r=reply: r, now=DETECTED)

    def test_main_exits_2_when_chromiumdash_cannot_be_read(self):
        failing = mock.Mock(side_effect=upstream.UpstreamError("no reply"))
        with mock.patch.object(upstream, "fetch_json", failing), \
                contextlib.redirect_stderr(io.StringIO()) as err:
            self.assertEqual(upstream.main(["check"]), 2)
        self.assertIn("no reply", err.getvalue())


class ReportTest(PinnedTestCase):
    def gh(self, existing=()):
        calls = []

        def run(args):
            calls.append(args)
            if args[:2] == ["issue", "list"]:
                return json.dumps([{"title": t, "url": "https://x/1"} for t in existing])
            if args[:2] == ["issue", "create"]:
                return "https://github.com/o/r/issues/7\n"
            return ""
        return run, calls

    def test_current_opens_nothing(self):
        run, calls = self.gh()
        upstream.report(self.check(entry(version=OLD)), run)
        self.assertEqual(calls, [])

    def test_an_issue_with_the_same_title_is_not_opened_again(self):
        run, calls = self.gh(existing=["Security release: Chromium 152.0.7977.158"])
        self.assertIn("already", upstream.report(self.check(entry()), run))
        self.assertFalse(any(c[:2] == ["issue", "create"] for c in calls))

    def test_opens_the_issue_with_its_label(self):
        run, calls = self.gh(existing=["Security release: Chromium 152.0.7977.1580"])
        result = self.check(entry())
        self.assertIn("issues/7", upstream.report(result, run))
        label = next(c for c in calls if c[:2] == ["label", "create"])
        self.assertEqual(label[2], "security-release")
        create = next(c for c in calls if c[:2] == ["issue", "create"])
        self.assertEqual(create[create.index("--title") + 1], result["issue"]["title"])
        self.assertEqual(create[create.index("--body") + 1], result["issue"]["body"])
        self.assertEqual(create[create.index("--label") + 1], "security-release")
        self.assertLess(calls.index(label), calls.index(create))


if __name__ == "__main__":
    unittest.main()
