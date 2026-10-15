# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import unittest

import compat_page
from compat_corpus import Step


class Clock:
    def __init__(self):
        self.now = 0.0

    def time(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


class FakeSession:
    """Answers Runtime.evaluate with answer(expression); records the rest."""

    def __init__(self, answer, events=()):
        self.answer = answer
        self.calls = []
        self.events = list(events)

    def call(self, method, **params):
        self.calls.append((method, params))
        if method == "Runtime.evaluate":
            return {"result": {"value": self.answer(params["expression"])}}
        if method == "Page.navigate":
            return {"frameId": "1"}
        return {}

    def methods(self):
        return [m for m, _ in self.calls if m != "Runtime.evaluate"]


def answers(**by_marker):
    """An answer function: the first marker the expression contains decides;
    an expression with none of them gets None (an element that isn't there)."""
    def answer(expression):
        for marker, value in by_marker.items():
            if marker in expression:
                return value
        return None
    return answer


class LoadTest(unittest.TestCase):
    def test_waits_for_complete_then_a_quiet_network(self):
        clock = Clock()
        session = FakeSession(answers(readyState=True))
        self.assertEqual(compat_page.load(session, "https://a.test/", clock.time, clock.sleep),
                         (True, ""))
        self.assertEqual(session.methods()[:4],
                         ["Page.enable", "Runtime.enable", "Network.enable", "Page.navigate"])
        self.assertGreaterEqual(clock.now, compat_page.QUIET)

    def test_a_navigation_error_is_not_loaded(self):
        class Failing(FakeSession):
            def call(self, method, **params):
                if method == "Page.navigate":
                    return {"errorText": "net::ERR_NAME_NOT_RESOLVED"}
                return super().call(method, **params)
        clock = Clock()
        self.assertEqual(compat_page.load(Failing(answers()), "https://a.test/", clock.time,
                                          clock.sleep), (False, "net::ERR_NAME_NOT_RESOLVED"))

    def test_a_page_that_never_completes_times_out(self):
        clock = Clock()
        self.assertEqual(compat_page.load(FakeSession(answers(readyState=False)),
                                          "https://a.test/", clock.time, clock.sleep),
                         (False, "timeout"))
        self.assertGreaterEqual(clock.now, compat_page.LOAD_TIMEOUT)


class ExceptionsTest(unittest.TestCase):
    def test_first_lines_deduplicated(self):
        event = lambda text: {"method": "Runtime.exceptionThrown", "params": {
            "exceptionDetails": {"text": "Uncaught", "exception": {"description": text}}}}
        session = FakeSession(answers(), [
            event("TypeError: a\n    at x.js:1"), event("TypeError: a\n    at y.js:2"),
            {"method": "Runtime.exceptionThrown", "params": {"exceptionDetails": {
                "text": "Uncaught SyntaxError"}}},
            {"method": "Network.requestWillBeSent", "params": {}}])
        self.assertEqual(compat_page.exceptions(session),
                         ("TypeError: a", "Uncaught SyntaxError"))


class StepTest(unittest.TestCase):
    def run_step(self, step, answer):
        clock = Clock()
        session = FakeSession(answer)
        compat_page.run_step(session, step, clock.time, clock.sleep)
        return session

    def test_click_dispatches_a_press_and_a_release_at_the_centre(self):
        session = self.run_step(Step("click", selector="#go"),
                                answers(getBoundingClientRect=[10, 20]))
        mouse = [p for m, p in session.calls if m == "Input.dispatchMouseEvent"]
        self.assertEqual([(p["type"], p["x"], p["y"]) for p in mouse],
                         [("mousePressed", 10, 20), ("mouseReleased", 10, 20)])

    def test_type_clicks_then_types_each_character(self):
        session = self.run_step(Step("type", selector="#q", text="ab"),
                                answers(getBoundingClientRect=[1, 2]))
        keys = [p["key"] for m, p in session.calls if m == "Input.dispatchKeyEvent"]
        self.assertEqual(keys, ["a", "a", "b", "b"])

    def test_press_sends_the_named_key(self):
        session = self.run_step(Step("press", key="Enter"), answers())
        keys = [(p["type"], p.get("windowsVirtualKeyCode")) for m, p in session.calls
                if m == "Input.dispatchKeyEvent"]
        self.assertEqual(keys, [("keyDown", 13), ("keyUp", 13)])

    def test_a_missing_element_fails_the_step_after_its_timeout(self):
        with self.assertRaises(compat_page.StepFailed) as caught:
            self.run_step(Step("expect_visible", selector="#nothing"), answers())
        self.assertIn("#nothing", str(caught.exception))

    def test_click_if_present_passes_without_the_element(self):
        session = self.run_step(Step("click_if_present", selector="#consent"),
                                answers())
        self.assertNotIn("Input.dispatchMouseEvent", session.methods())

    def test_expect_text_looks_for_the_text_in_the_element(self):
        session = self.run_step(Step("expect_text", selector="h1", text="Chromium"),
                                answers(innerText=True))
        expression = session.calls[0][1]["expression"]
        self.assertIn('"h1"', expression)
        self.assertIn('"chromium"', expression)
