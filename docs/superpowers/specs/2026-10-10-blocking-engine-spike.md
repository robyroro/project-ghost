# The blocking engine: progress notes

- Phase 3, sub-project 3A ([design](2026-10-10-blocking-engine-design.md), [plan](../plans/2026-10-10-blocking-engine.md), [ADR 0006](../../adr/0006-blocking-engine-adblock-rust.md))
- Machine: the reference machine (Ryzen 5 3600, 6 cores, 32 GB)
- Chromium 152.0.7977.158, the series with patches 0028–0031 (31 patches); `out/vanilla` (development, component), `out/release` (official: ThinLTO, PGO), `out/fuzz` (libFuzzer, ASan)

## The spike's questions

| Question | Answer |
|---|---|
| Can `gnrt` generate `BUILD.gn` files for crates outside Chromium's crate set? | No: its paths are fixed to `third_party/rust/chromium_crates_io`. `tools/rust_vendor.py` does it: `pin` moves every crate Chromium also has to Chromium's version, `vendor` copies the rest and writes their `BUILD.gn` and `README.chromium`. |
| The dependency set | adblock-rust 0.13.3 with `default-features = false` and `full-regex-handling`: 55 crates. 39 are Chromium's, at Chromium's versions; 16 are vendored ([licensing](../../licensing.md#dependency-policy)). |
| Chromium's crates we can't use | `regex`, `regex-automata` and `aho-corasick` are test-only in Chromium, so they are vendored; `regex-syntax`, `memchr` and `utf8_iter` are Chromium's, made visible to `//ghost`. |
| Is one patch enough for Chromium's crates? | Yes, patch 0028: `regex-syntax`, `memchr` and `utf8_iter` visible to `//ghost/third_party/rust/*`; `serde` and `serde_core` with `rc`, `bitflags` with `serde`. The design expected `regex`, `memchr`, `utf8_iter`, `serde` and `bitflags`: `regex` became ours, and `regex-syntax` and `serde_core` were found by building. |
| Does adblock-rust build with Chromium's toolchain? | Yes, after two changes in `rust_vendor.py`: an empty `features` list is left out (Chromium's build-script runner fails on it), and `flatbuffers`' build script is off (`vendor_config.toml`): it detects Chromium's rustc as a nightly and turns on `trusted_len`, which the toolchain refuses. |
| A `cxx` call from a test | Works. The bridge calls back into C++ for registrable domains, so blocking and the browser agree on what a site is. |
| Does the official build accept the Rust objects? | Yes: `ghost_blocking_perftests` builds and runs in `out/release`, with ThinLTO. |

**Patch numbers.** The design numbered the patches by the order they were planned; they were written in another order. As committed: **0028** Chromium's crates, **0029** the `ContentBrowserClient` hook, **0030** the installer, and **0031** `about:credits` (below).

## Performance

The test (`ghost_blocking_perftests`) builds the engine from the shipped lists and checks 2,194 requests recorded from 20 news front pages, three times each. The design asked for about 5,000: the recorder kept one tab per site for 15 seconds, sites that timed out gave what had arrived, and identical URLs (with query values replaced by `x`) count once. 240 of the 2,194 are blocked.

| Build | Compile the lists | Memory | p50 | p99 |
|---|---|---|---|---|
| `out/vanilla` | 124–132 ms | about 4–5 MiB | 150 µs | 350–380 µs |
| `out/vanilla`, Rust without debug assertions (experiment) | 120 ms | about 5 MiB | 9 µs | 60 µs |
| `out/release` (official) | 77–79 ms | about 6 MiB | 6 µs | 36–39 µs |

ADR 0006's budget is 50 µs at p99: **within budget in the official build**, with modest headroom on this machine. The test fails over budget only there.

A development build sets `dcheck_always_on`, which compiles every Rust crate with `-Cdebug-assertions` (`//build/config:feature_flags`), and adblock-rust's matching is about 15 times slower with it. A crate-level `-Cdebug-assertions=off` doesn't help: the global flag comes later on the command line. Ruled out on the way: lazy regex compilation (the last pass is as slow as the first), the domain resolver (0.4 µs a call), URL parsing (3 µs). Development builds keep the assertions, which are worth having there; their numbers are reported, not judged.

`ghost_blocking_perftests` is its own binary because `ghost_unittests` depends on `//chrome/browser`: in `out/release` it would have compiled Chrome's test support and linked a test binary the size of Chrome. Alone it built in 2 minutes. The builder runs it with the other suites.

## Fuzzing

Two libFuzzer targets in `out/fuzz` (`build/args/fuzz.gn`: ASan, DCHECKs, and Rust's debug assertions with overflow checks):

- `ghost_blocking_list_fuzzer` builds an engine from arbitrary list text, with a dictionary of filter syntax, and checks five requests against it. Seeds: the shipped lists cut into pieces under 4 KB.
- `ghost_blocking_request_fuzzer` checks arbitrary requests against the shipped lists. The input is a line of the performance corpus, so its 2,194 requests are the seeds. Requests are what a page controls.

Chromium's fuzzing build instruments C++ and not Rust, so a fuzzer over adblock-rust would have mutated blind. `//ghost/build/config:rust_fuzz_coverage` adds cargo-fuzz's instrumentation to `//ghost`'s Rust in a fuzzing build only; `rust_vendor.py` writes it into every vendored crate. Chromium's own crates (`regex-syntax`, `memchr`, `serde`) stay uninstrumented. `optimize_for_fuzzing` isn't supported on Windows.

| Fuzzer, 30 minutes | Runs | Per second | Edges covered | Corpus | Peak memory | Crashes, leaks, timeouts |
|---|---|---|---|---|---|---|
| list | 227,974 | 126 | 8,696 → 12,033 | 2,454 inputs | 550 MB | none |
| request | 1,331,102 | 739 | 9,910 → 11,343 | 2,362 inputs | 717 MB | none |

The fuzzing build took 30 minutes (only the fuzzers and their dependencies). How to run them is in [testing.md](../../testing.md).

## Found and fixed

| Problem | Fix |
|---|---|
| **Bytes that aren't UTF-8 aborted the browser.** The bridge takes Rust strings, and `cxx` calls `std::abort()` on invalid UTF-8. A damaged list file crashed the browser at startup; a compromised renderer could crash the browser process with a request method that isn't UTF-8, since the request filter sees it before the network service validates it. Found by reading `cxx` while writing the fuzzers, which only pass UTF-8. | `BlockingEngine` leaves such a list out with an error and blocks such a request (the network service would refuse it anyway); an invalid page URL is passed as no page instead of through `GURL::spec()`, which dumps. Four unit tests. |
| A same-site request's redirect to a tracker escaped checking. | Same-site requests start at once but stay in the filter, so their redirects are checked. |
| `127.0.0.1` had the registrable domain `0.1`. | IP addresses are their own site (`url::HostIsIPAddress`). |
| `about:credits` in an official build named none of the vendored crates nor the lists: Chromium's `licenses.py` looks only under its own `third_party/`. Two crates (`flatbuffers`, `seahash`) ship no license file at all. | Patch 0031 and `//ghost/build/credits.gni`, from the variable Chromium leaves for downstream projects; `rust_vendor.py` generates the crate list and stops on a crate without a license text unless `vendor_config.toml` names one. Checked in `out/release`: all 16 crates and the lists, with their license texts. A proc macro is marked `Shipped: no`. The SBOM names the lists' directory too. |
| The same-site browser test couldn't fail (mutation check M2, below). | Fixed in the test. |

## Mutation checks

Each made in `src/ghost` or the Chromium checkout, seen failing, then undone:

| Check | Result |
|---|---|
| M1: a redirect judged by the original URL instead of the new one | `ARedirectToATrackerIsBlocked` fails: the image loads and `tracker.test` is reached. |
| M2: no same-site exemption | **First, nothing failed.** The test's list was `\|\|tracker.test^$third-party`, which the engine itself never applies to `a.test`. The test now lists a rule for any site's `own.js` and checks that it blocks `b.test`'s; with M2, `a.test`'s and `cdn.a.test`'s are blocked and the test fails. |
| M3: the lists left out of `chrome.release` | `installer_smoke` fails in Windows Sandbox: both lists missing from `152.0.7977.158\blocking`. Undone, it passes. |
| M4: a check while loading answered "allow" | `ACheckBeforeLoadingIsAnsweredAfterIt` fails. |
| Credits: `licenses.py` without `//ghost/build/credits.gni`'s directories | None of the crates or lists in the generated page; with them, all. |

## End to end, 2026-10-10

| Check | Result |
|---|---|
| Tooling tests | pass (1 skipped: the TPM test) |
| `ghost_unittests` | 52 of 52 |
| `ghost_browsertests` | 22 of 22; between two and six pass on retry after `EXCESSIVE_OUTPUT` (ANGLE's verbose log lines), the known category, never a failure |
| `ghost_blocking_perftests` | passes in `out/vanilla` (reports) and `out/release` (within budget) |
| `installer_smoke` | passes; the lists are installed in `<version>\blocking\` |
| Egress audit, `out/vanilla` | 0 unexpected hosts (the route probe and `wpad`, not counted) |

## Not covered yet

- WebSocket and WebTransport requests: their own `ContentBrowserClient` hooks, with 3D.
- The lists change only with browser releases until 3E delivers them as components.
- The fuzzers run by hand; the build host doesn't run them yet.
