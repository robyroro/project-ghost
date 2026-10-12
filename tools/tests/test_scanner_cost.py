# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import unittest

import scanner_cost


class ArithmeticTest(unittest.TestCase):
    def test_deltas_are_milliseconds_of_each_metric(self):
        before = {"ScriptDuration": 1.0, "TaskDuration": 2.0, "Other": 9.0}
        after = {"ScriptDuration": 1.0125, "TaskDuration": 2.05, "Other": 10.0}
        deltas = scanner_cost.metric_deltas(before, after)
        self.assertEqual(set(deltas), {"ScriptDuration", "TaskDuration"})
        self.assertAlmostEqual(deltas["ScriptDuration"], 12.5)
        self.assertAlmostEqual(deltas["TaskDuration"], 50.0)

    def test_medians_per_metric(self):
        rows = [{"ScriptDuration": s, "TaskDuration": t} for s, t in ((3, 30), (1, 10), (2, 20))]
        self.assertEqual(scanner_cost.medians(rows), {"ScriptDuration": 2, "TaskDuration": 20})


class OffPreferencesTest(unittest.TestCase):
    def test_sets_the_served_site_off(self):
        self.assertEqual(scanner_cost.off_preferences(),
                         {"ghost": {"privacy_policy": {"site_levels": {"cosmetic.test": "off"}}}})


class CommandLineTest(unittest.TestCase):
    def test_quotes_paths_with_spaces(self):
        line = scanner_cost.command_line(["C:/Program Files/x/chrome.exe", "--flag=a b", "about:blank"])
        self.assertEqual(line, '"C:/Program Files/x/chrome.exe" "--flag=a b" about:blank')


if __name__ == "__main__":
    unittest.main()
