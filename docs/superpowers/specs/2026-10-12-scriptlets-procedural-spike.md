# Scriptlets and procedural rules: progress notes

Phase 4B ([design](2026-10-12-scriptlets-procedural-design.md), [plan](../plans/2026-10-12-scriptlets-procedural.md)).

## Task 1: CSS validation (2026-10-12)

- **The feature:** `css-validation` on adblock-rust 0.13.3 (`third_party/rust/Cargo.toml`). `tools/rust_vendor.py` vendored 19 more crates (35 in all; 40 are Chromium's):

  | Crate | Version | License |
  |---|---|---|
  | cssparser, cssparser-macros, dtoa-short, selectors, servo_arc | 0.35.0, 0.6.1, 0.3.5, 0.32.0, 0.4.3 | MPL-2.0 |
  | derive_more, derive_more-impl | 2.1.1 | MIT |
  | new_debug_unreachable | 1.0.6 | MIT |
  | phf, phf_codegen, phf_generator, phf_macros, phf_shared | 0.11.3 | MIT |
  | dtoa, rand, rand_core, rustc_version, semver, siphasher | 1.0.11, 0.8.7, 0.6.4, 0.4.1, 1.0.28, 1.0.3 | MIT OR Apache-2.0 |

  `rand`, `rand_core`, `phf_generator`, `phf_codegen`, `rustc_version` and `semver` serve proc macros and build scripts only: they don't ship in the browser.
- **Patch 0043** (series = 43): Chromium's `stable_deref_trait` gets its `default` and `std` features (servo_arc needs them) and, with `itoa` (cssparser needs it), visibility from `//ghost/third_party/rust`, as 0028 does for other crates.
- **`vendor_config.toml`:**
  - `selectors` ships no license file: `licenses/selectors/LICENSE` is the MPL-2.0 text cssparser ships;
  - its `build.rs` writes `ascii_case_insensitive_html_attributes.rs` (`build_script_outputs`);
  - `derive_more`, `derive_more-impl` and `siphasher` include Markdown files with `include_str!`, which siso refuses unless they are GN inputs. A new setting, `inputs`, lists them.
- **`tools/rust_vendor.py`** learned two things, each with a test:
  - `inputs`;
  - renamed dependencies: selectors names `new_debug_unreachable` as `debug_unreachable`, so the generator writes `aliased_deps`.
- **Existing tests:** the 4A engine and cosmetic unit tests pass unchanged.
- **Dev build, the engine with CSS validation** (`out\vanilla`, reported, not judged; Rust runs with debug assertions):
  - compile 171 ms, about 7 MiB;
  - a request's check: p50 152 µs, p99 373 µs;
  - a document's rules: p50 145 µs, p99 272 µs;
  - a batch of 1,000 names: p50 43 µs, p99 102 µs.
- **What parses** (spike point 4; the shipped lists, 2026-10-10):
  - 24,691 cosmetic lines; the validation refuses 7:
    - EasyList's `:-abp-properties` rules, which adblock-rust 0.13 refuses (the 6 lines found);
    - `abema.tv##^script:has-text(NREUM)`, which is HTML filtering and adblock-rust doesn't have.
  - Of the 297 lines with a procedural operator or an action, the engine turns 220 distinct rules into JSON for 264 hosts: 224 `css-selector` steps, 219 `has-text`, 1 `remove`. 4 action rules are plain CSS (style rules).
  - **63 rules put `:has-text` inside `:has()`** (`deliveroo.*#?#a[role="button"]:has(p:has-text(Sponsored))`). adblock-rust passes `:has()` through as CSS with its inside untouched, so these arrived in `hide_selectors`, where Blink drops them.
    - Decision while executing: the bridge moves a hide selector that holds a procedural operator into the procedural rules, as one `css-selector`.
    - `procedural.js` parses such a selector itself (Task 7).
  - The host with the most procedural rules: bleepingcomputer.com (6). The largest scriptlet: accuweather.com (6,155 bytes with the dependencies).

## Task 2: resources and bridge (2026-10-12)

- **adblock-rust injects only `application/javascript` resources** (`supports_scriptlet_injection`); `fn/javascript` serves as a dependency only. The scriptlets are `application/javascript`, their helpers (`shade-safe`, `shade-chain`, `shade-trap`) `fn/javascript`. A scriptlet can therefore also be named as a redirect; it then answers with its function's source, which defines a function and runs nothing.

## Task 3: browser checks (2026-10-12)

- The overload `StyleSheetFor(std::vector<StyledSelector>)` made 4A's braced calls ambiguous: it is `StyleSheetForActions`.

## Task 4: the push and the main-world scriptlet (2026-10-12)

- **Spike point 1, the order.** `WebLocalFrame::ExecuteScript` at `RunScriptsAtDocumentStart` runs the scriptlet before the page's first inline script, and a page whose CSP allows only a nonce'd script still gets it: `ScriptletsBrowserTest.TheFirstScriptAlreadySeesTheScriptlet` and `APageCspDoesntStopIt` pass, and 4A's 12 tests pass on the shared harness.
- **Spike point 2, frames.** A frame its parent writes into with `document.open()` and `document.write()` gets the parent's scriptlet before the written script runs (`AFrameItsParentWritesTakesTheParentsScriptlets`). No limitation to document.

## Task 6: the other scriptlets (2026-10-12)

- **Errors are sanitized.** `WebLocalFrame::ExecuteScript` creates the script with `SanitizeScriptErrors::kSanitize`, so the window's error event for an exception a scriptlet throws reads "Script error.", without its message. A marker in the message can't keep it out of the console. `shadeSafe().abort()` sets a flag just before the throw, which the scriptlets' own error listener checks and clears; a microtask clears it too, for a page that catches the exception and so gets no event. The test log shows no console error from the scriptlets.
- **Natives need their `this`.** `safe.setTimeout(...)` called the native as a method of the helpers object: "Illegal invocation". It calls `setTimeout` with `window` now.
- **`set-cookie`'s reload is deferred.** Blink allows no navigation while `RunScriptsAtDocumentStart` runs, so `location.reload()` there does nothing; the scriptlet reloads from a zero-delay timer. The reload often cuts the first load short before its own scripts run (the test checks the navigation's type, `reload`, and that no second reload follows).
- 17 scriptlet browser tests and 12 of 4A's pass; `EveryScriptletTheListsUseResolvesExceptJsonEdit` passes.

