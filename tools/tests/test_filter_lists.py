# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import datetime
import json
import tempfile
import unittest
from pathlib import Path

import filter_lists

LIST = "[Adblock Plus 2.0]\n! Version: 202610100815\n! Title: EasyList\n||ads.test^\n"
NOW = datetime.datetime(2026, 10, 10, 8, 30, tzinfo=datetime.timezone.utc)


def fetcher(text=LIST):
    return lambda url: (text.replace("EasyList", url.rsplit("/", 1)[1])).encode("utf-8")


class UpdateTest(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.data = Path(tmp.name)

    def test_writes_each_list_and_its_record(self):
        filter_lists.update(self.data, fetcher(), now=NOW)
        versions = json.loads((self.data / filter_lists.VERSIONS).read_text(encoding="utf-8"))
        self.assertEqual(sorted(versions), ["easylist.txt", "easyprivacy.txt"])
        record = versions["easylist.txt"]
        self.assertEqual(record["url"], "https://easylist.to/easylist/easylist.txt")
        self.assertEqual(record["version"], "202610100815")
        self.assertEqual(record["fetched"], "2026-10-10")
        self.assertEqual(filter_lists.check(self.data), [])

    def test_refuses_something_that_is_not_a_filter_list(self):
        with self.assertRaisesRegex(filter_lists.ListError, "not a filter list"):
            filter_lists.update(self.data, fetcher("<html>blocked</html>"), now=NOW)
        self.assertEqual(list(self.data.iterdir()), [])


class CheckTest(unittest.TestCase):
    def test_a_changed_or_missing_list_is_reported(self):
        with tempfile.TemporaryDirectory() as d:
            data = Path(d)
            filter_lists.update(data, fetcher(), now=NOW)
            (data / "easylist.txt").write_text(LIST + "||more.test^\n", encoding="utf-8")
            (data / "easyprivacy.txt").unlink()
            problems = filter_lists.check(data)
        self.assertEqual(len(problems), 2)
        self.assertIn("easylist.txt", problems[0])
        self.assertIn("easyprivacy.txt", problems[1])

    def test_the_committed_lists_match_their_record(self):
        self.assertEqual(filter_lists.check(filter_lists.DATA), [])


if __name__ == "__main__":
    unittest.main()
