# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import unittest

import uncloak_cost
from uncloak_cost import Uncloaking


class ParseTest(unittest.TestCase):
    def test_reads_the_filters_lines_only(self):
        # Chromium's format: [pid:tid:date/time:LEVEL:file:line] message.
        log = "\n".join([
            r"[1:2:1013/101010.123:VERBOSE2:ghost\browser\blocking\request_filter.cc:321] "
            "Uncloak cdn.example.com: 412 us (resolution 300 us)",
            r"[1:2:1013/101010.124:VERBOSE1:ghost\browser\blocking\request_filter.cc:300] "
            "Blocked https://x/ by ||x^",
            "[1:2:1013/101011.125:VERBOSE2:ghost/browser/blocking/request_filter.cc:321] "
            "Uncloak a.example.com: 1203 us (resolution 1100 us)",
            "[1:2:1013/101010.126:VERBOSE2:other.cc:9] Uncloak b.example.com: 5 us (resolution 1 us)",
            "[1:2:1013/101012.000:VERBOSE2:ghost/browser/blocking/request_filter.cc:321] "
            "Uncloak cdn.example.com: 7 us (remembered)",
            "garbage",
        ])
        rows = uncloak_cost.parse(log)
        # A host the page's memo answered has no resolution.
        self.assertEqual([(r.host, r.total_us, r.resolution_us) for r in rows],
                         [("cdn.example.com", 412, 300), ("a.example.com", 1203, 1100),
                          ("cdn.example.com", 7, None)])
        # The line is written at the verdict: the start is that time less the total.
        self.assertAlmostEqual(rows[1].end - rows[0].end, 1.002)
        self.assertAlmostEqual(rows[0].start, rows[0].end - 0.000412)


def netlog(*requests) -> dict:
    """A NetLog with the given host resolver requests: (id, begin ms, host,
    speculative, cache hit, net error at the end or None for no end)."""
    events = []
    for source_id, begin, host, speculative, hit, error in requests:
        source = {"id": source_id, "type": 7}
        events.append({"phase": 1, "source": source, "time": str(begin), "type": 100,
                       "params": {"host": host, "is_speculative": speculative}})
        events.append({"phase": 0, "source": source, "time": str(begin), "type": 101 if hit else 102})
        if error is not None:
            events.append({"phase": 2, "source": source, "time": str(begin + 1), "type": 100,
                           "params": {"net_error": error} if error else {}})
    # A request of another source: the request's own resolution, never ours.
    events.append({"phase": 1, "source": {"id": 99, "type": 8}, "time": "1", "type": 100,
                   "params": {"host": "https://a.test", "is_speculative": False}})
    return {"constants": {
        "logEventTypes": {"HOST_RESOLVER_MANAGER_REQUEST": 100,
                          "HOST_RESOLVER_MANAGER_CACHE_HIT": 101,
                          "HOST_RESOLVER_MANAGER_CREATE_JOB": 102},
        "logSourceType": {"NETWORK_SERVICE_HOST_RESOLVER": 7, "QUIC_SESSION_POOL_DIRECT_JOB": 8},
        "logEventPhase": {"PHASE_BEGIN": 1, "PHASE_END": 2, "PHASE_NONE": 0}},
        "events": events}


class CacheHitsTest(unittest.TestCase):
    def test_per_host_in_the_order_they_began(self):
        log = netlog((3, 20, "https://a.test", False, True, 0),
                     (1, 10, "https://a.test:8443", False, False, 0),
                     (2, 15, "http://b.test", False, True, 0))
        self.assertEqual(uncloak_cost.cache_hits(log), {"a.test": [False, True], "b.test": [True]})

    def test_skips_speculative_and_cancelled_requests(self):
        log = netlog((1, 10, "https://a.test", True, True, 0),
                     (2, 11, "https://a.test", False, False, -3),
                     (3, 12, "https://a.test", False, False, None),
                     (4, 13, "https://a.test", False, True, -105))
        # A failed resolution answered the filter; a cancelled one didn't.
        self.assertEqual(uncloak_cost.cache_hits(log), {"a.test": [True]})


class ClassifyTest(unittest.TestCase):
    def row(self, host: str, end: float, total_us: int) -> Uncloaking:
        return Uncloaking(host, end, total_us, total_us)

    def test_matches_each_host_by_start(self):
        # b started first though its line came second.
        rows = [self.row("a.test", 1.0, 100), self.row("a.test", 1.001, 5000),
                self.row("c.test", 2.0, 10)]
        hits = {"a.test": [False, True], "c.test": [True, True]}
        self.assertEqual(uncloak_cost.classify(rows, hits), [True, False, None])

    def test_remembered_rows_pair_with_no_resolution(self):
        rows = [self.row("a.test", 1.0, 100), Uncloaking("a.test", 1.5, 7, None)]
        self.assertEqual(uncloak_cost.classify(rows, {"a.test": [True]}), [True, None])


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
