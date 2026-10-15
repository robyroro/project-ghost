# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""One corpus site in one browser tab, over DevTools (Phase 4D-1).

load() navigates and waits for the load, then for the network to stay quiet;
signals() reads what the verdict needs; run_step() acts as a user would,
through DevTools' input pipeline, so pages see real clicks and key presses.
The clock and sleep are parameters, so the tests run without waiting.
"""

from __future__ import annotations

import json
import time
from typing import Callable

import cdp
from compat_corpus import Step
from egress_audit import key_events

LOAD_TIMEOUT = 30.0  # navigation to a quiet network, at most
QUIET = 2.0  # no request started for this long
STEP_TIMEOUT = 10.0
CONSENT_TIMEOUT = 3.0  # how long click_if_present waits for its element
POLL = 0.25
# Key name: (Windows virtual key code, text it types).
KEYS = {"Enter": (13, "\r"), "Escape": (27, ""), "Tab": (9, "\t"), "ArrowDown": (40, "")}
SIGNALS = ("(() => ({status: performance.getEntriesByType('navigation')[0]?.responseStatus ?? 0,"
           " text: document.body ? document.body.innerText.length : 0,"
           " height: document.documentElement.scrollHeight}))()")

Clock = Callable[[], float]
Sleep = Callable[[float], None]


class StepFailed(Exception):
    """A flow step didn't succeed within its timeout."""


def evaluate(session: cdp.Session, expression: str):
    reply = session.call("Runtime.evaluate", expression=expression, returnByValue=True)
    if "exceptionDetails" in reply:
        raise cdp.ProtocolError(f"{expression}: {reply['exceptionDetails'].get('text')}")
    return reply["result"].get("value")


def poll(session: cdp.Session, expression: str, timeout: float, clock: Clock,
         sleep: Sleep) -> bool:
    deadline = clock() + timeout
    while True:
        try:
            if evaluate(session, expression):
                return True
        except cdp.ProtocolError:
            pass  # the page is between documents
        if clock() >= deadline:
            return False
        sleep(POLL)


def _requests(session: cdp.Session) -> int:
    return sum(1 for e in session.events if e.get("method") == "Network.requestWillBeSent")


def load(session: cdp.Session, url: str, clock: Clock = time.monotonic,
         sleep: Sleep = time.sleep) -> tuple[bool, str]:
    """Navigates; (loaded, error). Loaded once complete, then quiet or out of time."""
    for domain in ("Page", "Runtime", "Network"):
        session.call(f"{domain}.enable")
    reply = session.call("Page.navigate", url=url)
    if reply.get("errorText") == "net::ERR_ABORTED":
        # The tab's own navigation to about:blank may commit after ours
        # started, which aborts ours: in Shade the cosmetic filters' throttle
        # holds that commit until the engine answers. Once more, then.
        sleep(1.0)
        reply = session.call("Page.navigate", url=url)
    if reply.get("errorText"):
        return False, reply["errorText"]
    start = clock()
    if not poll(session, "document.readyState === 'complete'", LOAD_TIMEOUT, clock, sleep):
        return False, "timeout"
    seen, quiet_since = _requests(session), clock()
    while clock() - quiet_since < QUIET and clock() - start < LOAD_TIMEOUT:
        sleep(POLL)
        evaluate(session, "0")  # a reply drains the events that came before it
        now = _requests(session)
        if now != seen:
            seen, quiet_since = now, clock()
    return True, ""


def exceptions(session: cdp.Session) -> tuple[str, ...]:
    """The uncaught exceptions so far: each one's first line, once, sorted."""
    found = set()
    for event in session.events:
        if event.get("method") != "Runtime.exceptionThrown":
            continue
        details = event["params"]["exceptionDetails"]
        text = details.get("exception", {}).get("description") or details.get("text", "")
        found.add(text.splitlines()[0][:200] if text else "")
    return tuple(sorted(found))


def signals(session: cdp.Session) -> dict:
    """The main document's status, the page's text length and height."""
    return evaluate(session, SIGNALS)


def _element(selector: str, test: str) -> str:
    return (f"(() => {{ const e = document.querySelector({json.dumps(selector)});"
            f" return !!e && ({test}); }})()")


def _visible(selector: str) -> str:
    return _element(selector, "(r => r.width > 0 && r.height > 0)(e.getBoundingClientRect())"
                              " && getComputedStyle(e).visibility !== 'hidden'")


def _click(session: cdp.Session, selector: str) -> None:
    x, y = evaluate(session, f"(() => {{ const e = document.querySelector("
                             f"{json.dumps(selector)}); e.scrollIntoView({{block: 'center'}});"
                             f" const r = e.getBoundingClientRect();"
                             f" return [r.x + r.width / 2, r.y + r.height / 2]; }})()")
    for kind in ("mousePressed", "mouseReleased"):
        session.call("Input.dispatchMouseEvent", type=kind, x=x, y=y, button="left",
                     clickCount=1)


def _press(session: cdp.Session, key: str) -> None:
    code, text = KEYS[key]
    down = {"type": "keyDown", "key": key, "code": key, "windowsVirtualKeyCode": code}
    if text:
        down.update(text=text, unmodifiedText=text)
    session.call("Input.dispatchKeyEvent", **down)
    session.call("Input.dispatchKeyEvent", type="keyUp", key=key, code=key,
                 windowsVirtualKeyCode=code)


def run_step(session: cdp.Session, step: Step, clock: Clock = time.monotonic,
             sleep: Sleep = time.sleep) -> None:
    """Does one flow step; raises StepFailed when it can't within its timeout."""
    def wait(expression: str, what: str, timeout: float = STEP_TIMEOUT) -> None:
        if not poll(session, expression, timeout, clock, sleep):
            raise StepFailed(f"{step.action}: {what} not within {timeout:g} s")

    if step.action == "click_if_present":
        if poll(session, _visible(step.selector), CONSENT_TIMEOUT, clock, sleep):
            _click(session, step.selector)
            sleep(1.0)  # the banner goes, the page may reload
    elif step.action == "click":
        wait(_visible(step.selector), f"{step.selector} visible")
        _click(session, step.selector)
    elif step.action == "type":
        wait(_visible(step.selector), f"{step.selector} visible")
        _click(session, step.selector)
        for key in key_events(step.text):
            session.call("Input.dispatchKeyEvent", **key)
    elif step.action == "press":
        _press(session, step.key)
    elif step.action == "scroll":
        session.call("Input.dispatchMouseEvent", type="mouseWheel", x=200, y=200, deltaX=0,
                     deltaY=2000)
        sleep(1.0)
    elif step.action == "wait_for":
        wait(_element(step.selector, "true"), f"{step.selector} present")
    elif step.action == "expect_visible":
        wait(_visible(step.selector), f"{step.selector} visible")
    elif step.action == "expect_text":
        wait(_element(step.selector, f"e.innerText.toLowerCase().includes("
                                     f"{json.dumps(step.text.lower())})"),
             f"{step.text!r} in {step.selector}")
    else:
        raise ValueError(f"unknown action {step.action}")
