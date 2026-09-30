# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import copy
import json
import tempfile
import unittest
import urllib.request
from pathlib import Path

import egress_audit

# Event type ids are assigned per build; the audit must resolve names through
# the constants table rather than assume ids.
CONSTANTS = {"logEventTypes": {"URL_REQUEST_START_JOB": 104, "HOST_RESOLVER_MANAGER_REQUEST": 7,
                               "TCP_CONNECT": 51, "HTTP_STREAM_POOL_ATTEMPT_MANAGER_ALIVE": 300,
                               "UDP_CONNECT": 60, "UDP_LOCAL_ADDRESS": 61, "UDP_BYTES_SENT": 62,
                               "SOCKET_ALIVE": 63, "SOCKET_CONNECT": 64,
                               "HOST_RESOLVER_DNS_TASK": 8, "HOST_RESOLVER_MANAGER_CACHE_HIT": 9},
             "logSourceType": {"URL_REQUEST": 1, "HOST_RESOLVER_IMPL_JOB": 2, "UDP_SOCKET": 20,
                               "UDP_CLIENT_SOCKET": 21}}
URL_REQUEST, RESOLVER_JOB, UDP_SOCKET, UDP_CLIENT_SOCKET = 1, 2, 20, 21


def event(type_id, params, source_id=1, source_type=URL_REQUEST):
    return {"source": {"id": source_id, "type": source_type}, "type": type_id, "time": "1",
            "phase": 1, "params": params}


def udp_socket(source_id, address, wrapper_id=None, sent=False):
    """Events of one UDP_SOCKET source, optionally wrapped by a UDP_CLIENT_SOCKET."""
    events = []
    if wrapper_id is not None:
        events.append(event(63, {"source_dependency": {"id": wrapper_id, "type": 21}},
                            source_id, UDP_SOCKET))
    events.append(event(60, {"address": address}, source_id, UDP_SOCKET))
    if sent:
        events.append(event(62, {"byte_count": 1200}, source_id, UDP_SOCKET))
    return events


