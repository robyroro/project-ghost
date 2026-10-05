# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import datetime
import json
import tempfile
import unittest
from pathlib import Path

import release_state

T0 = datetime.datetime(2026, 10, 5, 9, 0, tzinfo=datetime.timezone.utc)
T1 = datetime.datetime(2026, 10, 5, 21, 30, tzinfo=datetime.timezone.utc)


class StateTest(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.path = Path(tmp.name) / "152.0.7977.149-1" / "state.json"

    def test_a_new_release_has_done_nothing(self):
        state = release_state.State.load(self.path, "152.0.7977.149-1")
        self.assertFalse(state.is_done("build", {"commit": "a" * 40}))
        self.assertFalse(self.path.exists())

    def test_a_stage_recorded_with_the_same_inputs_is_done_after_a_reload(self):
        state = release_state.State.load(self.path, "152.0.7977.149-1")
        state.record("build", {"commit": "a" * 40}, {"mini_installer.exe": "b" * 64}, T0, T1)
        again = release_state.State.load(self.path, "152.0.7977.149-1")
        self.assertTrue(again.is_done("build", {"commit": "a" * 40}))
        self.assertEqual(again.outputs("build"), {"mini_installer.exe": "b" * 64})
        doc = json.loads(self.path.read_text(encoding="utf-8"))
        self.assertEqual(doc["stages"]["build"]["started"], "2026-10-05T09:00:00+00:00")
        self.assertEqual(doc["stages"]["build"]["finished"], "2026-10-05T21:30:00+00:00")

    def test_changed_inputs_mean_the_stage_runs_again(self):
        state = release_state.State.load(self.path, "152.0.7977.149-1")
        state.record("build", {"commit": "a" * 40}, {}, T0, T1)
        self.assertFalse(state.is_done("build", {"commit": "c" * 40}))

    def test_forget(self):
        state = release_state.State.load(self.path, "152.0.7977.149-1")
        state.record("sign", {}, {}, T0, T1)
        state.forget("sign")
        self.assertFalse(release_state.State.load(self.path, "152.0.7977.149-1")
                         .is_done("sign", {}))

    def test_another_releases_state_is_refused(self):
        release_state.State.load(self.path, "152.0.7977.149-1").record("sync", {}, {}, T0, T1)
        with self.assertRaisesRegex(release_state.StateError, "152.0.7977.149-1"):
            release_state.State.load(self.path, "152.0.7977.149-2")

    def test_the_write_is_atomic(self):
        release_state.State.load(self.path, "152.0.7977.149-1").record("sync", {}, {}, T0, T1)
        self.assertEqual([p.name for p in self.path.parent.iterdir()], ["state.json"])
