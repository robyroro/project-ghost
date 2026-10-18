# Breakage controls (4D-2): progress notes

What executing [the plan](../plans/2026-10-18-breakage-controls.md) found, task by task. The design is [here](2026-10-18-breakage-controls-design.md).

## Tasks 1–2: an engine that names its rules (2026-10-18)

- adblock-rust 0.13 keeps a rule's text in debug mode (`FilterSet::new(true)`): the explaining engine returns `||tracker.test^$third-party` exactly, and decides as the filtering engine does on blocks and exceptions (`EngineBridgeTest.AnExplainingEngine*`). The filtering engine still reports the rule's `Debug` form to the console.
- `ListLabel` reads the header only; the shipped lists are "EasyList 202610100609", "EasyPrivacy 202610100609" and "Shade's fixes".

## Tasks 3–7: the blocked requests, the report, the guided step (2026-10-18)

- `BlockingService` gained a member that isn't trivial (the test lists), so Chromium's style plugin asks for an out-of-line destructor; `ReportInputs` and `BreakageReport` likewise get out-of-line constructors.
- `net::GetValueForKeyInQuery` leaves `%0A` escaped: it doesn't decode control characters. The issue URL is right, and GitHub decodes it; the unit test decodes values itself (`base::UnescapeBinaryURLComponent`).
- In `ChromeRenderViewHostTestHarness`, `NavigationSimulator::CreateFromPending(...)->Commit()` finishes the reload's loading, so `DidStopLoading` asks as in the browser: the plan's fallback wasn't needed.

## Tasks 8–10: the panel (2026-10-18)

- **Native buttons, not `cr-button`.** The panel uses native controls since 3D-3 (radios); a `cr-button` would have added `cr_elements` to its build for four buttons. They take the theme's colours.
- WebUI's lint wants click handlers named `on…Click_`.
- The bad-message test clicks in a `setTimeout`: the browser ends the panel's renderer at once, and a click inside the script made `ExecJs` fail with "RenderFrame deleted". The waiter is `RenderProcessHostBadMojoMessageWaiter` at the pin.
- `ui/protections` depends on `//ghost/browser/blocking` with no cycle (`gn` accepted it as planned).

## Task 11: the look (2026-10-18)

- Screenshots come from a temporary browser test (never committed) that runs the flow and copies the panel's surface to PNG files; browser tests need `EnablePixelOutput()`, or the copy is solid blue. Nothing opens on the user's screen.
- The user approved the four states (the panel with its button, the offer, the question, the report) on 2026-10-18.
- The first report view had wide gaps: `white-space: pre-wrap` on the field list kept the template's own line breaks. Only the values keep their breaks now, and they wrap with `overflow-wrap: anywhere` rather than in the middle of a word.

## Task 12: mutation checks, suites, audit (2026-10-18)

Each mutant built, and its test failed:

| # | Mutation | Failed |
|---|---|---|
| M1 | the report's address keeps the query | `BreakageReportTest.TheAddressHasNoQueryOrFragment` |
| M2 | `RestoreLevel` writes the level it started from instead of the site's own choice | `BreakageCheckTest.RestoringPutsTheExactChoiceBack` |
| M3 | an edited address isn't checked against the page's site | `BreakageReportTest.AnEditedAddressMustStayOnTheSite` |
| M4 | the cap of 50 hosts raised to 1,000 | `PageProtectionsTest.KeepsAtMostFiftyHosts` |
| M5 | the report reads the reloaded page's blocks, not the snapshot | `ProtectionsPanelBrowserTest.SiteNotWorkingLowersAndAsks` |
| M6 | the details' limit raised a thousandfold | `ProtectionsPanelBrowserTest.ALongDetailsTextIsABadMessage` |

M6's first form, the check deleted, didn't compile (the limit's constant became unused, an error with `/WX`), so it counted for nothing until rewritten.

The suites, with the mutants reverted and both targets rebuilt: `ghost_unittests` 193/193, `ghost_browsertests` 147/147 (the launcher's summary shows 146: it counts `PRE_GpcCanBeTurnedOff` inside its test), no retry; tooling 557 tests.
The egress audit on the dev build (`out/vanilla`, the hidden desktop): 0 unexpected hosts; one route probe and one proxy auto-detection lookup, as always. The flow sends nothing without the click on *Open on GitHub*, and the audit never opens the panel.
