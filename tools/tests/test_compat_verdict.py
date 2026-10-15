# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import unittest

import compat_verdict as v
from compat_verdict import Signals

FINE = Signals(True, 200, ("TypeError: x is undefined",), 5000, 3000)


class JudgeTest(unittest.TestCase):
    def test_the_same_page_passes(self):
        self.assertEqual(v.judge(FINE, FINE), (v.PASSED, []))

    def test_a_baseline_that_fails_skips_the_site(self):
        for baseline, words in ((Signals(False, error="timeout"), "didn't load (timeout)"),
                                (Signals(True, 403, (), 10, 10), "HTTP 403"),
                                (Signals(True, 200, (), 10, 10, failed_step=1, error="no #q"),
                                 "failed step 2 (no #q)")):
            verdict, why = v.judge(FINE, baseline)
            self.assertEqual(verdict, v.SKIPPED)
            self.assertIn(words, why[0])

    def test_shade_not_loading_is_broken(self):
        self.assertEqual(v.judge(Signals(False, error="ERR_BLOCKED_BY_CLIENT"), FINE),
                         (v.BROKEN, ["didn't load (ERR_BLOCKED_BY_CLIENT)"]))

    def test_a_failed_flow_is_broken(self):
        shade = Signals(True, 200, FINE.exceptions, 5000, 3000, failed_step=0, error="no #q")
        self.assertEqual(v.judge(shade, FINE), (v.BROKEN, ["failed step 1 (no #q)"]))

    def test_a_new_exception_is_broken_an_old_one_isnt(self):
        shade = Signals(True, 200, FINE.exceptions + ("ReferenceError: ga is not defined",),
                        5000, 3000)
        self.assertEqual(v.judge(shade, FINE),
                         (v.BROKEN, ["new exception(s): ReferenceError: ga is not defined"]))

    def test_text_and_height_under_half_are_broken(self):
        self.assertEqual(v.judge(Signals(True, 200, FINE.exceptions, 2500, 1500), FINE)[0],
                         v.PASSED)
        verdict, why = v.judge(Signals(True, 200, FINE.exceptions, 2499, 1499), FINE)
        self.assertEqual(verdict, v.BROKEN)
        self.assertEqual(why, ["text 2499 against 5000", "height 1499 against 3000"])


class SettleTest(unittest.TestCase):
    BROKEN_TEXT = (v.BROKEN, ["text 10 against 5000"])

    def test_only_a_broken_site_is_rerun(self):
        self.assertEqual(v.settle((v.PASSED, []), None, ""), (v.PASSED, []))
        with self.assertRaises(ValueError):
            v.settle(self.BROKEN_TEXT, None, "")

    def test_broken_twice_is_broken_then_passing_is_flaky(self):
        self.assertEqual(v.settle(self.BROKEN_TEXT, self.BROKEN_TEXT, ""), self.BROKEN_TEXT)
        self.assertEqual(v.settle(self.BROKEN_TEXT, (v.PASSED, []), ""),
                         (v.FLAKY, ["text 10 against 5000"]))

    def test_a_rerun_the_baseline_fails_is_skipped(self):
        self.assertEqual(v.settle(self.BROKEN_TEXT, (v.SKIPPED, ["the baseline got HTTP 500"]),
                                  ""), (v.SKIPPED, ["the baseline got HTTP 500"]))

    def test_a_reason_excuses_signals_but_never_a_flow_or_a_load(self):
        self.assertEqual(v.settle(self.BROKEN_TEXT, self.BROKEN_TEXT, "ads only"),
                         (v.EXCUSED, ["text 10 against 5000", "excused: ads only"]))
        for why in (["failed step 1 (no #q)"], ["didn't load (timeout)"]):
            self.assertEqual(v.settle((v.BROKEN, why), (v.BROKEN, why), "ads only"),
                             (v.BROKEN, why))


class RateAndReportTest(unittest.TestCase):
    def result(self, url, verdict, why=()):
        return {"url": url, "category": "news", "verdict": verdict, "why": list(why),
                "shade": vars_of(FINE), "baseline": vars_of(FINE)}

    def test_skipped_sites_are_left_out_of_the_rate(self):
        results = [self.result("https://a/", v.PASSED), self.result("https://b/", v.FLAKY),
                   self.result("https://c/", v.EXCUSED), self.result("https://d/", v.BROKEN),
                   self.result("https://e/", v.SKIPPED)]
        self.assertEqual(v.rate(results), 0.75)
        self.assertIsNone(v.rate([self.result("https://e/", v.SKIPPED)]))

    def test_the_report_lists_what_isnt_passed(self):
        report = v.report({"standard": [
            self.result("https://a/", v.PASSED),
            self.result("https://d/", v.BROKEN, ["failed step 1 (no #q)"]),
            self.result("https://e/", v.SKIPPED, ["the baseline got HTTP 500"])]})
        self.assertIn("## Standard: 50.0% (1 of 2), 1 broken", report)
        self.assertIn("- https://d/ (news): failed step 1 (no #q)", report)
        self.assertIn("Shade: loaded, HTTP 200, 1 exception(s), text 5000, height 3000", report)
        self.assertIn("### Skipped\n\n- https://e/ (news): the baseline got HTTP 500", report)
        self.assertNotIn("https://a/", report)

    def test_signals_round_trip_through_json(self):
        self.assertEqual(Signals.from_dict(vars_of(FINE)), FINE)


def vars_of(signals: Signals) -> dict:
    return {**signals.__dict__, "exceptions": list(signals.exceptions)}


if __name__ == "__main__":
    unittest.main()
