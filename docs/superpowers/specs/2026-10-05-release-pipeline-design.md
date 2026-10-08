# Release pipeline: design

- Status: done 2026-10-08 ([progress notes](2026-10-05-release-pipeline-spike.md)); design approved 2026-10-05
- Phase 2, sub-project E ([roadmap](../../roadmap.md#phase-2-release-engineering))
- Depends on sub-projects A ([release version](2026-10-02-release-version-design.md)), B ([branded updater](2026-10-02-branded-updater-design.md)), C ([update server](2026-10-03-update-server-design.md)) and D ([signing](2026-10-04-signing-design.md))

## Goal

One command turns a release tag into a release: an official build, tested on the bits that ship, signed, described by an SBOM and a provenance document, kept as a draft GitHub release, and offered by the update server to a fraction of update checks that can be raised, halted or promoted. It runs on the reference machine, which holds the signing keys, and it resumes where it stopped, because an official build takes most of a day there.

## Decisions

- **Release builds are official builds** (`is_official_build = true`): static, ThinLTO and the PGO profiles Chromium publishes, as Chrome ships. The first one is the spike, which measures time, peak memory and disk before anything else depends on them.
- **The pipeline is a local tool, `tools/release.py`, not a CI workflow.** The signing keys are in this PC's TPM and the publisher key asks for a PIN, and the CI builder holds no secrets ([build-host.md](../../build/build-host.md#security)). A workflow on a self-hosted runner would need the keys on the CI machine and a person at its desktop.
- **The release build uses the existing checkout,** in a new output directory, `out/release`. A second checkout doesn't fit: 95 GB were free on 2026-10-05. So the tool checks the checkout's state instead of trusting it.
- **Staged rollout is a probability per update check.** Update requests carry no identifier ([privacy model](../../privacy-model.md#the-update-request)), so the server can't place a user in a stable bucket. It offers the candidate release to each check with probability *p*, and the active release otherwise. Clients check about five times a day, so *p* = 1 % reaches about 5 % of clients a day. The halt switch is *p* = 0. Nothing new goes to the client, and nothing goes back to the client: the server never offers a version older than the installed one.
- **Releases are GitHub drafts** under the test identity. [licensing.md](../../licensing.md#release-gates) allows no public build before the final name is cleared: installs of a test identity would keep its app IDs, registry keys and directories, and wouldn't migrate. `release.py` refuses `--public` while the build pins the test identity.
- **The provenance is SLSA Build Level 1,** and the documents say so. The build runs on a maintainer's machine, not on a hosted build platform that would sign the provenance itself (Level 2). The artifacts' integrity comes from their Authenticode signatures and the CRX3 publisher proof; the provenance records how they were made.
- **No two-maintainer approval yet.** The project has one maintainer. The threat model's rule applies from the second.

## The configuration

`build/args/release.gn`:

| Argument | Value | Why |
|---|---|---|
| `is_official_build` | `true` | Static, ThinLTO, PGO, official defaults |
| `branding_file_path` | `//ghost/branding/BRANDING` | As in `dev.gn` |
| `enable_updater`, `enable_update_notifications` | `true` | As in `dev.gn` |
| `ghost_signing_identity` | `"test"` | D's keys |
| `ghost_update_url` | the test server's URL | C |
| `symbol_level` | 1 | Full symbols (2, the default for official Windows builds) may not fit the free disk; 1 keeps function names and line tables, and Ghost uploads no crash reports |

Official builds don't load the field-trial testing configuration, so `disable_fieldtrial_testing_config` isn't needed. `out/release` builds every shipped target: `chrome`, `mini_installer`, and the updater with its metainstaller. An official build is static, so the updater needs no output directory of its own.

**PGO profiles.** Official builds need them, and the checkout has them off (`checkout_pgo_profiles: False` in `.gclient`). `bootstrap.py` turns them on, and a sync downloads the `win64` profile Chromium publishes for the pinned revision.

## Components

### In `//ghost`

| Component | Purpose |
|---|---|
| `build/args/release.gn` | The release configuration (above) |
| `tools/release.py` | The pipeline: `run --tag T` runs the stages below; `verify DIR`; `rollout`, `halt`, `promote`, `drop` |
| `tools/release_state.py` | A release's `state.json`: each stage's inputs, outputs with their SHA-256 and times; decides what a re-run skips |
| `tools/sbom.py` | The SPDX document: Chromium's `tools/licenses/licenses.py --format spdx` over the shipped targets' dependencies, plus Ghost's own code (MPL-2.0) and patch series |
| `tools/provenance.py` | The in-toto v1 statement with an SLSA Provenance v1 predicate; `SHA256SUMS` |
| `tools/bootstrap.py` | `.gclient` gets `checkout_pgo_profiles: True` |
| `tools/sign_release.py` | Unchanged; called once with `--crx --offline-installer` |
| `tools/update_smoke.py` | Runs the server repository's service in the Sandbox for the rollout steps |

`release.py` reuses `builder.py`'s sync and apply decisions and their stamps, so the series is re-applied only when it or the pin changed: re-applying rewrites every patched file, and the build tool rebuilds by modification time.

### In `project-ghost-update-server`

| Component | Change |
|---|---|
| `releases.json` (`manifest.py`) | Beside each app's active release, an optional **candidate**: the same fields plus `fraction`, from 0 to 1 |
| `protocol.py` | Offers the candidate with probability `fraction`, the active release otherwise, from a random source the tests inject. The decision isn't logged. A client already at or above the offered version gets `noupdate`, as now. |
| `ghost-update-admin` | `stage --fraction F` (a staged package becomes the candidate), `set-fraction F`, `halt` (`fraction` 0), `promote` (the candidate becomes the active release), `drop` |
| `ghost_update/release.py` | With `--fraction`, uploads as the candidate; without it, activates as before (C's first deployment uses that) |

**One candidate at a time.** If `-2` is halted for a bug, it is dropped and `-3` staged; clients left on `-2` take `-3`, which is newer.

## A release

`python tools\release.py run --tag 152.0.7977.149-1` runs these stages in order. Each records its inputs and outputs in `%USERPROFILE%\ProjectGhostReleases\<tag>\state.json`.

1. **Check.** Every condition, reported together:
   - the tag exists, is on `origin/main`, and the tooling workflow passed on its commit;
   - `WEBOPS` is clean and at the tag's commit, and `src/ghost` at the same commit;
   - `CHROMIUM_VERSION` matches the tag;
   - `src` is at the pin, its tree is clean, and `.ghost-applied` holds the digest of the tag's series (no forgotten mutation can ship);
   - `out/release/args.gn`, if it exists, equals the one `release.gn` generates.
2. **Sync and apply,** when the pin or the series changed (`builder.py`'s decisions).
3. **Build.** `release_version.py write`, `gn gen out\release`, `autoninja -j 10` for the shipped targets and `ghost_unittests`; `chrome\VERSION` restored in a `finally`. Every command's exit code is checked.
4. **Test,** on the bits that ship:
   - `ghost_unittests` from `out/release`;
   - `ghost_browsertests`: from `out/release` if the spike finds its LTO link affordable, otherwise from `out/vanilla` at the same commit. The provenance names the configuration each suite ran in.
   - the installer smoke test on the unsigned `mini_installer.exe`;
   - the egress audit on the official build. Official defaults differ from development ones, so an unexpected host stops the release.
5. **Sign.** `sign_release.py --crx --offline-installer`: the PE files once, one PIN, the CRX3 and the tagged offline installer.
6. **Describe.** The SBOM, the provenance and `SHA256SUMS`.
7. **Draft.** `gh release create <tag> --draft --prerelease` with the offline installer, `update.crx3`, the SBOM, the provenance and `SHA256SUMS` (the largest about 480 MB, under GitHub's 2 GB limit). The notes say it is the test identity, not for daily use, and that installs won't migrate to the final name.
8. **Stage,** when `--stage F` is given: the CRX3 goes to the update server as the candidate with fraction F.

A re-run skips the stages whose inputs haven't changed; `--redo <stage>` runs one again.

**The provenance** holds:
- subjects: each artifact and its SHA-256;
- external parameters: the tag and the `WEBOPS` commit;
- resolved dependencies: the Chromium commit, the patch series digest, the depot_tools commit, the clang revision, and the Visual Studio and Windows SDK versions;
- internal parameters: the full `args.gn`, and where each test suite ran;
- run details: the builder (the reference machine), start and finish times.

**`release.py verify DIR`** recomputes every hash against `SHA256SUMS` and the provenance, and checks the Authenticode signatures and the CRX3 proof. Anyone with the files can run it.

## Error handling

| Case | Behavior |
|---|---|
| A check fails | Stops before building, listing every failed condition; repairs nothing itself |
| A command fails or is interrupted | Stops; the stage stays not done. A build resumes incrementally. |
| Wrong PIN or backup password | Only the signing stage fails; it is re-run |
| A draft exists for the tag | Compared by hashes: identical, the stage is done; different, refused |
| `--public` with the test identity | Refused |
| A test fails | Nothing is signed, drafted or staged |
| `stage` while a candidate exists | Refused: `drop` or `promote` it first |

## Testing

- **Unit tests, in CI on Ubuntu and Windows:**
  - the checks, against real temporary git repositories, like the existing tooling tests;
  - the stage state: skipping, `--redo`, changed inputs;
  - the SBOM's and the provenance's shape, and `verify` on a small fixture;
  - the refusal of `--public` with the test identity.
- **In the server's repository:** the candidate with an injected random source (*p* = 0, 1 and between), the admin commands, and the rule that no older version is offered.
- **End to end in Windows Sandbox,** the server repository's service running in the Sandbox in place of the test server:
  1. the official, signed, tagged offline installer for `-1` installs;
  2. `-2` is staged with *p* = 0: the client gets `noupdate` and stays on `-1`;
  3. *p* = 1: the client moves to `-2`;
  4. launch, privacy and uninstall, as in B.
- **Mutation checks,** each of which must fail:
  - a patched file changed in `src`: `release.py` refuses the release;
  - the server ignoring the fraction: the halted step fails;
  - a wrong hash in the provenance: `verify` fails;
  - `--public` with the test identity: refused.

## For the spike

Questions the implementation answers first, recorded in the progress notes:

- How long does the first official build of `152.0.7977.149` take on the reference machine, with what peak memory and disk use? Does it fit the free disk, at which `symbol_level`?
- How long does a respin take, now that each one re-links with LTO?
- Can `ghost_browsertests` be linked in `out/release` at an affordable cost?
- Do the 24 patches build under the official configuration?
- Does the server repository's service run in the Sandbox, with the Python and packages there?

If the disk or the memory isn't enough, the spike stops and asks: nothing is deleted to make room.

## Done when

1. `release.py` has produced the official releases `-1` and `-2` of the pinned Chromium, each with its draft GitHub release.
2. The egress audit on the official build finds no unexpected host.
3. The end-to-end test passes, and the mutation checks fail as required.
4. The update server's repository has the candidate and its commands, with its tests passing in CI. Its deployment stays deferred, as decided for C.
5. The documentation is updated:
   - `docs/build/release.md`: making a release, `verify`, rolling out, halting;
   - [threat-model.md](../../threat-model.md#supply-chain): release builds as they are made, SLSA Build Level 1, one maintainer;
   - [architecture.md](../../architecture.md#updates-and-signed-data): the candidate and the fraction;
   - the roadmap marks E done.

Phase 2's exit criterion "an update shipped end to end to test machines" stays open until C's server is deployed.

## Out of scope

- **Public releases:** after the final name ([licensing.md](../../licensing.md#trademarks)).
- **SLSA Build Level 2 and reproducible builds:** they need a hosted build platform, and a measurement of how close two builds get.
- **Automatic rollout schedules:** with no telemetry there's nothing to judge a step by; the maintainer raises the fraction.
- **Rollback instructions** (a signed instruction to install an older version): with F's security release runbook if it needs one.
- **Components, system-level installs, other platforms.**
