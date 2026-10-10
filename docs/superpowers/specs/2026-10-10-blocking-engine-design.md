# The blocking engine: design

- Status: design approved 2026-10-10
- Phase 3, sub-project 3A ([roadmap](../../roadmap.md#phases-to-public-alpha)); the engine is [ADR 0006](../../adr/0006-blocking-engine-adblock-rust.md)
- Phase 3's sub-projects, in order (decided 2026-10-10): **3A** the blocking engine; **3B** tracking-parameter stripping; **3C** network defaults (third-party cookies, HTTPS-First, DoH, WebRTC, GPC, preconnect and prefetch off); **3D** per-site policy and the protections panel; **3E** list updates as signed components, which needs the update server (sub-project C) deployed

## Goal

Shade blocks third-party ads and trackers out of the box: every network request a page makes passes through adblock-rust with EasyList and EasyPrivacy, built into the browser with Chromium's own toolchain, at a cost per request within ADR 0006's budget.

## Decisions

Settled in discussion on 2026-10-10:

- **adblock-rust 0.13.3** (MPL-2.0), built by Chromium's in-tree Rust toolchain and reached from C++ through `cxx`, as ADR 0006 decided.
- **Its dependencies live in `//ghost/third_party/rust/`, with generated `BUILD.gn` files** (approach A of three). Each crate Chromium lacks is vendored at an exact version, with its license and `README.chromium`; each crate Chromium already has (`regex`, `serde`, `serde_json`, `memchr`, `base64`, `bitflags`, `itertools`, and ICU4X for `idna`) is used from Chromium, never duplicated. Rejected: adding the crates to Chromium's own crate set through a patch (tens of thousands of foreign lines in the patch series, and a `Cargo.lock` conflict at every milestone) and linking a library built separately with `cargo` (outside the hermetic build: another compiler, other flags, at odds with the official build's LTO and CFI).
- **Chromium decides what a site is.** adblock-rust's optional public-suffix crates (`addr`, `psl`) stay out; the engine asks C++ for registrable domains, answered by `registry_controlled_domains`, so blocking and the browser agree on "same site".
- **What is checked:** every request a page makes (subresources, subframes, workers, service workers) in Normal and Incognito profiles, except **top-level navigations**, which always proceed.
- **The level is Standard, fixed until 3D**, as [privacy-model.md](../../privacy-model.md#blocking) defines it: third-party ads and trackers. Requests to the page's own site (same eTLD+1) aren't checked; Strict and per-site Off come with 3D.
- **A blocked request fails with `net::ERR_BLOCKED_BY_CLIENT`**, as with blocking extensions: sites and DevTools know it, and the console says why.
- **The lists are EasyList and EasyPrivacy, unmodified, committed** in `components/blocking/data/` with their source, date and sha256, and refreshed by a tool. Builds are reproducible and each refresh is a reviewable diff. They ship in the installer as **separate data files** beside the browser, never compiled into a binary ([licensing.md](../../licensing.md#filter-lists-and-scriptlets)), and are credited on the credits page.
- **Until the engine is ready, requests that need a verdict wait.** Otherwise the first pages after startup, often restored ones, would escape blocking. **If the engine can't load** (a list missing or unreadable), requests proceed and the error is logged: a browser that loads nothing is worse than one that doesn't block. A visible status comes with 3D's panel.
- **WebSocket and WebTransport** use other `ContentBrowserClient` hooks; they come with 3D, and the documentation says so meanwhile.
- **Our own list** (first-party and CNAME-cloaked trackers) comes later; 3A provides the mechanism.

## Components

### `//ghost/third_party/rust/`

`<crate>/v<major>/` per crate: the crate's sources at the pinned version, its license, `README.chromium` (`Name`, `URL`, `Version`, `License`, `License File`, `Security Critical: yes`, `Shipped: yes`), and `BUILD.gn` using Chromium's `cargo_crate` template, depending on Chromium's own crates where they exist (`//third_party/rust/regex/v1:lib` and so on). `Cargo.toml` and `Cargo.lock` at `//ghost/third_party/rust/` pin the whole set.

**`tools/rust_vendor.py`** (unless the spike finds `gnrt` can do it for a directory outside Chromium's crate set): downloads each crate in our `Cargo.lock` from crates.io, checks its sha256 against the lock, unpacks it, and writes `README.chromium` and `BUILD.gn`. Re-running it on an unchanged lock changes nothing. It never runs during a build.

Crates to vendor, by the 2026-10-10 survey: `adblock`, `url`, `idna`, `idna_adapter` (its ICU4X backend, on Chromium's ICU4X), `form_urlencoded`, `percent-encoding`, `flatbuffers`, `arrayvec`, `rustc-hash`, `seahash`, `precomputed-hash`, `thiserror` v1 and `thiserror-impl` v1 (Chromium has v2). The spike confirms the list and the versions.

### `//ghost/components/blocking/`

| Unit | What it does |
|---|---|
| `rust/lib.rs` | The `cxx` bridge: `Engine::from_lists(lists)` builds the engine; `check(url, source_url, request_type) -> Verdict` (blocked, and the matching rule for the console). The registrable-domain resolver is a C++ function the bridge calls. |
| `blocking_engine.{h,cc}` | Owns the engine on its own `SequencedTaskRunner`. `Load(lists)` is asynchronous; `Check(request, callback)` answers on the caller's sequence. State: loading, ready, failed. Checks posted while loading are answered once it settles. |
| `filter_lists.{h,cc}` | Reads the list files from the installed browser's directory, on a blocking-I/O task. |
| `request_types.{h,cc}` | Maps `network::mojom::RequestDestination` to adblock-rust's request types (script, image, stylesheet, subdocument, xmlhttprequest, font, media, ping, other). |
| `data/` | `easylist.txt`, `easyprivacy.txt`, `VERSIONS.json` (source URL, date, sha256 of each), `README.chromium` for the credits page. |
| `tools/filter_lists.py` | `update` fetches both lists, writes them and `VERSIONS.json`; `check` verifies the files against `VERSIONS.json` (run by the tooling tests). |

### `//ghost/browser/blocking/`

| Unit | What it does |
|---|---|
| `request_filter.{h,cc}` | A proxying `URLLoaderFactory`. Decides what needs a verdict (not top-level navigations, not same-site requests), holds the request until the verdict, then fails it with `ERR_BLOCKED_BY_CLIENT` or passes it on. It wraps the loader client so that **every redirect is checked again**. |
| `blocking_service.{h,cc}` | One per browser, not per profile: one compiled engine serves every tab ([architecture.md](../../architecture.md#processes)). Starts loading the lists at browser start. |

### Patches

- **0028**, `chrome/browser/chrome_content_browser_client.cc`: `WillCreateURLLoaderFactory` calls `ghost::MaybeProxyURLLoaderFactory`. `Why:` Chromium has no embedder hook for request filtering besides the extensions system.
- **0029**, `chrome/installer/mini_installer/chrome.release` (and the build rule that copies the data): the list files go into the installer beside the browser.

## For the spike

Before the rest:

1. Whether `gnrt` (`tools/crates/gnrt`) can generate `BUILD.gn` files for a crate directory outside `third_party/rust/chromium_crates_io`; otherwise `tools/rust_vendor.py`.
2. The full dependency set at adblock-rust 0.13.3 with `default-features = false` plus the features we need, and whether Chromium's versions of shared crates satisfy its requirements.
3. That adblock-rust compiles and links in `out/vanilla` with the in-tree toolchain, and that a `cxx` call from a `ghost_unittests` test works.
4. Whether the official build's flags (ThinLTO, CFI) accept the Rust objects: one official build of the engine's test target, before the release that first ships it.

## Error handling

- A list that can't be read or parsed: the engine loads what it can; if nothing loads, its state is failed and every check answers "allow". Logged once.
- A Rust panic would abort the browser process. The bridge catches none: the fuzzer (below) is how panics are found before shipping.
- A request whose verdict can't be determined (a URL the engine can't parse) is allowed.
- `rust_vendor.py` and `filter_lists.py` stop, writing nothing, on any checksum mismatch.

## Testing

**Unit tests** (`ghost_unittests`):
- the engine through the bridge, with small lists written in the test: a third-party tracker's script is blocked; the same site passes; `$third-party` and `$domain=` work; malformed lines are skipped; an engine without lists allows everything;
- `BlockingEngine`: its states; a check posted before loading is answered after it; a failed load allows;
- the request-type mapping, for every destination.

**Browser tests** (`ghost_browsertests`), on an embedded test server with host-resolver rules, no real network:
- a page on `a.test` loading a script from `tracker.test` (a test list blocks it): blocked, `ERR_BLOCKED_BY_CLIENT`;
- a script from `a.test`: allowed;
- an image on `a.test` redirecting to `tracker.test`: blocked at the redirect;
- navigating to `tracker.test` directly: allowed;
- the same in an Incognito window;
- with the real, packaged EasyList: a request to a well-known ad host, mapped to the test server, is blocked.

**The installer:** `installer_smoke` checks that the list files are installed beside the browser.

**Performance** (ADR 0006's budget: under 50 µs p99 per check): a test runs the engine, with the real EasyList and EasyPrivacy, over about 5,000 request URLs recorded once from about 20 news sites (URLs only: no addresses, cookies or headers). It reports the lists' compile time, p50 and p99 per check, and the engine's memory, and fails over budget in an official build.

**Fuzzing:** a libFuzzer target over list parsing, `blocking_list_fuzzer`, run locally for a fixed time before 3A is done.

**Mutation checks**, each must fail:
- M1: no check on redirect → the redirect test fails;
- M2: no same-site exemption → the same-site test fails;
- M3: the lists left out of the installer → `installer_smoke` fails;
- M4: requests pass while the engine loads → the before-ready test fails.

**The egress audit** stays at no unexpected host.

## Documentation

[architecture.md](../../architecture.md) (the units, the patches, the engine's sequence), [privacy-model.md](../../privacy-model.md#blocking) (what Standard blocks now; WebSocket and WebTransport not yet), [testing.md](../../testing.md) (the tests above, the performance test, the fuzzer), [licensing.md](../../licensing.md) (the vendored crates; EasyList and EasyPrivacy shipped as data with attribution), [roadmap.md](../../roadmap.md) (Phase 3's sub-projects; 3A done).

## Done when

- [ ] The tests above pass and the four mutation checks fail as required.
- [ ] The performance test is within budget in an official build.
- [ ] The fuzzer ran for its fixed time without a crash.
- [ ] The egress audit finds no unexpected host.
- [ ] The documentation is updated; everything is pushed with tooling CI green.

## Out of scope

- Cosmetic filtering, scriptlets and CNAME uncloaking (Phase 4).
- Levels other than Standard, per-site settings, the panel, WebSocket and WebTransport (3D).
- Tracking-parameter stripping, including `$removeparam` (3B).
- List updates without a new release, and a cached compiled engine (3E).
- Our own filter list.
