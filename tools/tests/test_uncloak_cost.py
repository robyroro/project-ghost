# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import unittest

import uncloak_cost


class ParseTest(unittest.TestCase):
    def test_reads_the_filters_lines_only(self):
        # Chromium's format: [pid:tid:date/time:LEVEL:file:line] message.
        log = "\n".join([
            r"[1:2:1013/101010.123:VERBOSE2:ghost\browser\blocking\request_filter.cc:321] "
            "Uncloak cdn.example.com: 412 us (resolution 300 us)",
            r"[1:2:1013/101010.124:VERBOSE1:ghost\browser\blocking\request_filter.cc:300] "
            "Blocked https://x/ by ||x^",
            "[1:2:1013/101010.125:VERBOSE2:ghost/browser/blocking/request_filter.cc:321] "
            "Uncloak a.example.com: 1203 us (resolution 1100 us)",
            "[1:2:1013/101010.126:VERBOSE2:other.cc:9] Uncloak b.example.com: 5 us (resolution 1 us)",
            "garbage",
        ])
        self.assertEqual(uncloak_cost.parse(log), [(412, 300), (1203, 1100)])


class PercentileTest(unittest.TestCase):
    def test_nearest_rank(self):
        values = list(range(1, 101))
        self.assertEqual(uncloak_cost.percentile(values, 50), 50)
        self.assertEqual(uncloak_cost.percentile(values, 99), 99)
        self.assertEqual(uncloak_cost.percentile([7], 99), 7)

    def test_empty_is_none(self):
        self.assertIsNone(uncloak_cost.percentile([], 99))


class PagesTest(unittest.TestCase):
    def test_the_corpus_front_pages(self):
        pages = uncloak_cost.corpus_pages()
        self.assertEqual(len(pages), 20)
        self.assertTrue(all(page.startswith("https://") for page in pages))


if __name__ == "__main__":
    unittest.main()
