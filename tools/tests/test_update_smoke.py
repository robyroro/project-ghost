# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import json
import unittest

import update_smoke


class RequestLogTest(unittest.TestCase):
    def test_allowed_requests_pass(self):
        lines = [json.dumps({"path": "/update?cup2key=1:2",
                             "body": {"request": {"protocol": "4.0", "apps": []}}})]
        self.assertEqual(update_smoke.evaluate_requests(lines), [])

    def test_forbidden_keys_and_no_requests_fail(self):
        lines = [json.dumps({"path": "/update",
                             "body": {"request": {"protocol": "4.0", "hw": {}}}})]
        self.assertEqual(update_smoke.evaluate_requests(lines),
                         ["request 1 carries request.hw"])
        self.assertEqual(update_smoke.evaluate_requests([]), ["the updater sent no request"])

    def test_event_requests_fail_even_when_scrubbed(self):
        # The scrubber removes an app's `event` list, so an event request
        # reaches the server as an app without `updatecheck`.
        check = {"appid": "{a}", "version": "1", "updatecheck": {}}
        event = {"appid": "{b}", "version": "1", "enabled": True}
        lines = [json.dumps({"path": "/update?cup2key=1:2",
                             "body": {"request": {"protocol": "4.0", "apps": [check]}}}),
                 json.dumps({"path": "/update",
                             "body": {"request": {"protocol": "4.0", "apps": [event]}}})]
        self.assertEqual(update_smoke.evaluate_requests(lines),
                         ["request 2 is not an update check: app {b} has no updatecheck"
                          " (an event request)"])


class UpdaterLogTest(unittest.TestCase):
    def test_only_loopback_urls(self):
        log = "x http://127.0.0.1:8484/update y\nz https://example.com/a\n"
        self.assertEqual(update_smoke.foreign_urls(log), ["https://example.com/a"])

    def test_xml_namespaces_are_not_requests(self):
        # The updater logs its scheduled task's XML, whose namespace is a URI.
        log = '<Task xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">'
        self.assertEqual(update_smoke.foreign_urls(log), [])


class RemoteModeTest(unittest.TestCase):
    def test_the_server_is_an_allowed_host(self):
        log = ("a https://203.0.113.5/update b https://203.0.113.5/releases/x.crx3\n"
               "c http://127.0.0.1:8484/update d https://example.com/a\n")
        self.assertEqual(update_smoke.foreign_urls(log, ("https://203.0.113.5",)),
                         ["http://127.0.0.1:8484/update", "https://example.com/a"])

    def test_client_address(self):
        self.assertEqual(update_smoke.client_address("198.51.100.7 51234 203.0.113.5 22\n"),
                         "198.51.100.7")
        with self.assertRaises(ValueError):
            update_smoke.client_address("")

    def test_the_sandbox_gets_network_only_when_asked(self):
        import installer_smoke
        from pathlib import Path
        args = (Path("i"), Path("t"), Path("p"), Path("r"))
        self.assertIn("<Networking>Disable</Networking>", installer_smoke.sandbox_config(*args))
        self.assertIn("<Networking>Enable</Networking>",
                      installer_smoke.sandbox_config(*args, networking=True))

    def test_argument_combinations(self):
        def problem(*argv):
            return update_smoke.argument_problem(update_smoke.parser().parse_args(
                ["sandbox", "--release-version", "1.0.0.1", "--appid", "{a}", *argv]))
        self.assertIsNone(problem("--offline-installer", "o.exe", "--update-crx", "u.crx3",
                                  "--update-version", "1.0.0.2"))
        self.assertIsNone(problem("--offline-installer", "o.exe", "--update-version", "1.0.0.2",
                                  "--server", "https://203.0.113.5"))
        self.assertIsNone(problem("--online-installer", "u.exe", "--server",
                                  "https://203.0.113.5"))
        for argv in ((), ("--offline-installer", "o.exe", "--online-installer", "u.exe"),
                     ("--offline-installer", "o.exe", "--update-version", "1.0.0.2"),
                     ("--online-installer", "u.exe"),
                     ("--online-installer", "u.exe", "--server", "https://h",
                      "--update-version", "1.0.0.2")):
            with self.subTest(argv):
                self.assertIsNotNone(problem(*argv))


if __name__ == "__main__":
    unittest.main()
