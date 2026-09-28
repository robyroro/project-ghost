# Testing and CI

**The standard:** a test exists to fail when the behavior it describes breaks. A test that can't fail, or that only exercises a mock of the code under test, is deleted in review. When we claim a guarantee in [privacy-model.md](privacy-model.md), a test named in this document enforces it.

## Test layers

| Layer | Framework | Runs | Covers |
|---|---|---|---|
| Tooling | Python `unittest`, against real temporary git repositories | Every push (hosted CI) | `tools/`: environment rules, gclient generation, patch export and apply determinism, lint |
| Unit | gtest (`ghost_unittests`) | Every PR (build host) | Pure logic in `components/*/core`: policy resolution, parameter rules, identity rules, score formula, key derivation, list management, manifest parsing |
| Browser | `InProcessBrowserTest` (`ghost_browsertests`) with `EmbeddedTestServer` serving several HTTPS hostnames | Every PR (build host) | Integration with Chromium: interception, partitions, OTR lifecycle, defaults, UI entry points |
| Isolation contract | Browser tests, table-driven | Every PR | Every storage mechanism × every boundary; see [architecture.md](architecture.md#testing-isolation) |
| Fingerprinting | Browser tests evaluating script in frames, workers and media queries | Every PR | Consistency, determinism per site, unlinkability across sites, per-mode tiers |
| Egress audit | Harness driving the packaged browser; NetLog analysis | Nightly and release | No contact with hosts outside the allowlist |
| Upstream suites | Filtered `unit_tests`, `browser_tests`, `content_browsertests` | Nightly | Upstream behavior our patches touch |
| Fuzzing | libFuzzer | Nightly, later continuous | List parsing, parameter stripping, manifest parsing, every mojom handler we add |
| Performance | crossbench (Speedometer 3, JetStream, MotionMark) and a page-load corpus | Nightly | Regression against vanilla Chromium at the same tag; blocking overhead budget |
| Compatibility | Smoke corpus of popular sites | Nightly, never gating | Breakage from blocking and protections |

**Notes on specific layers**
- **Ghost session destruction** is tested twice:
  - behaviorally: state set in a session is absent from the next session;
  - on disk: the user-data directory is diffed before and after, and any new file outside an allowlist fails the test.
- **Real-site compatibility tests are flaky** by nature. They report trends and never block a merge.

## Running the tooling tests

```
python -m unittest discover -s tools/tests -t tools
python tools/lint.py
```

The tooling tests run git with system and global config disabled. The CI runners' `core.autocrlf` and the developer's `diff.*` and `format.*` settings therefore can't change results.

## CI

**Hosted runners (GitHub Actions), from Phase 0**
- `tooling.yml` runs the tooling tests and lint on Windows and Ubuntu for every push and pull request.
- Actions are pinned by commit SHA, and the workflow token is read-only.

**Self-hosted Windows build host, from Phase 1**
- **Persistent checkout and build cache.** A clean Chromium build takes hours, so pull-request builds are incremental.
- **Pull requests:** apply the series, build `chrome`, `ghost_unittests` and `ghost_browsertests`, and run them.
- **Nightly:**
  - an official-configuration build and the installer;
  - the egress audit and the isolation suite;
  - upstream suites, performance and compatibility runs;
  - the **canary rebase** of the series onto the current Beta tag ([ADR 0003](adr/0003-upstream-extended-stable.md)).
- **Fork pull requests** never run automatically on self-hosted runners. A maintainer applies a label after reviewing the change.

**Release pipeline, from Phase 2**
- A separate builder that runs only tagged releases, from a clean checkout.
- Signing goes through a hardware-backed service. Stable releases need two maintainers' approval.
- Each release publishes an SBOM (SPDX) and provenance (SLSA) next to its artifacts.
- Rollout is staged by percentage, with a halt switch.

**Later.** When build minutes dominate cost, evaluate a remote-execution backend compatible with Chromium's build tool (Siso speaks the Remote Execution API).
