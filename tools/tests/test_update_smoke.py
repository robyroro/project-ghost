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


class UpdaterLogTest(unittest.TestCase):
    def test_only_loopback_urls(self):
        log = "x http://127.0.0.1:8484/update y\nz https://example.com/a\n"
        self.assertEqual(update_smoke.foreign_urls(log), ["https://example.com/a"])

    def test_xml_namespaces_are_not_requests(self):
        # The updater logs its scheduled task's XML, whose namespace is a URI.
        log = '<Task xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">'
        self.assertEqual(update_smoke.foreign_urls(log), [])


if __name__ == "__main__":
    unittest.main()
