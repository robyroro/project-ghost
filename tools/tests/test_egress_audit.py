# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import json
import tempfile
import unittest
import urllib.request
from pathlib import Path

import egress_audit

# Event type ids are assigned per build; the audit must resolve names through
# the constants table rather than assume ids.
CONSTANTS = {"logEventTypes": {"URL_REQUEST_START_JOB": 104, "HOST_RESOLVER_MANAGER_REQUEST": 7,
                               "TCP_CONNECT": 51, "HTTP_STREAM_POOL_ATTEMPT_MANAGER_ALIVE": 300}}


def event(type_id, params):
    return {"source": {"id": 1, "type": 1}, "type": type_id, "time": "1", "phase": 1,
            "params": params}


def netlog(*events):
    return {"constants": CONSTANTS, "events": list(events)}


class HostFromStringTest(unittest.TestCase):
    CASES = {
        "https://clients2.google.com/service/update2": "clients2.google.com",
        "wss://push.example.org:443/ws": "push.example.org",
        "https://[2001:db8::1]:443/": "2001:db8::1",
        "update.example.net:443": "update.example.net",
        "update.example.net": "update.example.net",
        "ssl/www.gstatic.com:443": "www.gstatic.com",
        "https://a.example:443 <https://b.example same_site>": "a.example",
        "93.184.216.34:80": "93.184.216.34",
    }
    NOT_HOSTS = ["data:text/html,<p>x", "chrome://newtab/", "blob:https://a.example/uuid",
                 "1.0", "443", "GET", "", "not an origin"]

    def test_recognised_shapes(self):
        for value, host in self.CASES.items():
            self.assertEqual(egress_audit.host_from_string(value), host, value)

    def test_non_destinations(self):
        for value in self.NOT_HOSTS:
            self.assertIsNone(egress_audit.host_from_string(value), value)


class ExtractTest(unittest.TestCase):
    def test_finds_hosts_across_event_types_and_ignores_loopback(self):
        found = egress_audit.extract_destinations(netlog(
            event(104, {"url": "http://127.0.0.1:8000/", "method": "GET"}),
            event(104, {"url": "https://clients2.google.com/service/update2", "method": "POST"}),
            event(7, {"host": "https://optimizationguide-pa.googleapis.com:443"}),
            event(51, {"address_list": ["142.250.180.206:443"]}),
            event(300, {"stream_key": {"destination": "https://www.gstatic.com:443"}}),
            event(104, {"url": "http://localhost:9/", "initiator": "not an origin"}),
        ))
        self.assertEqual(sorted(found), ["142.250.180.206", "clients2.google.com",
                                         "optimizationguide-pa.googleapis.com", "www.gstatic.com"])
        self.assertEqual(found["clients2.google.com"].first_event, "URL_REQUEST_START_JOB")

    def test_counts_repeats_and_keeps_few_examples(self):
        found = egress_audit.extract_destinations(netlog(
            *[event(104, {"url": f"https://a.example/{i}"}) for i in range(5)]))
        self.assertEqual(found["a.example"].count, 5)
        self.assertEqual(len(found["a.example"].examples), 3)

    def test_unknown_event_type_is_still_audited(self):
        found = egress_audit.extract_destinations(netlog(event(9999, {"url": "https://x.example/"})))
        self.assertEqual(found["x.example"].first_event, "type 9999")


class LoadTest(unittest.TestCase):
    def write(self, text):
        tmp = tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, encoding="utf-8")
        tmp.write(text)
        tmp.close()
        self.addCleanup(Path(tmp.name).unlink)
        return Path(tmp.name)

    def test_complete_log(self):
        path = self.write(json.dumps(netlog(event(104, {"url": "https://a.example/"}))))
        self.assertEqual(len(egress_audit.load_netlog(path)["events"]), 1)

    def test_log_truncated_by_a_killed_browser(self):
        # FileNetLogObserver writes one event per line; a kill leaves the
        # array open, possibly with a partial final line.
        header = '{"constants": ' + json.dumps(CONSTANTS) + ',\n"events": [\n'
        lines = [json.dumps(event(104, {"url": f"https://h{i}.example/"})) for i in range(2)]
        path = self.write(header + ",\n".join(lines) + ',\n{"source": {"id": 9, "ty')
        loaded = egress_audit.load_netlog(path)
        self.assertEqual(len(loaded["events"]), 2)

    def test_garbage_is_rejected(self):
        with self.assertRaises(ValueError):
            egress_audit.load_netlog(self.write("not json"))


class AllowlistTest(unittest.TestCase):
    ALLOW = [{"host": "update.example.org", "match": "exact", "reason": "updates"},
             {"host": "example.net", "match": "suffix", "reason": "lists"}]

    def test_exact_and_label_suffix(self):
        self.assertTrue(egress_audit.is_allowed("update.example.org", self.ALLOW))
        self.assertFalse(egress_audit.is_allowed("evil.update.example.org", self.ALLOW))
        self.assertTrue(egress_audit.is_allowed("cdn.example.net", self.ALLOW))
        self.assertFalse(egress_audit.is_allowed("evilexample.net", self.ALLOW))

    def test_committed_allowlist_is_empty_in_phase_1(self):
        self.assertEqual(egress_audit.load_allowlist(), [])

    def test_entries_need_a_reason(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "a.json"
            path.write_text(json.dumps({"hosts": [{"host": "a.example"}]}))
            with self.assertRaisesRegex(ValueError, "no reason"):
                egress_audit.load_allowlist(path)


class AuditTest(unittest.TestCase):
    def test_unexpected_hosts_fail_the_audit(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "netlog.json"
            path.write_text(json.dumps(netlog(
                event(104, {"url": "https://update.example.org/check"}),
                event(104, {"url": "https://tracker.example.com/pixel"}))))
            allowed, unexpected = egress_audit.audit(path, AllowlistTest.ALLOW)
        self.assertEqual([d.host for d in allowed], ["update.example.org"])
        self.assertEqual([d.host for d in unexpected], ["tracker.example.com"])
        report = egress_audit.format_report(allowed, unexpected)
        self.assertIn("1 unexpected host(s), 1 allowed.", report)


class RunnerPiecesTest(unittest.TestCase):
    def test_local_page_is_served(self):
        with tempfile.TemporaryDirectory() as tmp:
            server, url = egress_audit.serve_page(Path(tmp))
            try:
                with urllib.request.urlopen(url, timeout=5) as response:
                    self.assertIn(b"local page", response.read())
            finally:
                server.shutdown()
                server.server_close()

    def test_browser_is_launched_like_a_first_run(self):
        args = egress_audit.browser_args(Path("chrome.exe"), Path("p"), Path("n.json"), 9222,
                                         headless=False)
        self.assertIn("--log-net-log=n.json", args)
        self.assertIn("--user-data-dir=p", args)
        self.assertFalse(any(a.startswith("--no-first-run") for a in args))
        self.assertNotIn("--headless", args)


if __name__ == "__main__":
    unittest.main()
