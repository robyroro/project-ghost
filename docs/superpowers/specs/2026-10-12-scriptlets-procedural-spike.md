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