def netlog(*events):
    return {"constants": copy.deepcopy(CONSTANTS), "events": list(events)}


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

    def test_own_addresses_are_not_destinations(self):
        # UDP_LOCAL_ADDRESS reports this machine's address; listing it would
        # also leak the user's IP into reports.
        found = egress_audit.extract_destinations(netlog(
            event(61, {"address": "192.168.1.20:57125"}),
            event(61, {"address": "[2a02:db8::1234]:52349"})))
        self.assertEqual(found, {})

    def test_dns_results_are_not_connections(self):
        found = egress_audit.extract_destinations(netlog(event(8, {"results": [
            {"domain_name": "update.example.org",
             "endpoints": [{"address": "203.0.113.7", "port": 0}]}]})))
        self.assertEqual(sorted(found), [])

    def test_connections_are_attributed_to_resolved_names(self):
        found = egress_audit.extract_destinations(netlog(
            event(8, {"results": [{"domain_name": "update.example.org",
                                   "endpoints": [{"address": "203.0.113.7", "port": 0}]}]}),
            event(9, {"results": {"aliases": ["cdn.example.net", "edge.example.net"],
                                  "ip_endpoints": [{"endpoint_address": "2001:db8::5"}]}}),
            event(51, {"address_list": ["203.0.113.7:443"]}),
            event(60, {"address": "[2001:db8::5]:443"}),
            event(60, {"address": "198.51.100.9:443"})))
        self.assertEqual({k: v.kind for k, v in found.items()},
                         {"update.example.org": "host", "cdn.example.net": "host",
                          "198.51.100.9": "ip"})

    def test_dns_resolver_is_reported_separately_and_does_not_fail(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "netlog.json"
            path.write_text(json.dumps(netlog(event(60, {"address": "[2001:db8::53]:53"}))))
            result = egress_audit.audit(path, [])
        self.assertEqual([d.host for d in result.dns], ["2001:db8::53"])
        self.assertEqual(result.unexpected, [])

    def test_udp_connect_without_datagrams_is_a_route_probe(self):
        # The shape HostResolverManager's IPv6 reachability probe leaves: the
        # wrapper and its socket both connect, nothing is sent.
        log = netlog(event(64, {"address": "[2001:4860:4860::8888]:443"}, 12, UDP_CLIENT_SOCKET),
                     *udp_socket(13, "[2001:4860:4860::8888]:443", wrapper_id=12))
        found = egress_audit.extract_destinations(log)
        self.assertEqual(found["2001:4860:4860::8888"].kind, "probe")
        self.assertEqual(found["2001:4860:4860::8888"].count, 2)

    def test_udp_socket_that_sent_data_is_traffic(self):
        # QUIC: the datagrams are logged on the socket, the connect on both it
        # and its wrapper; neither may be taken for a probe.
        log = netlog(event(64, {"address": "198.51.100.9:443"}, 12, UDP_CLIENT_SOCKET),
                     *udp_socket(13, "198.51.100.9:443", wrapper_id=12, sent=True))
        found = egress_audit.extract_destinations(log)
        self.assertEqual(found["198.51.100.9"].kind, "ip")
        self.assertEqual(found["198.51.100.9"].count, 2)

    def test_only_udp_sockets_can_be_probes(self):
        # A TCP connect sends a SYN, so it is traffic even if no data follows.
        found = egress_audit.extract_destinations(netlog(
            event(51, {"address_list": ["198.51.100.9:443"]}, 5, RESOLVER_JOB)))
        self.assertEqual(found["198.51.100.9"].kind, "ip")

    def test_route_probes_are_reported_separately_and_do_not_fail(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "netlog.json"
            path.write_text(json.dumps(netlog(*udp_socket(13, "[2001:db8::1]:443"))))
            result = egress_audit.audit(path, [])
        self.assertEqual([d.host for d in result.probes], ["2001:db8::1"])
        self.assertEqual(result.unexpected, [])
        self.assertIn("0 DNS resolver(s) and 1 route probe(s) (not counted).",
                      egress_audit.format_report(result))

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
            result = egress_audit.audit(path, AllowlistTest.ALLOW)
        self.assertEqual([d.host for d in result.allowed], ["update.example.org"])
        self.assertEqual([d.host for d in result.unexpected], ["tracker.example.com"])
        report = egress_audit.format_report(result)
        self.assertIn("1 unexpected, 1 allowed, 0 DNS resolver(s) and 0 route probe(s) "
                      "(not counted).", report)


class PhaseTest(unittest.TestCase):
    PHASES = [("startup", 1_000_000), ("idle", 1_005_000), ("login", 1_600_000)]

    def test_first_sighting_is_dated_in_unix_time(self):
        # Event times are TimeTicks in ms; timeTickOffset converts them.
        log = netlog(event(104, {"url": "https://a.example/"}))
        log["constants"]["timeTickOffset"] = 1_000_000
        log["events"][0]["time"] = "600123"
        found = egress_audit.extract_destinations(log)
        self.assertEqual(found["a.example"].first_time, 1_600_123)

    def test_logs_without_an_offset_are_undated(self):
        found = egress_audit.extract_destinations(netlog(event(104, {"url": "https://a.example/"})))
        self.assertIsNone(found["a.example"].first_time)

    def test_phase_at(self):
        self.assertEqual(egress_audit.phase_at(1_004_999, self.PHASES), "startup")
        self.assertEqual(egress_audit.phase_at(1_005_000, self.PHASES), "idle")
        self.assertEqual(egress_audit.phase_at(9_999_999, self.PHASES), "login")
        self.assertIsNone(egress_audit.phase_at(999_999, self.PHASES))
        self.assertIsNone(egress_audit.phase_at(None, self.PHASES))

    def test_report_names_the_phase_of_the_first_sighting(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "netlog.json"
            log = netlog(event(104, {"url": "https://leak.example/lookup"}))
            log["constants"]["timeTickOffset"] = 1_000_000
            log["events"][0]["time"] = "600000"
            path.write_text(json.dumps(log))
            result = egress_audit.audit(path, [])
        report = egress_audit.format_report(result, self.PHASES)
        self.assertIn("first seen in URL_REQUEST_START_JOB during login", report)
        self.assertNotIn("during", egress_audit.format_report(result))


class SiteTest(unittest.TestCase):
    def setUp(self):
        self.server, self.url = egress_audit.serve_site()
        self.addCleanup(self.server.server_close)
        self.addCleanup(self.server.shutdown)

    def fetch(self, path, data=None):
        with urllib.request.urlopen(self.url + path, data=data, timeout=5) as response:
            return response.status, response.read().decode("utf-8")

    def test_local_page_is_served(self):
        self.assertIn("local page", self.fetch("")[1])

    def test_form_submissions_land_on_a_page_without_the_form(self):
        # The password manager takes a login as successful when the page
        # after the submission no longer shows the password field.
        for scenario in egress_audit.form_scenarios(password="pw"):
            status, body = self.fetch(scenario.page)
            action = FormParser.parse(body).action
            status, body = self.fetch(action.lstrip("/"), data=b"a=1")
            self.assertEqual(status, 200, scenario.name)
            self.assertNotIn("<input", body, scenario.name)


class FormParser(egress_audit.html.parser.HTMLParser):
    """Collects the ids of inputs and the form action of a page."""

    @classmethod
    def parse(cls, text):
        parser = cls()
        parser.inputs, parser.action, parser.urls = [], None, []
        parser.feed(text)
        return parser

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag == "input":
            self.inputs.append(attrs.get("id"))
        if tag == "form":
            self.action = attrs.get("action")
        self.urls += [v for k, v in attrs.items() if k in ("src", "href", "action") and v]


class ScenarioTest(unittest.TestCase):
    def test_scenarios_type_into_fields_their_page_has(self):
        for scenario in egress_audit.form_scenarios(password="pw"):
            page = FormParser.parse((egress_audit.SITE / scenario.page).read_text("utf-8"))
            self.assertEqual([f for f, _ in scenario.fields], page.inputs, scenario.name)

    def test_site_never_refers_to_another_host(self):
        # A page that loads anything from the network would put hosts in the
        # log that the browser did not choose to contact.
        for page in egress_audit.SITE.glob("*.html"):
            for url in FormParser.parse(page.read_text("utf-8")).urls:
                self.assertTrue(url.startswith("/") and not url.startswith("//"), (page, url))

    def test_login_uses_the_password_it_is_given(self):
        login = {s.name: s for s in egress_audit.form_scenarios(password="s3cret")}["login"]
        self.assertIn(("password", "s3cret"), login.fields)

    def test_typing_sends_a_key_down_and_up_per_character(self):
        self.assertEqual(egress_audit.key_events("ab"), [
            {"type": "keyDown", "key": "a", "text": "a", "unmodifiedText": "a"},
            {"type": "keyUp", "key": "a"},
            {"type": "keyDown", "key": "b", "text": "b", "unmodifiedText": "b"},
            {"type": "keyUp", "key": "b"}])

    def test_enter_submits_like_the_key(self):
        down, up = egress_audit.ENTER
        self.assertEqual((down["type"], down["windowsVirtualKeyCode"], down["text"]),
                         ("keyDown", 13, "\r"))
        self.assertEqual(up["type"], "keyUp")

    def test_scenarios_are_chosen_on_the_command_line(self):
        self.assertEqual(egress_audit.parse_scenarios("login,address"), ["login", "address"])
        self.assertEqual(egress_audit.parse_scenarios("all"), ["address", "login"])
        with self.assertRaises(egress_audit.argparse.ArgumentTypeError):
            egress_audit.parse_scenarios("login,card")


class RunnerPiecesTest(unittest.TestCase):

    def test_devtools_text_replies_are_not_parsed_as_json(self):
        # /json/close answers "Target is closing"; parsing it crashed the
        # first real run after the idle period.
        class TextHandler(egress_audit.http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                self.send_response(200)
                self.end_headers()
                self.wfile.write(b"Target is closing")

            def log_message(self, *args):
                pass

        server = egress_audit.http.server.ThreadingHTTPServer(("127.0.0.1", 0), TextHandler)
        egress_audit.threading.Thread(target=server.serve_forever, daemon=True).start()
        try:
            port = server.server_address[1]
            self.assertEqual(egress_audit._devtools(port, "/json/close/x", parse=False),
                             "Target is closing")
            with self.assertRaises(ValueError):
                egress_audit._devtools(port, "/json/close/x")
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
