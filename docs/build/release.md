# Making a release

A release turns a tag into files people can install and update to: an official build, tested on the bits that ship, signed, with an SBOM and a provenance document, kept as a GitHub release, and offered by the update server. `tools/release.py` does all of it. Design: [release pipeline](../superpowers/specs/2026-10-05-release-pipeline-design.md).

## The release machine

Releases are built and signed on the machine that holds the signing keys ([signing](../signing/README.md)), not on the CI builder, which holds no secrets ([build-host.md](build-host.md#security)). Until the project has more, that is the reference machine (Ryzen 5 3600, 6 cores, 32 GB), building in `out/release` of its development checkout. `release.py` checks the checkout's state before every release instead of trusting it.

What one release costs there:

| Stage | Time |
|---|---|
| The first official build of a Chromium version | 13 h 16 min (2026-10-05 and 06, from an empty `out/release`) |
| A respin (the build stage of a later release) | 23 min (`152.0.7977.149-4`), most of it `chrome.dll`'s ThinLTO link |
| Tests: unit and browser suites, installer smoke test, egress audit | 14–15 min |
| Signing (`sign_release.py`, one PIN) | 3–7 min |
| SBOM, provenance, draft | about 1 min |

A whole respin, tag to draft, took 41 minutes. `out/release` takes about 32 GB.

A respin rebuilds only what carries the release version, because nothing a host tool is built from reads it: `//base` takes the Chromium release (patch 0026), and so does the build timestamp (`build/compute_build_timestamp.py`). A change there costs the next release hours instead of minutes; the [progress notes](../superpowers/specs/2026-10-05-release-pipeline-spike.md) say how to check one with `gn` before building.

## Making a release

1. Everything for the release is on `main`, pushed, and the tooling workflow passed on it.
2. Tag it, `<CHROMIUM_VERSION>[-<respin>]` ([ADR 0007](../adr/0007-version-numbers.md)), and push the tag:

   ```
   git tag -a -m "Release 152.0.7977.149-1" 152.0.7977.149-1
   git push origin 152.0.7977.149-1
   ```
3. Bring `src/ghost` to the tag: `git -C <src>\ghost pull --ff-only`.
4. Run, with depot_tools first on `PATH`:

   ```
   python tools\release.py run --tag 152.0.7977.149-1 --src <src>
   ```

   It asks for the publisher key's PIN at the signing stage. The egress audit opens a browser window for about 15 minutes; don't use it.

`run` stops at the first failure. Fix the cause and run the same command again: the stages that finished, with the same inputs, are skipped. `--redo <stage>` runs one stage again, and the ones after it that depend on it.

**The checks** run first, every time, and report every problem at once: the tag exists and is on `origin`'s `main`; the tooling workflow passed on its commit; this repository and `src/ghost` are clean and at that commit; the Chromium checkout has no local changes; `out/release/args.gn` is the release's configuration. `release.py` never repairs any of these itself.

**Long runs.** A release build runs for hours. Start it where it outlives the terminal that started it. A program started from a shell that is itself a child of another tool (an editor's terminal, an agent's shell) can end with that shell.

## What each stage does

| Stage | What it does |
|---|---|
| `sync` | Syncs Chromium when the pin moved (`bootstrap.py --pgo`); fetches Chrome's `win64` PGO profile and V8's builtins profiles, which official builds need |
| `apply` | Re-applies the patch series when the branch isn't exactly `patches/` |
| `build` | Writes the release version into `chrome\VERSION` and restores it afterwards; builds the browser, its installer, the updater and the test suites with `build/args/release.gn` |
| `test` | `ghost_unittests` and `ghost_browsertests`, the installer smoke test in Windows Sandbox, the egress audit on the official build |
| `sign` | `sign_release.py --crx --offline-installer`: every PE file, the CRX3 with the publisher proof, the tagged offline installer |
| `describe` | The SBOM, the provenance, `SHA256SUMS` |
| `draft` | A draft pre-release on GitHub with the five files |
| `stage` | With `--stage F --host USER@HOST`: uploads the CRX3 as the update server's candidate, offered to fraction F of update checks |

Each release keeps its state and files in `%USERPROFILE%\ProjectGhostReleases\<tag>\`: `state.json`, `signed\`, `results\` (the test summaries and the NetLog, which holds this machine's addresses and is never published), and `publish\`.

**Public releases.** `--public` makes a published release instead of a draft. `release.py` refuses it for the development and test identities: no public build comes before the final name ([licensing.md](../licensing.md#release-gates)).

## Provenance

`provenance.intoto.json` is an [in-toto](https://in-toto.io) Statement v1 with an [SLSA Provenance v1](https://slsa.dev/spec/v1.0/provenance) predicate. It names each published file with its SHA-256, and records how they were made: the tag and its commit, the Chromium commit, the patch series' digest, the depot_tools commit, the clang, Visual Studio and Windows SDK versions, the full `args.gn`, the identity, and where each test suite ran.

It meets **SLSA Build Level 1**: the provenance exists and is complete. It is not signed, because the build runs on a maintainer's machine, not on a hosted build platform that would sign it (Level 2). The files' integrity comes from their own signatures: Authenticode on every PE file and installer, and the publisher proof on the CRX3.

`sbom.spdx.json` is SPDX 2.2, made by Chromium's `tools/licenses/licenses.py` for the browser's installer and the updater, merged, with Ghost's own code (MPL-2.0) as the package that contains them.

## Verifying a release

```
python tools\release.py verify <directory with the release's files>
```

It checks every file against `SHA256SUMS`, the provenance against them, the CRX3's proof against the identity's pinned publisher keys, and, on Windows, the offline installer's Authenticode signature.

## Rolling out

A release goes to the update server as its **candidate**, beside the active release. The server offers the candidate to a fraction of update checks. Update requests carry no identifier ([privacy model](../privacy-model.md#the-update-request)), so each check draws on its own: at about five checks a day, a fraction of 0.01 reaches about 5 % of clients a day. Nothing about the choice is recorded.

```
python tools\release.py run --tag <tag> --src <src> --stage 0.01 --host ghost@<server>
python tools\release.py rollout --fraction 0.1 --host ghost@<server>
python tools\release.py promote --host ghost@<server>
```

There's no schedule: Ghost has no telemetry to judge a step by, so the maintainer raises the fraction from what users report. `promote` makes the candidate the active release, for every check.

## Halting

```
python tools\release.py halt --host ghost@<server>
```

The fraction becomes 0: no new client gets the release. Clients that took it keep it, because the server never offers a version older than a client's own. The fix is the next respin: `drop` the halted candidate, then release and stage the fix, which those clients take because it is newer.
