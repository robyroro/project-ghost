# Blocking Engine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Shade blocks third-party ads and trackers: every request a page makes passes through adblock-rust 0.13.3 with EasyList and EasyPrivacy, built with Chromium's toolchain, within 50 µs p99 per check.

**Architecture:** Crates Chromium lacks are vendored in `//ghost/third_party/rust/` by `tools/rust_vendor.py` (from a `Cargo.lock` pinned to Chromium's versions of shared crates) with `cargo_crate` `BUILD.gn` files; patch 0030 opens three Chromium crates to `//ghost` and adds three features. `//ghost/components/blocking` wraps the engine (a `cxx` bridge, an engine on its own sequence, the lists as data files); `//ghost/browser/blocking` intercepts requests with a proxying `URLLoaderFactory` (patch 0028); patch 0029 ships the lists in the installer. Spec: [2026-10-10-blocking-engine-design.md](../specs/2026-10-10-blocking-engine-design.md).

**Tech Stack:** Rust (Chromium's in-tree toolchain, rustc 1.98 nightly), `cxx`, C++20 Chromium code (`//base`, `//services/network`, `//content`), GN `cargo_crate` / `rust_static_library`, Python 3.11 tools, gtest browser tests, libFuzzer.

---

## Conventions

As in the earlier plans: `WEBOPS`, `SRC` = `$WEBOPS\chromium\src`; builds with depot_tools first on `PATH`, `autoninja -C out\vanilla …`; tests `python -m unittest discover -s tools/tests -t tools`; lint after `git add`; commits small, no AI trailers; patch commits in `SRC` with `git commit -s -F <file>` (never a PowerShell pipe: it adds a BOM) and `Why:`/`Upstream:` trailers; then `python tools/patches.py export --src chromium/src` and `check`. Never edit `SRC` while a build runs. Copy uncommitted `//ghost` files into `SRC/ghost` with Git Bash `cp` to build them before committing; after committing, `git -C chromium/src/ghost pull --ff-only`.

**Spike results already known (2026-10-10, while planning):**
- `gnrt` hard-codes `third_party/rust/chromium_crates_io`: we write `tools/rust_vendor.py`.
- `cargo` 1.98 is in `third_party/rust-toolchain/bin/`.
- Resolved with `default-features = false, features = ["full-regex-handling"]`, adblock-rust needs 55 crates. Chromium has all but these, which we vendor: `adblock`, `arrayvec`, `flatbuffers` (build script; build-dependencies `rustc_version`, `semver`), `form_urlencoded`, `idna`, `idna_adapter`, `percent-encoding`, `precomputed-hash`, `rustc-hash`, `seahash`, `thiserror` 1 (build script), `thiserror-impl` 1 (proc macro), `url`, and the two build-dependencies.
- Chromium's versions satisfy adblock-rust's requirements (`regex` 1.12.4, `serde` 1.0.228, `serde_json` 1.0.150, `bitflags` 2.13.1, `memchr` 2.8.2, `itertools` 0.14.0, `base64` 0.22.1, `smallvec` 1.15.2, ICU4X 2.2).
- Patch 0030 needs: visibility for `regex/v1`, `memchr/v2`, `utf8_iter/v1`; `rc` in `serde/v1` and `serde_core/v1`; `serde` in `bitflags/v2` with a dependency on `serde_core/v1`.

## File map

| File | Responsibility |
|---|---|
| `third_party/rust/Cargo.toml`, `Cargo.lock` | The crate set we vendor, pinned |
| `third_party/rust/vendor_config.toml` | Per-crate settings the generator can't infer (build-script outputs, rustflags) |
| `third_party/rust/<crate>/v<epoch>/` | Each vendored crate: `crate/` (sources), `BUILD.gn`, `README.chromium`, license |
| `tools/rust_vendor.py` | Resolves the graph with `cargo metadata`, vendors what Chromium lacks, writes `BUILD.gn` and `README.chromium`, checks features of Chromium's crates |
| `components/blocking/rust/{lib.rs,BUILD.gn}` | The `cxx` bridge |
| `components/blocking/{blocking_engine,filter_lists,request_types}.{h,cc}` | The engine on its sequence; reading the lists; request types |
| `components/blocking/data/` | `easylist.txt`, `easyprivacy.txt`, `VERSIONS.json`, `README.chromium` |
| `components/blocking/*_unittest.cc`, `blocking_perftest.cc`, `fuzz/blocking_list_fuzzer.cc` | Tests |
| `browser/blocking/{request_filter,blocking_service}.{h,cc}`, `*_browsertest.cc` | Interception |
| `tools/filter_lists.py` | `update` and `check` for the lists |
| `tools/installer_smoke.py` | Checks the lists are installed |
| Patches 0028, 0029, 0030 | The hook, the installer, the crates |

---

### Task 1: The spike's build question: one vendored crate builds

**Files:** Create `third_party/rust/Cargo.toml`, `tools/rust_vendor.py` (first version), `tools/tests/test_rust_vendor.py`.

- [ ] **Step 1: The manifest.** `third_party/rust/Cargo.toml`: a `[package]` `ghost_rust_deps` (never built: it only resolves the set) with `adblock = { version = "=0.13.3", default-features = false, features = ["full-regex-handling"] }`, and `[patch.crates-io]` empty. Generate `Cargo.lock` with the toolchain's `cargo generate-lockfile`, then pin every crate Chromium has to Chromium's version: `cargo update -p <name> --precise <chromium version>` for each (the table above); commit both.
- [ ] **Step 2: Tests first** for the tool's pure parts: reading `cargo metadata` JSON (a saved fixture of a three-crate graph) into crates with versions, features, dependencies, build scripts and proc macros; deciding "Chromium has it" from a fake `//third_party/rust` tree (directory `<name>/v<epoch>` with `cargo_pkg_version`); the epoch rule (`1.2.3` → `v1`, `0.22.1` → `v0_22`); rendering `BUILD.gn` for a library, a proc macro and a crate with a build script (golden strings); refusing when a Chromium crate lacks a feature the graph needs (names the crate and the features).
- [ ] **Step 3:** Implement `tools/rust_vendor.py` to pass them: `resolve()`, `chromium_crate()`, `epoch()`, `render_build_gn()`, `check_features()`, `vendor()` (download from `https://static.crates.io/crates/<n>/<n>-<v>.crate`, check the lock's sha256, unpack into `<crate>/v<epoch>/crate/`), `write_readme()`.
- [ ] **Step 4:** Vendor one leaf crate, `seahash`, build `//ghost/third_party/rust/seahash/v4:lib` in `out/vanilla`. Expected: compiles. Record in the progress notes.
- [ ] **Step 5:** Commit.

### Task 2: Patch 0030 and the whole crate set

- [ ] **Step 1:** In `SRC`: the visibility lines of `regex/v1`, `memchr/v2`, `utf8_iter/v1` gain `"//ghost/third_party/rust/*"`; `serde/v1` and `serde_core/v1` gain feature `"rc"`; `bitflags/v2` gains feature `"serde"` and dep `"//third_party/rust/serde_core/v1:lib"`. Commit with `Why:` (spec) and `Upstream: not upstreamable: a downstream crate set`; export; `check`.
- [ ] **Step 2:** `python tools/rust_vendor.py` vendors the whole set; it must report no missing feature.
- [ ] **Step 3:** Build `//ghost/third_party/rust/adblock/v0_13:lib`. Fix what the build finds (build-script outputs and rustflags go into `vendor_config.toml`, and into tests of the tool). Record each in the progress notes.
- [ ] **Step 4:** Licenses: every vendored crate's license is MIT, Apache-2.0, Unicode-3.0, Unlicense or MPL-2.0 (the survey's); `licensing.md` lists the set. Commit (the vendored sources are large: one commit, "third_party/rust: adblock-rust 0.13.3 and its dependencies").

### Task 3: The `cxx` bridge

**Files:** `components/blocking/rust/{lib.rs,BUILD.gn}`, `components/blocking/registrable_domain.{h,cc}`, `components/blocking/engine_bridge_unittest.cc`.

- [ ] **Step 1: Tests first** (`ghost_unittests`), the spec's engine cases: a list `||tracker.test^$third-party` blocks `https://tracker.test/t.js` from `https://a.test/` as a script, and not from `https://tracker.test/`; `$domain=a.test` limits a rule; a malformed line doesn't stop the rest; an empty engine allows everything.
- [ ] **Step 2:** `lib.rs`: `#[cxx::bridge(namespace = "ghost::blocking")]` with `type Engine; fn new_engine(lists: &[String]) -> Box<Engine>; fn check(self: &Engine, url: &str, source_url: &str, request_type: &str) -> Verdict;` and `struct Verdict { blocked: bool, filter: String }`; `extern "C++" { include!("ghost/components/blocking/registrable_domain.h"); fn registrable_domain(host: &str) -> String; }` used through adblock-rust's `ResolvesDomain`. `BUILD.gn`: `rust_static_library` with `cxx_bindings = [ "lib.rs" ]`, deps on the adblock crate.
- [ ] **Step 3:** `registrable_domain.cc`: `net::registry_controlled_domains::GetDomainAndRegistry(host, INCLUDE_PRIVATE_REGISTRIES)`, the host itself when empty.
- [ ] **Step 4:** Build, run, commit.

### Task 4: `BlockingEngine`, `FilterLists`, request types

- [ ] **Step 1: Tests first:** states loading → ready / failed; a `Check` posted while loading answered after `Load` completes; a failed load (no list read) answers allow; every `network::mojom::RequestDestination` maps (table test).
- [ ] **Step 2:** Implement: `BlockingEngine` owns `rust::Box<Engine>` on `base::ThreadPool::CreateSequencedTaskRunner({base::TaskPriority::USER_BLOCKING})`, `Load(std::vector<std::string>)`, `Check(const CheckRequest&, base::OnceCallback<void(Verdict)>)` replying on the caller's sequence; `FilterLists::Read(base::FilePath dir, callback)` on a `MayBlock` task; `ToAdblockType(RequestDestination)`.
- [ ] **Step 3:** Commit.

### Task 5: The lists

- [ ] **Step 1:** `tools/filter_lists.py` with tests: `update` fetches `https://easylist.to/easylist/easylist.txt` and `easyprivacy.txt`, writes them and `VERSIONS.json` (url, fetched date, the list's own `! Version:` line, sha256); `check` fails on a file whose sha256 differs. Run `update`; commit the lists, `VERSIONS.json` and `README.chromium` (EasyList's dual license GPL-3.0 / CC BY-SA 3.0, `Shipped: yes`).
- [ ] **Step 2:** The tooling tests run `filter_lists.py check`.

### Task 6: Interception and the service; patch 0028

- [ ] **Step 1: Browser tests first** (`browser/blocking/request_filter_browsertest.cc`), as the spec lists: blocked third-party script (`ERR_BLOCKED_BY_CLIENT`), allowed same-site script, blocked at a redirect, allowed top-level navigation, Incognito, the real EasyList blocking a known ad host mapped to the test server. A test list is set with `BlockingService::SetListsForTesting`.
- [ ] **Step 2:** `RequestFilter`: a `network::mojom::URLLoaderFactory` proxy; per request: skip top-level navigations and same-site requests (`net::SchemeHostPort` → registrable domain of the request vs the initiator / top frame); otherwise `Check` and hold; block with `client->OnComplete(network::URLLoaderCompletionStatus(net::ERR_BLOCKED_BY_CLIENT))`; a client wrapper re-checks on `OnReceiveRedirect`. `BlockingService`: a `base::NoDestructor` singleton started from `ghost::BrowserMainExtraParts::PostBrowserStart`, lists from `<exe dir>/<version>/blocking/`.
- [ ] **Step 3:** Patch 0028 (`ChromeContentBrowserClient::WillCreateURLLoaderFactory` calls `ghost::MaybeProxyURLLoaderFactory`), export, `check`.
- [ ] **Step 4:** Build, run, commit.

### Task 7: Patch 0029 and the installer check

- [ ] **Step 1:** `installer_smoke` test first: an installed browser lacking `<version>\blocking\easylist.txt` fails.
- [ ] **Step 2:** Patch 0029: `chrome.release` lists `blocking\*.txt` beside the version directory's other data; the GN rule copies `//ghost/components/blocking/data/*.txt` to `$root_out_dir/blocking/`.
- [ ] **Step 3:** Build `mini_installer`, run `installer_smoke`: PASSED; commit.

### Task 8: Performance and fuzzing

- [ ] **Step 1:** Record the URL corpus once: the egress audit's NetLog tooling on ~20 news front pages, URLs only (`tools/egress_audit.py` parsing, no addresses), into `components/blocking/test/request_corpus.txt`.
- [ ] **Step 2:** `blocking_perftest.cc`: compile time, p50/p99 per check over the corpus, the engine's memory; fails over 50 µs p99 when `is_official_build`.
- [ ] **Step 3:** `fuzz/blocking_list_fuzzer.cc` (libFuzzer over `new_engine`), run 30 minutes in a fuzzing build; no crash.
- [ ] **Step 4:** The official-build question of the spike: build `blocking_perftest` in `out/release` (ThinLTO, CFI) once and run it; record.

### Task 9: Mutation checks, egress audit, docs, push

- [ ] M1–M4 of the spec, each made, seen failing, undone.
- [ ] Egress audit on `out/vanilla`: no unexpected host.
- [ ] Docs: architecture, privacy model, testing, licensing, roadmap (Phase 3's sub-projects; 3A done), progress notes `2026-10-10-blocking-engine-spike.md`, spec status.
- [ ] Push (approved for this sub-project: "write the plan and do everything"), wait for both tooling jobs.
