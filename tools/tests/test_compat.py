# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import contextlib
import hashlib
import io
import json
import shutil
import tempfile
import unittest
import zipfile
from pathlib import Path

import compat
import compat_verdict as v
from compat_corpus import Site
from compat_verdict import Signals


def chrome_zip() -> bytes:
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w") as z:
        z.writestr("chrome-win64/chrome.exe", b"MZ fake")
    return buffer.getvalue()


class BaselineTest(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.dir, ignore_errors=True)
        self.record = self.dir / "baseline.json"
        self.data = chrome_zip()
        self.urls = []

    def download(self, url, path):
        self.urls.append(url)
        path.write_bytes(self.data)

    def fetch(self):
        with contextlib.redirect_stdout(io.StringIO()):  # the "recorded" notice
            return compat.fetch_baseline("152.0.7977.167", self.dir / "cft", self.record,
                                         self.download)

    def test_a_new_version_is_recorded_and_unpacked(self):
        exe = self.fetch()
        self.assertEqual(self.urls, ["https://storage.googleapis.com/chrome-for-testing-public/"
                                     "152.0.7977.167/win64/chrome-win64.zip"])
        self.assertEqual(exe.read_bytes(), b"MZ fake")
        self.assertEqual(json.loads(self.record.read_text()),
                         {"version": "152.0.7977.167",
                          "sha256": hashlib.sha256(self.data).hexdigest(),
                          "url": self.urls[0]})

    def test_a_recorded_version_must_match(self):
        self.record.write_text(json.dumps({"version": "152.0.7977.167", "sha256": "0" * 64}))
        with self.assertRaises(compat.BaselineError):
            self.fetch()

    def test_an_unpacked_baseline_is_reused(self):
        self.fetch()
        self.fetch()
        self.assertEqual(len(self.urls), 1)


class ProfileTest(unittest.TestCase):
    def test_the_level_is_shades_default_level(self):
        self.assertEqual(compat.preferences("strict"),
                         {"ghost": {"privacy_policy": {"default_level": "strict"}}})

    def test_browser_arguments(self):
        args = compat.browser_args(Path("C:/x/chrome.exe"), Path("C:/p"), 9400,
                                   ["--host-resolver-rules=MAP a.test 127.0.0.1"])
        self.assertEqual(args, [str(Path("C:/x/chrome.exe")), f"--user-data-dir={Path('C:/p')}",
                                "--no-first-run", "--no-default-browser-check",
                                "--remote-debugging-port=9400",
                                "--host-resolver-rules=MAP a.test 127.0.0.1", "about:blank"])


class RunSiteTest(unittest.TestCase):
    FINE = Signals(True, 200, (), 5000, 3000)

    def run_site(self, shade_runs, baseline_runs, reason=""):
        shade, baseline = iter(shade_runs), iter(baseline_runs)
        return compat.run_site(Site("https://a.test/", "news", reason=reason),
                               lambda site: next(shade), lambda site: next(baseline))

    def test_a_passing_site_runs_once(self):
        result = self.run_site([self.FINE], [self.FINE])
        self.assertEqual((result["verdict"], result["rerun"]), (v.PASSED, None))

    def test_a_broken_site_runs_again(self):
        broken = Signals(False, error="timeout")
        result = self.run_site([broken, self.FINE], [self.FINE, self.FINE])
        self.assertEqual(result["verdict"], v.FLAKY)
        self.assertEqual(result["rerun"]["shade"], self.FINE.to_dict())

    def test_a_browser_that_fails_counts_against_its_side(self):
        def boom(site):
            raise ConnectionError("DevTools closed")
        shade_fails = compat.run_site(Site("https://a.test/", "news"), boom,
                                      lambda site: self.FINE)
        self.assertEqual(shade_fails["verdict"], v.BROKEN)
        self.assertIn("browser: DevTools closed", shade_fails["why"][0])
        baseline_fails = compat.run_site(Site("https://a.test/", "news"),
                                         lambda site: self.FINE, boom)
        self.assertEqual(baseline_fails["verdict"], v.SKIPPED)


class SelfTestTest(unittest.TestCase):
    def test_the_self_test_sites(self):
        blocked, fine = compat.self_test_sites("http://compat.test:8000")
        self.assertEqual(blocked.url, "http://compat.test:8000/blocked.html")
        self.assertEqual(fine.url, "http://compat.test:8000/fine.html")
        self.assertEqual(blocked.flow, fine.flow)

    def test_the_self_test_expects_broken_then_passed(self):
        good = [{"url": "x/blocked.html", "verdict": v.BROKEN},
                {"url": "x/fine.html", "verdict": v.PASSED}]
        self.assertEqual(compat.self_test_problems(good), [])
        bad = [{"url": "x/blocked.html", "verdict": v.PASSED},
               {"url": "x/fine.html", "verdict": v.PASSED}]
        self.assertEqual(compat.self_test_problems(bad),
                         ["x/blocked.html: passed, expected broken"])


class SelectTest(unittest.TestCase):
    SITES = [Site("https://a.test/", "news"), Site("https://b.test/", "video"),
             Site("https://c.test/", "news")]

    def test_by_url_and_category(self):
        self.assertEqual(compat.select(self.SITES, ["https://b.test/"], []), [self.SITES[1]])
        self.assertEqual(compat.select(self.SITES, [], ["news"]),
                         [self.SITES[0], self.SITES[2]])
        self.assertEqual(compat.select(self.SITES, [], []), self.SITES)

    def test_an_unknown_url_is_an_error(self):
        with self.assertRaises(ValueError):
            compat.select(self.SITES, ["https://z.test/"], [])


if __name__ == "__main__":
    unittest.main()
