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