## Task 7: the procedural executor (2026-10-12)

- `procedural.js` runs every operator and action; its `css-selector` step parses a selector with a procedural operator inside `:has()` (the 63 rules Task 1 found) into steps of its own: CSS, the operator, and `has` for `:has()` around one, which keeps the elements with a descendant (or, after a leading combinator, a relative) the inside selects. `ProceduralBrowserTest`: 6 tests pass, one of them on `.feed:has(p:has-text(Sponsored))`.
- **Finding: adblock-rust's CSS validation rewrites escapes in an operator's argument.** It serializes each argument through cssparser (`to_css_nested`), which reads a backslash as a CSS escape: `/promo\d+/` arrives as `/promo\d +/` (`\d` is a hexadecimal escape, written back with its terminating space) and `/c\s+d/` as `/cs+d/` (`\s` stands for `s`); `\.` and `\(` survive. None of the shipped lists' 29 regular-expression arguments holds a backslash-letter or backslash-digit escape, so no shipped rule is affected; a list delivered later (3E) could be. Recorded as a limitation, to report to brave/adblock-rust; the vendored crate isn't patched. The browser test uses `/promo[0-9]+/`.

## Task 8: redirects (2026-10-12)

- **Spike point 3, answers without the network.** The request filter answers a redirect itself with a `URLResponseHead` (status 200, the resource's MIME type, `Access-Control-Allow-Origin: *`) and its response type set as the network would have: `basic` for the page's own origin, `opaque` for a `no-cors` request elsewhere, `cors` otherwise. The page runs the redirected script (`onload` fires), shows the 1x1 image (`naturalWidth` 1), reads `{}` from a `fetch()` in `cors` mode, and Google Tag Manager's stand-in calls the `eventCallback` the page pushes; none of the four requests reaches the server (`RedirectBrowserTest`). The first approach worked: nothing to change.
- `fetch()` is checked as `xmlhttprequest`, so EasyList's `$xhr` redirects apply to it.
- At the pin, `URLLoaderCompletionStatus::decoded_body_length` is a `base::ByteSize`.

## Task 9: budgets, dev build (2026-10-12)

- **The sites to measure.** The most scriptlets: oregonlive.com, with 4 rules (`acs`, `aost` twice, `rmnt`; techcrunch.com, pennlive.com, mlive.com and al.com have 4 too). The most procedural rules: openalternative.co, with 16 (Task 1's count, which saw only rules that reach the JSON, missed the ones nested in `:has()`).
- **`ghost_blocking_perftests`** (`out\vanilla`, Rust with debug assertions; reported, not judged):
  - compile 186 ms, about 8 MiB;
  - a request's check: p50 152 µs, p99 365 µs;
  - a document's rules for the corpus' 20 pages, now with scripts of up to 8,050 bytes: p50 288 µs, p99 464 µs (145 and 272 µs before the scriptlets);
  - the 22 scriptlet sites (scripts up to 9,898 bytes): p50 285 µs, p99 454 µs;
  - a batch of 1,000 names: p50 43 µs, p99 111 µs.
- **`scanner_cost.py`** (`out\vanilla`, five loads per profile, on the hidden desktop):
  - oregonlive.com, `light.html`: +8.9 ms of script, +23.5 ms of task time against Off;
  - openalternative.co, `heavy.html`: +128.7 ms of script;
  - cosmetic.test, `heavy.html`: +149.8 ms of script.

  The procedural rules' cost is lost in the dev build's noise, which is larger than it: only an official build can judge it.
- **The official build:** `out\release` had not been built since the security drill (2026-10-09), and 3A–4B's patches touch widely included headers: 28,837 actions, about 7–9 hours. Started, then stopped cleanly at the user's request after 605 actions (13,732 remaining, `.siso_fs_state` written), to run overnight; the budgets are measured from it before the push.

## Task 10: mutation checks, suites, audit (2026-10-12)

- **Mutation checks**, each failing as required (`mutate4b.py`: each mutant built before its tests ran):

  | Mutant | Fails |
  |---|---|
  | M1 the scriptlet runs at `DOMContentLoaded` | `ScriptletsBrowserTest.TheFirstScriptAlreadySeesTheScriptlet` |
  | M2 a template or non-function scriptlet accepted | `EngineBridgeTest.OnlyFunctionStyleScriptletsAreAccepted` |
  | M3 Off pages get the full rules | `ScriptletsBrowserTest.OffRunsNone`, `ProceduralBrowserTest.OffAppliesNone` |
  | M4 `IsSafeStyle` accepts any declarations | `StyleSheetTest.SafeStylesStayInTheirRule`, `ProceduralTest.DropsWhatCouldEscape` |
  | M5 a redirect becomes a plain block | `RedirectBrowserTest.ResourcesAnswerWithoutTheNetwork` |
  | M6 the executor in the page's world | `ProceduralBrowserTest.ThePageSeesNoExecutor` |

  M1's first form posted the scriptlet as a task from document start: the test still passed, because the task runs before the first chunk of HTML is parsed. That is no regression to guard against, so M1 moves the scriptlet to `DOMContentLoaded`, after the page's scripts.
- After the mutants, `src/ghost` matches webops file for file.
- **The suites** (`out\vanilla`, rebuilt clean): `ghost_unittests` 137 tests pass; `ghost_browsertests` 127 tests pass, none retried.
- **The egress audit** (`out\vanilla`, on the hidden desktop): 0 unexpected hosts; not counted, as before, one route probe and the `wpad` lookups.
- **Fuzzing:** `ghost_cosmetic_procedural_fuzzer` (`out\fuzz`), 30 minutes from three seeds (a procedural rule, a style, a marker): 7,755,913 runs, 4,306 a second, coverage 449, no crash, peak 869 MB.

## Task 9: budgets, official build (2026-10-12)

- **The official build** resumed at 14:24 and finished at 14:43: siso ran 505 of the 13,729 actions it listed; the rest were up to date (19m47s).
- **`ghost_blocking_perftests`** (`out\release`, three runs), all within budget:
  - compile 105–117 ms, about 7 MiB;
  - a request's check: p50 6 µs, p99 39–42 µs (budget 50 µs; 41–49 µs in 4A);
  - a document's rules for the corpus' 20 pages, with scripts of up to 8,050 bytes: p50 167–172 µs, p99 294–328 µs (budget 1 ms);
  - the 22 scriptlet sites, scripts up to 9,898 bytes: p50 169–172 µs, p99 281–360 µs (budget 1 ms);
  - a batch of 1,000 names: p50 29 µs, p99 86–93 µs (budget 1 ms).
- **`scanner_cost.py`** (`out\release`, on the hidden desktop; script time against Off):

  | Site, page | Loads per profile | Cost |
  |---|---|---|
  | oregonlive.com, `light.html` | 5; 9; 9 | 2.0; 1.9; 1.9 ms |
  | cosmetic.test, `light.html` | 9; 9 | 0.9; 0.9 ms |
  | openalternative.co, `heavy.html` | 5 | 14.6 ms |
  | cosmetic.test, `heavy.html` | 5 | 14.3 ms |

  - **Procedural rules:** openalternative.co less cosmetic.test, about 0.3 ms: within the 5 ms budget.
  - **Scriptlets:** the spec's measure, what cosmetic filtering adds to a small page on the site with the most scriptlets, is 1.9 ms against a 1 ms budget: **missed**. Less cosmetic.test's cost on the same page (the style sheet and the scanner), the scriptlets alone take about 1.0 ms: about 10 KB of script compiled for each document (four scriptlets and their helpers), and `remove-node-text`'s observer while the page is parsed.
  - **4A's scanner,** measured in an official build for the first time: 14.3 ms on `heavy.html`, against 4A's 5 ms budget: **missed**. 4A's estimate (5–6 ms) divided the dev build's 111.5 ms by a 19 times speed-up measured on other page script; the scanner gains less.
