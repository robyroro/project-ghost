# Release Pipeline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** One command, `tools/release.py run --tag T`, turns a release tag into an official, tested, signed release with an SBOM and an SLSA provenance document, kept as a draft GitHub release, and offered by the update server to a fraction of update checks that can be raised, halted or promoted.

**Architecture:** Flat, standard-library Python tools in `tools/`, as the rest: `release.py` (the stages, resumable through `release_state.py`), `sbom.py` (Chromium's SPDX generator, merged and completed), `provenance.py` (in-toto statement, `SHA256SUMS`, hash verification). The official configuration is `build/args/release.gn`, built in `out/release` of the existing checkout. The update server gains a candidate release beside the active one, offered with probability *p*, and admin commands to move it. Spec: [2026-10-05-release-pipeline-design.md](../specs/2026-10-05-release-pipeline-design.md).

**Tech Stack:** Python 3.11 (`unittest`), GN/siso with ThinLTO and PGO, Chromium's `tools/licenses/licenses.py` (SPDX 2.2 JSON), in-toto Statement v1 with SLSA Provenance v1, the GitHub CLI (`gh`), Windows Sandbox; on the server, Python with `cryptography`.

---

## Conventions

- `WEBOPS` = `C:\Users\robyv\Desktop\DLU\webops` (`/c/Users/robyv/Desktop/DLU/webops` in Git Bash), `SRC` = `$WEBOPS\chromium\src`, `ROOT` = `$WEBOPS\chromium` (the checkout root: `.gclient` and the stamps), `SRV` = `C:\Users\robyv\Desktop\DLU\project-ghost-update-server`, `DEPOT` = `C:\src\depot_tools`.
- `RELEASES` = `%USERPROFILE%\ProjectGhostReleases`, where `release.py` keeps each release's directory. Outside both repositories; never commit from it.
- `SCRATCH` = the session's scratchpad directory: scripts, logs and mutation inputs. Never commit from it.
- `APPID` = `{c0ff4371-d9ab-461e-bffd-6b0dc2430b02}` (`browser_appid` in `branding/updater.gni`).
- Every source file starts with the MPL-2.0 notice. Commits: small, no AI trailers, the user is the sole author. In `SRV`, `git commit -s`.
- Tests in `WEBOPS`: `python -m unittest discover -s tools/tests -t tools -v`. Lint: `python tools/lint.py`, after `git add` (it checks tracked files only). In `SRV`: `python -m unittest discover -s tests -t . -v` and `python tools/lint.py`.
- **Syncing `src/ghost`.** After committing in `WEBOPS`: `git -C $SRC/ghost pull -q --ff-only`. To build uncommitted changes, copy files with Git Bash `cp` (not `Copy-Item`, which keeps the old modification time).
- **Builds** run in PowerShell with depot_tools first on `PATH`: `$env:PATH = "C:\src\depot_tools;$env:PATH"; cd $SRC`. Never rename or delete an output directory. Never re-apply the patch series by hand (it rewrites every patched file).
- **Restore `chrome\VERSION`** after every respin, and check each command's exit code in scripts: `release_version.py write` refuses an already written file.
- **Sandbox runs:** never edit `tools/` during one (the Sandbox maps it live); leave at least 3 minutes between two runs.
- **Long builds:** the official build takes most of a day. Start it in the background, keep working on tasks that don't touch `SRC`, and don't run a second build in `SRC` at the same time.
- **The user is present** for Tasks 18 and 19 (the publisher key's PIN; pushing release tags) and is asked before any tag is pushed or any GitHub release is created.
- **Disk:** 95 GB were free on 2026-10-05. If a step would need more, stop and ask; delete nothing to make room.

## File map

**`WEBOPS`:**

| File | Responsibility |
|---|---|
| `build/args/release.gn` | The official configuration |
| `tools/patches.py` | Gains `render_series` and `series_matches`: is the checked-out branch exactly the series in `patches/`? |
| `tools/release_state.py` | A release's `state.json`: what each stage did, and whether it needs to run again |
| `tools/provenance.py` | `SHA256SUMS`; the in-toto statement with the SLSA predicate; hash verification |
| `tools/sbom.py` | The SPDX document: Chromium's generator for the browser and the updater, merged, with Ghost's own package |
| `tools/release.py` | The pipeline: `run`, `verify`, `rollout`, `halt`, `promote`, `drop` |
| `tools/update_smoke.py` | `--rollout-server`: the server repository's service in the Sandbox, the halted and rolled-out steps |
| `tools/tests/test_*.py` | One module per tool |
| `docs/build/release.md` | Making a release, verifying it, rolling it out, halting it |
| `docs/…` | Threat model, architecture, testing, roadmap, progress notes, spec status |

**`SRV`:**

| File | Responsibility |
|---|---|
| `ghost_update/protocol.py` | `Offer`: the active release, the candidate and its fraction; the choice per check |
| `ghost_update/manifest.py` | `candidate` in `releases.json` |
| `ghost_update/service.py` | Passes offers to the protocol |
| `ghost_update/admin.py` | `stage`, `set-fraction`, `halt`, `promote`, `drop`; `activate` refuses while a candidate exists |
| `ghost_update/release.py` | `--fraction`: upload as the candidate |
| `README.md` | The rollout commands |

---

### Task 1: The spike, part 1: the release configuration, PGO profiles, and the first official build

**Files:**
- Create: `build/args/release.gn`
- Create: `docs/superpowers/specs/2026-10-05-release-pipeline-spike.md`

The build started here runs for most of a day. Tasks 2 to 16 don't touch `SRC` and go on while it runs.

- [ ] **Step 1: Write `build/args/release.gn`.**

```gn
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Release build: what ships (Phase 2, sub-project E). tools/release.py writes
# out/release/args.gn: this import, then the release's signing identity and,
# when given, its update URL. Never rename out/release: a renamed output
# directory discards the build state (docs/build/windows.md).

# Static, ThinLTO and the PGO profiles Chromium publishes, as Chrome ships.
# Official builds don't load the field-trial testing configuration.
is_official_build = true
is_debug = false

# Official Windows builds default to full symbols (symbol_level = 2), which the
# reference machine's disk can't hold beside the other output directories.
# Level 1 keeps function names and line tables; Ghost uploads no crash reports.
symbol_level = 1

# Product name, company and copyright in version resources and About.
branding_file_path = "//ghost/branding/BRANDING"

# Ghost's updater (sub-project B) and the browser's "relaunch to update" prompt.
enable_updater = true
enable_update_notifications = true
```

- [ ] **Step 2: Commit and sync `src/ghost`.**

```bash
cd /c/Users/robyv/Desktop/DLU/webops && git add build/args/release.gn && python tools/lint.py && git commit -q -m "build: the release configuration" && git -C chromium/src/ghost pull -q --ff-only && git -C chromium/src/ghost log --oneline -1
```

Expected: `lint: 0 problem(s)`, then the new commit in `src/ghost`.

- [ ] **Step 3: Download the PGO profile.** Official builds read it from `chrome/build/pgo_profiles/`, and the checkout has `checkout_pgo_profiles: False`. Run only the `win64` hook from `DEPS`, not a sync:

```powershell
$env:PATH = "C:\src\depot_tools;$env:PATH"; cd C:\Users\robyv\Desktop\DLU\webops\chromium\src
vpython3 tools\update_pgo_profiles.py --target=win64 update --gs-url-base=chromium-optimization-profiles/pgo_profiles
Get-Content chrome\build\win64.pgo.txt; Get-ChildItem chrome\build\pgo_profiles | Select-Object Name, Length
```

Expected: the `.profdata` file named in `win64.pgo.txt`, about 1 GB. If the download fails, stop and report it: an official build can't run without it.

- [ ] **Step 4: Write `out\release\args.gn`** exactly as `release.py` will (Task 9's `render_args("test", None)`), so the release later reuses this build:

```bash
mkdir -p /c/Users/robyv/Desktop/DLU/webops/chromium/src/out/release && printf '%s\n' '# Written by tools/release.py; edit build/args/release.gn instead.' 'import("//ghost/build/args/release.gn")' 'ghost_signing_identity = "test"' > /c/Users/robyv/Desktop/DLU/webops/chromium/src/out/release/args.gn && cat /c/Users/robyv/Desktop/DLU/webops/chromium/src/out/release/args.gn
```

- [ ] **Step 5: Record the free disk, generate, and start the build in the background** with a memory sampler. Write `$SCRATCH\spike_build.ps1` (the session scratchpad):

```powershell
$ErrorActionPreference = "Stop"
$env:PATH = "C:\src\depot_tools;$env:PATH"
Set-Location C:\Users\robyv\Desktop\DLU\webops\chromium\src
$log = "$PSScriptRoot\spike"
New-Item -ItemType Directory -Force $log | Out-Null
"free before: $([math]::Round((Get-PSDrive C).Free / 1GB, 1)) GB" | Tee-Object -Append "$log\summary.txt"
gn gen out\release
if ($LASTEXITCODE) { throw "gn gen failed" }
$sampler = Start-Job -ArgumentList "$log\memory.csv" {
  param($csv)
  "time,committed_gb,available_gb" | Out-File $csv -Encoding utf8
  while ($true) {
    $os = Get-CimInstance Win32_OperatingSystem
    $committed = (Get-Counter '\Memory\Committed Bytes').CounterSamples[0].CookedValue / 1GB
    "$(Get-Date -Format s),$([math]::Round($committed,1)),$([math]::Round($os.FreePhysicalMemory / 1MB,1))" | Out-File $csv -Append -Encoding utf8
    Start-Sleep 60
  }
}
$targets = "chrome", "mini_installer", "ghost_unittests", "chrome/updater/win/installer:installer", "chrome/updater/win:signing", "chrome/updater/win:updater"
$t = Measure-Command { autoninja -C out\release -j 10 @targets 2>&1 | Out-File "$log\build.log" -Encoding utf8 }
$code = $LASTEXITCODE
Stop-Job $sampler; Remove-Job $sampler
"full build: $([math]::Round($t.TotalHours, 2)) h, exit $code" | Tee-Object -Append "$log\summary.txt"
"out\release: $([math]::Round((Get-ChildItem out\release -Recurse -File | Measure-Object Length -Sum).Sum / 1GB, 1)) GB" | Tee-Object -Append "$log\summary.txt"
"free after: $([math]::Round((Get-PSDrive C).Free / 1GB, 1)) GB" | Tee-Object -Append "$log\summary.txt"
```

Start it detached, so it outlives any tool's time limit, and poll `summary.txt`:

```powershell
Start-Process powershell -ArgumentList "-NoProfile","-ExecutionPolicy","Bypass","-File","$SCRATCH\spike_build.ps1" -WindowStyle Minimized
```

Expected, eventually: `full build: … h, exit 0`. Check `build.log`'s tail every hour or two with `Get-Content $SCRATCH\spike\build.log -Tail 3` (siso prints a heartbeat every 30 s). **If the free disk drops under 15 GB,** stop the build cleanly (Ctrl+C in its window; siso keeps its state) and ask the user. **If a patch fails to compile** under the official configuration (for example a warning that official builds treat as an error), record the error, fix the patch in `SRC` with `git commit --amend` on its commit or a fixup and `patches.py export` (docs/patching.md), and resume the build.

- [ ] **Step 6: Meanwhile, start the progress notes** `docs/superpowers/specs/2026-10-05-release-pipeline-spike.md`:

```markdown
# Release pipeline: progress notes

- Phase 2, sub-project E ([design](2026-10-05-release-pipeline-design.md), [plan](../plans/2026-10-05-release-pipeline.md))
- Machine: the reference machine (Ryzen 5 3600, 6 cores, 32 GB), `autoninja -j 10`
- Chromium 152.0.7977.149, the 24-patch series, `build/args/release.gn` with `ghost_signing_identity = "test"`

## Spike, 2026-10-05

| Question | Answer |
|---|---|
| PGO profile | (Step 3: its name and size) |
| First official build: time | (Step 5) |
| Peak committed memory, lowest available memory | (Step 5's `memory.csv`) |
| `out/release` size; free disk before and after | (Step 5) |
```

Fill the table as results arrive, with the measured values (no estimates). Commit it when Task 1 ends (Step 7).

- [ ] **Step 7: When the build finishes, record and commit.** Read `summary.txt` and `memory.csv` (the peak of `committed_gb`, the lowest `available_gb`), complete the table, then:

```bash
cd /c/Users/robyv/Desktop/DLU/webops && git add docs/superpowers/specs/2026-10-05-release-pipeline-spike.md && python tools/lint.py && git commit -q -m "docs: the release pipeline's spike, the first official build"
```

---

### Task 2: `patches.py` tells whether the checkout is the series

**Files:**
- Modify: `tools/patches.py` (`export`, new `render_series`, `series_matches`)
- Test: `tools/tests/test_patches.py`

The release must build exactly the series its tag holds. Comparing the branch, rendered as `export` would write it, with `patches/` byte for byte says so without trusting a stamp.

- [ ] **Step 1: Write the failing tests.** Append to `tools/tests/test_patches.py`:

```python
class SeriesMatchesTest(SeriesTestCase):
    def test_the_exported_series_matches_its_branch(self):
        self.export()
        self.assertTrue(patches.series_matches(self.src, TAG, self.patches_dir))

    def test_a_commit_not_exported_does_not_match(self):
        self.export()
        self.commit(self.src, {"net/base/socket.cc": "int Connect() { return 2; }\n"},
                    "net: another change\n\n" + TRAILERS)
        self.assertFalse(patches.series_matches(self.src, TAG, self.patches_dir))

    def test_a_changed_patch_file_does_not_match(self):
        self.export()
        first = patches.list_patches(self.patches_dir)[0]
        first.write_bytes(first.read_bytes().replace(b"Hook for Ghost defaults.",
                                                     b"Hook for something else."))
        self.assertFalse(patches.series_matches(self.src, TAG, self.patches_dir))

    def test_render_series_writes_nothing(self):
        before = self.snapshot()
        rendered = patches.render_series(self.src, TAG)
        self.assertEqual(list(rendered), ["0001-prefs-call-into-ghost.patch",
                                          "0002-net-change-connect.patch"])
        self.assertEqual(self.snapshot(), before)
```

- [ ] **Step 2: Run them.**

Run: `cd /c/Users/robyv/Desktop/DLU/webops && python -m unittest discover -s tools/tests -t tools -p test_patches.py -v 2>&1 | tail -5`
Expected: the four new tests ERROR with `AttributeError: module 'patches' has no attribute 'series_matches'` (and `render_series`).

- [ ] **Step 3: Implement.** In `tools/patches.py`, replace `export` with:

```python
def render_series(src: Path, base: str) -> dict[str, bytes]:
    """The series as `export` would write it, file name -> bytes; writes nothing."""
    _require_commit(src, base)
    rng = f"{base}..HEAD"
    if _git(src, "rev-list", "--merges", rng).stdout.strip():
        raise PatchError(f"{rng} contains merge commits; the series must be linear")
    with tempfile.TemporaryDirectory() as tmp:
        _git(src, *_GIT_CONFIG_OVERRIDES, "format-patch", *_FORMAT_PATCH_FLAGS,
             "-o", tmp, rng)
        return {p.name: p.read_bytes() for p in Path(tmp).glob("*.patch")}


def series_matches(src: Path, base: str, patches_dir: Path) -> bool:
    """Whether the branch checked out in src is exactly the series in patches_dir."""
    return render_series(src, base) == {p.name: p.read_bytes()
                                        for p in list_patches(patches_dir)}


def export(src: Path, base: str, patches_dir: Path) -> int:
    new = render_series(src, base)
    old = {p.name: p.read_bytes() for p in list_patches(patches_dir)}
    patches_dir.mkdir(parents=True, exist_ok=True)
    for name in old:
        (patches_dir / name).unlink()
    for name, data in new.items():
        (patches_dir / name).write_bytes(data)
    changed = sum(1 for n in new if n in old and new[n] != old[n])
    print(f"Exported {len(new)} patch(es) to {patches_dir}: "
          f"{len(new.keys() - old.keys())} added, {len(old.keys() - new.keys())} removed, "
          f"{changed} changed.")
    return 0
```

- [ ] **Step 4: Run the patches tests, then the whole suite.**

Run: `cd /c/Users/robyv/Desktop/DLU/webops && python -m unittest discover -s tools/tests -t tools -p test_patches.py -v 2>&1 | tail -3 && python -m unittest discover -s tools/tests -t tools 2>&1 | tail -3`
Expected: `OK` twice.

- [ ] **Step 5: Check it against the real checkout** (read-only):

```bash
cd /c/Users/robyv/Desktop/DLU/webops/tools && python -c "import patches, repo; from pathlib import Path; print(patches.series_matches(Path(r'C:\Users\robyv\Desktop\DLU\webops\chromium\src'), 'refs/tags/' + repo.read_chromium_version(), repo.REPO_ROOT / 'patches'))"
```

Expected: `True`. If `False`, the checkout's branch and `patches/` differ: find out which before going on (`patches.py export` into a scratch copy of `patches/` and `diff -r`).

- [ ] **Step 6: Commit.**

```bash
cd /c/Users/robyv/Desktop/DLU/webops && git add tools/patches.py tools/tests/test_patches.py && python tools/lint.py && git commit -q -m "tools: patches.py tells whether a checkout is exactly the series"
```

---

### Task 3: A release's state

**Files:**
- Create: `tools/release_state.py`
- Test: `tools/tests/test_release_state.py`

- [ ] **Step 1: Write the failing tests** `tools/tests/test_release_state.py`:

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import datetime
import json
import tempfile
import unittest
from pathlib import Path

import release_state

T0 = datetime.datetime(2026, 10, 5, 9, 0, tzinfo=datetime.timezone.utc)
T1 = datetime.datetime(2026, 10, 5, 21, 30, tzinfo=datetime.timezone.utc)


class StateTest(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.path = Path(tmp.name) / "152.0.7977.149-1" / "state.json"

    def test_a_new_release_has_done_nothing(self):
        state = release_state.State.load(self.path, "152.0.7977.149-1")
        self.assertFalse(state.is_done("build", {"commit": "a" * 40}))
        self.assertFalse(self.path.exists())

    def test_a_stage_recorded_with_the_same_inputs_is_done_after_a_reload(self):
        state = release_state.State.load(self.path, "152.0.7977.149-1")
        state.record("build", {"commit": "a" * 40}, {"mini_installer.exe": "b" * 64}, T0, T1)
        again = release_state.State.load(self.path, "152.0.7977.149-1")
        self.assertTrue(again.is_done("build", {"commit": "a" * 40}))
        self.assertEqual(again.outputs("build"), {"mini_installer.exe": "b" * 64})
        doc = json.loads(self.path.read_text(encoding="utf-8"))
        self.assertEqual(doc["stages"]["build"]["started"], "2026-10-05T09:00:00+00:00")
        self.assertEqual(doc["stages"]["build"]["finished"], "2026-10-05T21:30:00+00:00")

    def test_changed_inputs_mean_the_stage_runs_again(self):
        state = release_state.State.load(self.path, "152.0.7977.149-1")
        state.record("build", {"commit": "a" * 40}, {}, T0, T1)
        self.assertFalse(state.is_done("build", {"commit": "c" * 40}))

    def test_forget(self):
        state = release_state.State.load(self.path, "152.0.7977.149-1")
        state.record("sign", {}, {}, T0, T1)
        state.forget("sign")
        self.assertFalse(release_state.State.load(self.path, "152.0.7977.149-1")
                         .is_done("sign", {}))

    def test_another_releases_state_is_refused(self):
        release_state.State.load(self.path, "152.0.7977.149-1").record("sync", {}, {}, T0, T1)
        with self.assertRaisesRegex(release_state.StateError, "152.0.7977.149-1"):
            release_state.State.load(self.path, "152.0.7977.149-2")

    def test_the_write_is_atomic(self):
        release_state.State.load(self.path, "152.0.7977.149-1").record("sync", {}, {}, T0, T1)
        self.assertEqual([p.name for p in self.path.parent.iterdir()], ["state.json"])
```

- [ ] **Step 2: Run them.**

Run: `cd /c/Users/robyv/Desktop/DLU/webops && python -m unittest discover -s tools/tests -t tools -p test_release_state.py -v 2>&1 | tail -3`
Expected: ERROR, `ModuleNotFoundError: No module named 'release_state'`.

- [ ] **Step 3: Implement** `tools/release_state.py`:

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""A release's state.json: what each stage of tools/release.py did.

    {"tag": T, "stages": {"build": {"inputs": {...}, "outputs": {...},
                                     "started": ISO 8601, "finished": ISO 8601}}}

A stage is done when it is recorded with the same inputs. A stage's inputs
include the hashes of the earlier stages' outputs it uses, so a stage that
runs again makes every later one that depends on it run again too.
"""

from __future__ import annotations

import datetime
import json
import os
from dataclasses import dataclass, field
from pathlib import Path


class StateError(Exception):
    pass


@dataclass
class State:
    path: Path
    tag: str
    stages: dict[str, dict] = field(default_factory=dict)

    @classmethod
    def load(cls, path: Path, tag: str) -> State:
        if not path.exists():
            return cls(path, tag)
        doc = json.loads(path.read_text(encoding="utf-8"))
        if doc.get("tag") != tag:
            raise StateError(f"{path} is the state of {doc.get('tag')}, not of {tag}")
        return cls(path, tag, doc.get("stages", {}))

    def is_done(self, stage: str, inputs: dict) -> bool:
        record = self.stages.get(stage)
        return record is not None and record["inputs"] == inputs

    def outputs(self, stage: str) -> dict:
        return self.stages[stage]["outputs"]

    def record(self, stage: str, inputs: dict, outputs: dict, started: datetime.datetime,
               finished: datetime.datetime) -> None:
        self.stages[stage] = {"inputs": inputs, "outputs": outputs,
                              "started": started.isoformat(timespec="seconds"),
                              "finished": finished.isoformat(timespec="seconds")}
        self.save()

    def forget(self, stage: str) -> None:
        self.stages.pop(stage, None)
        self.save()

    def save(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        temporary = self.path.with_name(self.path.name + ".new")
        temporary.write_text(json.dumps({"tag": self.tag, "stages": self.stages}, indent=1)
                             + "\n", encoding="utf-8", newline="\n")
        os.replace(temporary, self.path)
```

- [ ] **Step 4: Run the tests.** Same command as Step 2. Expected: `OK` (6 tests).

- [ ] **Step 5: Commit.**

```bash
cd /c/Users/robyv/Desktop/DLU/webops && git add tools/release_state.py tools/tests/test_release_state.py && python tools/lint.py && git commit -q -m "tools: a release's state, stage by stage"
```

---

### Task 4: Provenance and checksums

**Files:**
- Create: `tools/provenance.py`
- Test: `tools/tests/test_provenance.py`

- [ ] **Step 1: Write the failing tests** `tools/tests/test_provenance.py`:

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import datetime
import hashlib
import json
import tempfile
import unittest
from pathlib import Path

import provenance

UTC = datetime.timezone.utc
FACTS = provenance.BuildFacts(
    tag="152.0.7977.149-1", webops_commit="a" * 40, chromium_commit="b" * 40,
    series_digest="c" * 64, depot_tools_commit="d" * 40,
    toolchain={"clang": "llvmorg-23-init-19482-g53d18800-1", "windows-sdk": "10.0.26100.0"},
    args_gn='import("//ghost/build/args/release.gn")\nghost_signing_identity = "test"\n',
    identity="test", tests={"ghost_unittests": "release", "ghost_browsertests": "release"},
    started=datetime.datetime(2026, 10, 5, 9, 0, tzinfo=UTC),
    finished=datetime.datetime(2026, 10, 6, 7, 30, tzinfo=UTC))


class SumsTest(unittest.TestCase):
    def test_round_trip_sorted_by_name(self):
        text = provenance.sums_text({"b.exe": "1" * 64, "a.crx3": "2" * 64})
        self.assertEqual(text, f"{'2' * 64}  a.crx3\n{'1' * 64}  b.exe\n")
        self.assertEqual(provenance.parse_sums(text), {"a.crx3": "2" * 64, "b.exe": "1" * 64})

    def test_a_malformed_line_is_refused(self):
        with self.assertRaises(ValueError):
            provenance.parse_sums("not a sum\n")


class StatementTest(unittest.TestCase):
    def setUp(self):
        self.doc = provenance.statement({"update.crx3": "e" * 64, "a.exe": "f" * 64}, FACTS)

    def test_is_an_in_toto_statement_with_an_slsa_predicate(self):
        self.assertEqual(self.doc["_type"], "https://in-toto.io/Statement/v1")
        self.assertEqual(self.doc["predicateType"], "https://slsa.dev/provenance/v1")
        self.assertEqual(self.doc["subject"], [
            {"name": "a.exe", "digest": {"sha256": "f" * 64}},
            {"name": "update.crx3", "digest": {"sha256": "e" * 64}}])

    def test_records_how_the_release_was_made(self):
        definition = self.doc["predicate"]["buildDefinition"]
        self.assertEqual(definition["buildType"], provenance.BUILD_TYPE)
        self.assertEqual(definition["externalParameters"],
                         {"repository": provenance.REPOSITORY, "tag": "152.0.7977.149-1"})
        self.assertEqual(definition["internalParameters"]["args.gn"], FACTS.args_gn)
        self.assertEqual(definition["internalParameters"]["identity"], "test")
        self.assertEqual(definition["internalParameters"]["tests"], FACTS.tests)
        digests = [d.get("digest") for d in definition["resolvedDependencies"]]
        self.assertIn({"gitCommit": "a" * 40}, digests)
        self.assertIn({"gitCommit": "b" * 40}, digests)
        self.assertIn({"sha256": "c" * 64}, digests)
        self.assertIn({"gitCommit": "d" * 40}, digests)
        uris = [d["uri"] for d in definition["resolvedDependencies"]]
        self.assertIn("pkg:generic/clang@llvmorg-23-init-19482-g53d18800-1", uris)
        run = self.doc["predicate"]["runDetails"]
        self.assertEqual(run["builder"], {"id": provenance.BUILDER_ID})
        self.assertEqual(run["metadata"], {"invocationId": "152.0.7977.149-1",
                                           "startedOn": "2026-10-05T09:00:00Z",
                                           "finishedOn": "2026-10-06T07:30:00Z"})


class CheckFilesTest(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.dir = Path(tmp.name)
        for name, data in (("ProjectGhostOfflineSetup.exe", b"setup"), ("update.crx3", b"crx")):
            (self.dir / name).write_bytes(data)
        provenance.write_release_files(self.dir, ["ProjectGhostOfflineSetup.exe", "update.crx3"],
                                       FACTS)

    def test_a_complete_release_passes(self):
        self.assertEqual(provenance.check_files(self.dir), [])
        sums = provenance.parse_sums((self.dir / provenance.SUMS_FILE).read_text())
        self.assertEqual(set(sums), {"ProjectGhostOfflineSetup.exe", "update.crx3",
                                     provenance.PROVENANCE_FILE})
        self.assertEqual(sums["update.crx3"], hashlib.sha256(b"crx").hexdigest())

    def test_a_changed_file_fails(self):
        (self.dir / "update.crx3").write_bytes(b"other")
        self.assertEqual(provenance.check_files(self.dir),
                         [f"update.crx3: its SHA-256 differs from {provenance.SUMS_FILE}"])

    def test_a_missing_file_fails(self):
        (self.dir / "update.crx3").unlink()
        self.assertIn("update.crx3: missing", provenance.check_files(self.dir))

    def test_a_wrong_hash_in_the_provenance_fails(self):
        path = self.dir / provenance.PROVENANCE_FILE
        doc = json.loads(path.read_text(encoding="utf-8"))
        doc["subject"][0]["digest"]["sha256"] = "0" * 64
        path.write_text(json.dumps(doc), encoding="utf-8")
        failures = provenance.check_files(self.dir)
        self.assertIn(f"{doc['subject'][0]['name']}: the provenance names another SHA-256",
                      failures)
        self.assertIn(f"{provenance.PROVENANCE_FILE}: its SHA-256 differs from "
                      f"{provenance.SUMS_FILE}", failures)

    def test_no_sums_file(self):
        (self.dir / provenance.SUMS_FILE).unlink()
        self.assertEqual(provenance.check_files(self.dir), [f"no {provenance.SUMS_FILE}"])
```

- [ ] **Step 2: Run them.**

Run: `cd /c/Users/robyv/Desktop/DLU/webops && python -m unittest discover -s tools/tests -t tools -p test_provenance.py -v 2>&1 | tail -3`
Expected: ERROR, `No module named 'provenance'`.

- [ ] **Step 3: Implement** `tools/provenance.py`:

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""A release's provenance (SLSA Build Level 1) and checksums (sub-project E).

provenance.intoto.json is an in-toto Statement v1 whose predicate is SLSA
Provenance v1: the artifacts and their SHA-256, and how they were made (the
tag, the commits, the patch series, the toolchain, args.gn, where the tests
ran). It is not signed: the release is built on a maintainer's machine, not
on a build platform that would sign it (Level 2). The artifacts' integrity
comes from their Authenticode signatures and the CRX3 publisher proof.
SHA256SUMS lists every published file, the provenance included.
"""

from __future__ import annotations

import datetime
import hashlib
import json
import re
import urllib.parse
from dataclasses import dataclass
from pathlib import Path

REPOSITORY = "https://github.com/robyroro/project-ghost"
BUILD_TYPE = f"{REPOSITORY}/blob/main/docs/build/release.md#provenance"
BUILDER_ID = f"{REPOSITORY}/blob/main/docs/build/release.md#the-release-machine"
CHROMIUM = "https://chromium.googlesource.com/chromium/src"
DEPOT_TOOLS = "https://chromium.googlesource.com/chromium/tools/depot_tools"
SUMS_FILE = "SHA256SUMS"
PROVENANCE_FILE = "provenance.intoto.json"
_SUM_LINE = re.compile(r"^([0-9a-f]{64})  ([^/\\]+)$")


@dataclass(frozen=True)
class BuildFacts:
    tag: str
    webops_commit: str
    chromium_commit: str
    series_digest: str
    depot_tools_commit: str
    toolchain: dict[str, str]  # name -> version
    args_gn: str
    identity: str
    tests: dict[str, str]  # suite -> the output directory it ran from
    started: datetime.datetime
    finished: datetime.datetime


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sums_text(sums: dict[str, str]) -> str:
    return "".join(f"{digest}  {name}\n" for name, digest in sorted(sums.items()))


def parse_sums(text: str) -> dict[str, str]:
    sums = {}
    for line in text.splitlines():
        m = _SUM_LINE.match(line)
        if not m:
            raise ValueError(f"not a {SUMS_FILE} line: {line!r}")
        sums[m.group(2)] = m.group(1)
    return sums


def _utc(moment: datetime.datetime) -> str:
    return moment.astimezone(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def statement(subjects: dict[str, str], facts: BuildFacts) -> dict:
    tools = [{"name": name, "uri": f"pkg:generic/{name}@{urllib.parse.quote(version, safe='')}"}
             for name, version in sorted(facts.toolchain.items())]
    return {
        "_type": "https://in-toto.io/Statement/v1",
        "subject": [{"name": name, "digest": {"sha256": digest}}
                    for name, digest in sorted(subjects.items())],
        "predicateType": "https://slsa.dev/provenance/v1",
        "predicate": {
            "buildDefinition": {
                "buildType": BUILD_TYPE,
                "externalParameters": {"repository": REPOSITORY, "tag": facts.tag},
                "internalParameters": {"args.gn": facts.args_gn, "identity": facts.identity,
                                       "tests": facts.tests},
                "resolvedDependencies": [
                    {"uri": f"git+{REPOSITORY}@refs/tags/{facts.tag}",
                     "digest": {"gitCommit": facts.webops_commit}},
                    {"uri": f"git+{CHROMIUM}", "digest": {"gitCommit": facts.chromium_commit}},
                    {"uri": f"git+{REPOSITORY}@refs/tags/{facts.tag}#patches",
                     "digest": {"sha256": facts.series_digest}},
                    {"uri": f"git+{DEPOT_TOOLS}",
                     "digest": {"gitCommit": facts.depot_tools_commit}},
                    *tools,
                ],
            },
            "runDetails": {
                "builder": {"id": BUILDER_ID},
                "metadata": {"invocationId": facts.tag, "startedOn": _utc(facts.started),
                             "finishedOn": _utc(facts.finished)},
            },
        },
    }


def write_release_files(directory: Path, names: list[str], facts: BuildFacts) -> None:
    """Writes the provenance for `names` (files in directory), then SHA256SUMS."""
    subjects = {name: sha256_file(directory / name) for name in names}
    (directory / PROVENANCE_FILE).write_text(
        json.dumps(statement(subjects, facts), indent=2) + "\n", encoding="utf-8", newline="\n")
    sums = dict(subjects, **{PROVENANCE_FILE: sha256_file(directory / PROVENANCE_FILE)})
    (directory / SUMS_FILE).write_text(sums_text(sums), encoding="utf-8", newline="\n")


def check_files(directory: Path) -> list[str]:
    """Every file in SHA256SUMS is present with its hash, and the provenance agrees."""
    if not (directory / SUMS_FILE).exists():
        return [f"no {SUMS_FILE}"]
    sums = parse_sums((directory / SUMS_FILE).read_text(encoding="utf-8"))
    failures = []
    for name, digest in sorted(sums.items()):
        path = directory / name
        if not path.is_file():
            failures.append(f"{name}: missing")
        elif sha256_file(path) != digest:
            failures.append(f"{name}: its SHA-256 differs from {SUMS_FILE}")
    if PROVENANCE_FILE not in sums or not (directory / PROVENANCE_FILE).is_file():
        return failures + [f"{PROVENANCE_FILE}: not listed or missing"]
    doc = json.loads((directory / PROVENANCE_FILE).read_text(encoding="utf-8"))
    for subject in doc.get("subject", []):
        if sums.get(subject["name"]) != subject["digest"]["sha256"]:
            failures.append(f"{subject['name']}: the provenance names another SHA-256")
    return failures
```

- [ ] **Step 4: Run the tests.** Same command as Step 2. Expected: `OK` (9 tests).

- [ ] **Step 5: Commit.**

```bash
cd /c/Users/robyv/Desktop/DLU/webops && git add tools/provenance.py tools/tests/test_provenance.py && python tools/lint.py && git commit -q -m "tools: a release's provenance and checksums"
```

---

### Task 5: The SBOM

**Files:**
- Create: `tools/sbom.py`
- Test: `tools/tests/test_sbom.py`

Chromium's `tools/licenses/licenses.py license_file --format spdx` writes SPDX 2.2 JSON for one GN target's shipped dependencies: a root package `Chromium`, one package per third-party library with a `LicenseRef-…`, its license text in `hasExtractedLicensingInfos`, and a `CONTAINS` relationship from the root. `sbom.py` runs it for the browser's installer and the updater's, merges the two, and adds Ghost's own code.

- [ ] **Step 1: Write the failing tests** `tools/tests/test_sbom.py`:

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import unittest
from pathlib import Path

import sbom


def document(packages: list[tuple[str, str, str]]) -> dict:
    """An SPDX document as licenses.py writes it: (name, license ID, license text)."""
    root = "SPDXRef-Package-Chromium"
    doc = {"spdxVersion": "SPDX-2.2", "SPDXID": "SPDXRef-DOCUMENT", "name": "x",
           "documentNamespace": "https://example.invalid/x",
           "creationInfo": {"creators": ["Tool: spdx_writer.py"]}, "dataLicense": "CC0-1.0",
           "documentDescribes": [root], "packages": [], "hasExtractedLicensingInfos": [],
           "relationships": []}
    for name, license_id, text in [("Chromium", "LicenseRef-Chromium", "BSD")] + packages:
        package_id = f"SPDXRef-Package-{name}"
        doc["packages"].append({"SPDXID": package_id, "name": name,
                                "licenseConcluded": license_id})
        if name != "Chromium":
            doc["relationships"].append({"spdxElementId": root, "relationshipType": "CONTAINS",
                                         "relatedSpdxElement": package_id})
        if license_id not in [l["licenseId"] for l in doc["hasExtractedLicensingInfos"]]:
            doc["hasExtractedLicensingInfos"].append(
                {"name": name, "licenseId": license_id, "extractedText": text,
                 "crossRefs": [{"url": "https://example.invalid"}]})
    return doc


BROWSER = document([("zlib", "LicenseRef-zlib", "zlib text"),
                    ("libpng", "LicenseRef-libpng", "png text")])
UPDATER = document([("zlib", "LicenseRef-zlib", "zlib text"),
                    ("lzma", "LicenseRef-lzma", "lzma text"),
                    ("other", "LicenseRef-libpng", "a different text")])


class MergeTest(unittest.TestCase):
    def setUp(self):
        self.doc = sbom.merge(BROWSER, UPDATER)

    def test_adds_the_updaters_packages_once(self):
        self.assertEqual([p["name"] for p in self.doc["packages"]],
                         ["Chromium", "zlib", "libpng", "lzma", "other"])

    def test_a_license_id_with_another_text_is_renamed(self):
        other = next(p for p in self.doc["packages"] if p["name"] == "other")
        self.assertEqual(other["licenseConcluded"], "LicenseRef-libpng-updater")
        texts = {l["licenseId"]: l["extractedText"]
                 for l in self.doc["hasExtractedLicensingInfos"]}
        self.assertEqual(texts["LicenseRef-libpng"], "png text")
        self.assertEqual(texts["LicenseRef-libpng-updater"], "a different text")
        self.assertEqual(texts["LicenseRef-lzma"], "lzma text")

    def test_every_added_package_is_contained_by_the_root(self):
        contained = {r["relatedSpdxElement"] for r in self.doc["relationships"]}
        self.assertTrue({"SPDXRef-Package-lzma", "SPDXRef-Package-other"} <= contained)

    def test_the_inputs_are_unchanged(self):
        self.assertEqual(len(BROWSER["packages"]), 3)


class GhostTest(unittest.TestCase):
    def test_describes_the_release_with_ghosts_code(self):
        doc = sbom.complete(sbom.merge(BROWSER, UPDATER), version="152.0.7977.14901",
                            tag="152.0.7977.149-1", commit="a" * 40)
        self.assertEqual(doc["name"], "Project Ghost 152.0.7977.14901")
        self.assertEqual(doc["documentNamespace"],
                         "https://github.com/robyroro/project-ghost/releases/152.0.7977.149-1/sbom")
        ghost = next(p for p in doc["packages"] if p["SPDXID"] == sbom.GHOST_ID)
        self.assertEqual(ghost["licenseConcluded"], "MPL-2.0")
        self.assertEqual(ghost["versionInfo"], "152.0.7977.14901")
        self.assertEqual(ghost["downloadLocation"],
                         "git+https://github.com/robyroro/project-ghost@" + "a" * 40)
        self.assertEqual(doc["documentDescribes"], [sbom.GHOST_ID])
        self.assertIn({"spdxElementId": sbom.GHOST_ID, "relationshipType": "CONTAINS",
                       "relatedSpdxElement": "SPDXRef-Package-Chromium"}, doc["relationships"])
        self.assertIn("Tool: ghost/tools/sbom.py", doc["creationInfo"]["creators"])


class CommandTest(unittest.TestCase):
    def test_runs_chromiums_generator_for_one_target(self):
        src, out = Path(r"C:\src"), Path(r"C:\src\out\release")
        self.assertEqual(
            sbom.licenses_command("vpython3.bat", src, out, sbom.BROWSER_TARGET, Path("b.json")),
            ["vpython3.bat", str(src / "tools" / "licenses" / "licenses.py"), "license_file",
             "--format", "spdx", "--gn-out-dir", str(out), "--gn-target",
             "//chrome/installer/mini_installer:mini_installer", "--target-os", "win", "b.json"])
```

- [ ] **Step 2: Run them.**

Run: `cd /c/Users/robyv/Desktop/DLU/webops && python -m unittest discover -s tools/tests -t tools -p test_sbom.py -v 2>&1 | tail -3`
Expected: ERROR, `No module named 'sbom'`.

- [ ] **Step 3: Implement** `tools/sbom.py`:

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""A release's SBOM, sbom.spdx.json (sub-project E).

Chromium's tools/licenses/licenses.py writes SPDX 2.2 JSON for one GN
target's shipped third-party code. This runs it for the browser's installer
and the updater's, merges the two, and describes the release as Ghost's own
package (MPL-2.0) containing them.

    python tools/sbom.py --src SRC --out out\\release --tag T --commit C --output FILE
"""

from __future__ import annotations

import argparse
import copy
import datetime
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

import release_version
import repo

DOCUMENT = "sbom.spdx.json"
BROWSER_TARGET = "//chrome/installer/mini_installer:mini_installer"
UPDATER_TARGET = "//chrome/updater/win/installer:installer"
GHOST_ID = "SPDXRef-Package-Project-Ghost"
REPOSITORY = "https://github.com/robyroro/project-ghost"


def licenses_command(python: str, src: Path, out_dir: Path, target: str,
                     output: Path) -> list[str]:
    return [python, str(src / "tools" / "licenses" / "licenses.py"), "license_file",
            "--format", "spdx", "--gn-out-dir", str(out_dir), "--gn-target", target,
            "--target-os", "win", str(output)]


def merge(primary: dict, secondary: dict) -> dict:
    """primary, plus secondary's packages it lacks (by name) and their licenses."""
    doc = copy.deepcopy(primary)
    root = doc["documentDescribes"][0]
    names = {p["name"] for p in doc["packages"]}
    ids = {p["SPDXID"] for p in doc["packages"]}
    texts = {l["licenseId"]: l["extractedText"] for l in doc["hasExtractedLicensingInfos"]}
    theirs = {l["licenseId"]: l for l in secondary["hasExtractedLicensingInfos"]}
    renamed: dict[str, str] = {}
    for package in secondary["packages"]:
        if package["name"] in names:
            continue
        license_id = package["licenseConcluded"]
        if license_id not in renamed and license_id in theirs:
            text = theirs[license_id]["extractedText"]
            new_id = license_id if texts.get(license_id, text) == text else f"{license_id}-updater"
            if new_id not in texts:
                doc["hasExtractedLicensingInfos"].append(
                    dict(copy.deepcopy(theirs[license_id]), licenseId=new_id))
                texts[new_id] = text
            renamed[license_id] = new_id
        package_id = package["SPDXID"] if package["SPDXID"] not in ids \
            else f"{package['SPDXID']}-updater"
        doc["packages"].append(dict(package, SPDXID=package_id,
                                    licenseConcluded=renamed.get(license_id, license_id)))
        doc["relationships"].append({"spdxElementId": root, "relationshipType": "CONTAINS",
                                     "relatedSpdxElement": package_id})
        names.add(package["name"])
        ids.add(package_id)
    return doc


def complete(doc: dict, version: str, tag: str, commit: str) -> dict:
    """Describes the release: Ghost's package, MPL-2.0, contains Chromium's root."""
    doc = copy.deepcopy(doc)
    chromium = doc["documentDescribes"][0]
    doc["name"] = f"Project Ghost {version}"
    doc["documentNamespace"] = f"{REPOSITORY}/releases/{tag}/sbom"
    doc["creationInfo"] = {
        "created": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "creators": ["Tool: ghost/tools/sbom.py", *doc["creationInfo"]["creators"]]}
    doc["packages"].insert(0, {
        "SPDXID": GHOST_ID, "name": "Project Ghost", "versionInfo": version,
        "downloadLocation": f"git+{REPOSITORY}@{commit}", "licenseConcluded": "MPL-2.0",
        "comment": "Ghost's code (//ghost) and its Chromium patch series (patches/)."})
    doc["documentDescribes"] = [GHOST_ID]
    doc["relationships"].insert(0, {"spdxElementId": GHOST_ID, "relationshipType": "CONTAINS",
                                    "relatedSpdxElement": chromium})
    return doc


def build(python: str, src: Path, out_dir: Path, tag: str, commit: str, output: Path,
          env: dict[str, str] | None = None) -> None:
    version = release_version.parse_tag(tag, repo.read_chromium_version()).version
    with tempfile.TemporaryDirectory() as tmp:
        docs = []
        for target in (BROWSER_TARGET, UPDATER_TARGET):
            path = Path(tmp) / f"{len(docs)}.json"
            subprocess.run(licenses_command(python, src, out_dir, target, path), cwd=src,
                           env=env, check=True)
            docs.append(json.loads(path.read_text(encoding="utf-8")))
    doc = complete(merge(*docs), version, tag, commit)
    output.write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8", newline="\n")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--src", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True, help="the output directory, in src")
    parser.add_argument("--tag", required=True)
    parser.add_argument("--commit", required=True, help="the tag's commit in this repository")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--python", default="vpython3.bat" if os.name == "nt" else "vpython3")
    args = parser.parse_args(argv)
    try:
        build(args.python, args.src.resolve(), (args.src / args.out).resolve(), args.tag,
              args.commit, args.output)
    except (subprocess.CalledProcessError, release_version.ReleaseVersionError) as e:
        print(f"sbom: {e}", file=sys.stderr)
        return 1
    print(f"wrote {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Run the tests.** Same command as Step 2. Expected: `OK` (7 tests).

- [ ] **Step 5: Commit.**

```bash
cd /c/Users/robyv/Desktop/DLU/webops && git add tools/sbom.py tools/tests/test_sbom.py && python tools/lint.py && git commit -q -m "tools: a release's SBOM, from Chromium's generator"
```

The real run against `out/release` happens in Task 18, once the official build exists; `licenses.py` needs `gn` on `PATH`, which `release.py` provides.

---
### Task 6: `release.py`: the context and the checks

**Files:**
- Create: `tools/release.py`
- Test: `tools/tests/test_release.py`

- [ ] **Step 1: Write the failing tests** `tools/tests/test_release.py`:

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import subprocess
import unittest
from pathlib import Path

import release
from tests.gitutil import GitTestCase

TAG = "152.0.7977.149-1"
VERSION_FILE = "MAJOR=152\nMINOR=0\nBUILD=7977\nPATCH=149\n"


class ArgsTest(unittest.TestCase):
    def test_the_release_configuration_and_its_identity(self):
        self.assertEqual(release.render_args("test", None),
                         "# Written by tools/release.py; edit build/args/release.gn instead.\n"
                         'import("//ghost/build/args/release.gn")\n'
                         'ghost_signing_identity = "test"\n')

    def test_an_update_url(self):
        self.assertTrue(release.render_args("test", "https://203.0.113.5/update").endswith(
            'ghost_update_url = "https://203.0.113.5/update"\n'))

    def test_the_browser_app_id(self):
        self.assertEqual(release.browser_appid(), "{c0ff4371-d9ab-461e-bffd-6b0dc2430b02}")


class ReleaseRepos(GitTestCase):
    """A pushed, tagged repository; a Chromium checkout with it at src/ghost."""

    def setUp(self):
        super().setUp()
        self.origin = self.tmp / "origin.git"
        subprocess.run(["git", "init", "-q", "--bare", "-b", "main", str(self.origin)],
                       check=True)
        self.webops = self.init_repo("webops")
        self.commit(self.webops, {"CHROMIUM_VERSION": "152.0.7977.149\n",
                                  "branding/updater.gni": 'browser_appid = "{c0ff4371-d9ab-'
                                  '461e-bffd-6b0dc2430b02}"\n'}, "Initial")
        self.git(self.webops, "remote", "add", "origin", str(self.origin))
        self.git(self.webops, "tag", "-a", "-m", "Release", TAG)
        self.git(self.webops, "push", "-q", "origin", "main", TAG)
        self.git(self.webops, "fetch", "-q", "origin")
        self.src = self.init_repo("src")
        self.commit(self.src, {"chrome/VERSION": VERSION_FILE, "README.md": "chromium\n"},
                    "Upstream")
        self.git(self.tmp, "clone", "-q", str(self.webops), str(self.src / "ghost"))
        with open(self.src / ".git" / "info" / "exclude", "a", encoding="utf-8") as f:
            f.write("/ghost/\n")
        self.ctx = release.Context(tag=TAG, src=self.src, webops=self.webops,
                                   releases=self.tmp / "releases")

    def check(self, ctx=None, conclusions=("success",)):
        return release.check(ctx or self.ctx,
                             ci_conclusions=lambda webops, commit: list(conclusions))


class CheckTest(ReleaseRepos):
    def test_a_pushed_tag_on_clean_checkouts_passes(self):
        self.assertEqual(self.check(), [])

    def test_a_missing_tag(self):
        ctx = release.Context(tag="152.0.7977.149-3", src=self.src, webops=self.webops)
        self.assertEqual(self.check(ctx), [f"there is no tag 152.0.7977.149-3 in {self.webops}"])

    def test_a_tag_for_another_chromium(self):
        ctx = release.Context(tag="153.0.7000.1-1", src=self.src, webops=self.webops)
        [problem] = self.check(ctx)
        self.assertIn("is for Chromium 153.0.7000.1", problem)

    def test_the_tag_must_be_pushed(self):
        self.git(self.webops, "tag", "-a", "-m", "Release", "152.0.7977.149-2")
        ctx = release.Context(tag="152.0.7977.149-2", src=self.src, webops=self.webops)
        self.assertIn("the tag 152.0.7977.149-2 is not on origin", "\n".join(self.check(ctx)))

    def test_the_tags_commit_must_be_on_origins_main(self):
        self.git(self.webops, "checkout", "-q", "-b", "side")
        self.commit(self.webops, {"side.txt": "x\n"}, "Side")
        self.git(self.webops, "tag", "-a", "-m", "Release", "152.0.7977.149-2")
        self.git(self.webops, "push", "-q", "origin", "side", "152.0.7977.149-2")
        ctx = release.Context(tag="152.0.7977.149-2", src=self.src, webops=self.webops)
        self.assertIn("is not on origin's main", "\n".join(self.check(ctx)))

    def test_the_tooling_workflow_must_have_passed(self):
        self.assertIn("the tooling workflow has not passed",
                      "\n".join(self.check(conclusions=("failure",))))

    def test_this_repository_must_be_at_the_tag(self):
        self.commit(self.webops, {"later.txt": "x\n"}, "Later")
        self.assertIn("this repository is at", "\n".join(self.check()))

    def test_src_ghost_must_be_at_the_tag(self):
        self.commit(self.src / "ghost", {"later.txt": "x\n"}, "Later")
        self.assertIn("src/ghost is at", "\n".join(self.check()))

    def test_untracked_files_fail(self):
        self.write(self.webops, "stray.txt", "x\n")
        self.assertIn("this repository has uncommitted or untracked files",
                      "\n".join(self.check()))

    def test_local_changes_in_chromium_fail(self):
        self.write(self.src, "README.md", "changed\n")
        self.assertIn("has local changes", "\n".join(self.check()))

    def test_another_configuration_in_out_release_fails(self):
        self.write(self.src, "out/release/args.gn", "is_official_build = false\n")
        self.assertIn("args.gn differs", "\n".join(self.check()))

    def test_the_same_configuration_passes(self):
        self.write(self.src, "out/release/args.gn", release.render_args("test", None))
        self.assertEqual(self.check(), [])
```

- [ ] **Step 2: Run them.**

Run: `cd /c/Users/robyv/Desktop/DLU/webops && python -m unittest discover -s tools/tests -t tools -p test_release.py -v 2>&1 | tail -3`
Expected: ERROR, `No module named 'release'`.

- [ ] **Step 3: Implement** `tools/release.py`:

```python
#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Makes a release from a release tag, on the machine that holds the keys (sub-project E).

  run --tag T --src SRC [--identity test] [--update-url URL] [--redo STAGE]
      [--stage FRACTION --host USER@HOST] [--public] [--releases DIR]
        checks the repositories, then runs the stages: sync, apply, build,
        test, sign, describe, draft, stage. Each records its inputs and
        outputs in <releases>/<tag>/state.json; a re-run skips the stages
        whose inputs are unchanged.
  verify DIR [--identity test]
        every hash, the provenance, the Authenticode signature, the CRX3 proof
  rollout --fraction F | halt | promote | drop   --host USER@HOST [--appid A]
        the update server's candidate release, through ghost-update-admin

docs/build/release.md is the runbook.
"""

from __future__ import annotations

import json
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

import release_version
import repo

OUT = Path("out") / "release"
DEFAULT_RELEASES = Path.home() / "ProjectGhostReleases"
DEFAULT_DEPOT_TOOLS = Path(r"C:\src\depot_tools")
DEFAULT_SERVER_REPO = repo.REPO_ROOT.parent / "project-ghost-update-server"
# Identities that never make a public release: the development keys are
# committed, and the test identity's name isn't cleared (docs/licensing.md).
NOT_PUBLIC = ("dev", "test")


class ReleaseError(Exception):
    pass


def render_args(identity: str, update_url: str | None) -> str:
    lines = ["# Written by tools/release.py; edit build/args/release.gn instead.",
             'import("//ghost/build/args/release.gn")',
             f'ghost_signing_identity = "{identity}"']
    if update_url:
        lines.append(f'ghost_update_url = "{update_url}"')
    return "\n".join(lines) + "\n"


def browser_appid(root: Path = repo.REPO_ROOT) -> str:
    text = (root / "branding" / "updater.gni").read_text(encoding="utf-8")
    m = re.search(r'^\s*browser_appid\s*=\s*"(\{[0-9a-fA-F-]{36}\})"', text, re.M)
    if not m:
        raise ReleaseError("branding/updater.gni has no browser_appid")
    return m.group(1)


@dataclass(frozen=True)
class Context:
    tag: str
    src: Path
    identity: str = "test"
    update_url: str | None = None
    webops: Path = repo.REPO_ROOT
    depot_tools: Path = DEFAULT_DEPOT_TOOLS
    releases: Path = DEFAULT_RELEASES
    server_repo: Path = DEFAULT_SERVER_REPO
    host: str | None = None
    fraction: float | None = None
    public: bool = False
    jobs: int = 10
    python: str = sys.executable

    @property
    def root(self) -> Path:
        return self.src.parent

    @property
    def out(self) -> Path:
        return self.src / OUT

    @property
    def dir(self) -> Path:
        return self.releases / self.tag

    @property
    def chromium_version(self) -> str:
        return repo.read_chromium_version(self.webops)

    @property
    def version(self) -> str:
        return release_version.parse_tag(self.tag, self.chromium_version).version

    @property
    def base(self) -> str:
        return f"refs/tags/{self.chromium_version}"


def _git(directory: Path, *args: str, check: bool = True) -> str:
    proc = subprocess.run(["git", "-C", str(directory), *args], capture_output=True,
                          text=True, encoding="utf-8", errors="replace")
    if check and proc.returncode:
        raise ReleaseError(f"git {' '.join(args)} in {directory} failed: {proc.stderr.strip()}")
    return proc.stdout.strip() if proc.returncode == 0 else ""


def tag_commit(ctx: Context) -> str:
    return _git(ctx.webops, "rev-parse", "--verify", "--quiet",
                f"refs/tags/{ctx.tag}^{{commit}}", check=False)


def _ci_conclusions(webops: Path, commit: str) -> list[str]:
    """The conclusions of the tooling workflow's runs on a commit, from GitHub."""
    proc = subprocess.run(["gh", "run", "list", "--commit", commit, "--workflow", "tooling.yml",
                           "--json", "conclusion", "--limit", "20"], cwd=webops,
                          capture_output=True, text=True)
    if proc.returncode:
        return []
    return [run["conclusion"] for run in json.loads(proc.stdout)]


def check(ctx: Context, ci_conclusions=_ci_conclusions) -> list[str]:
    """Everything that must hold before a release is built, all reported at once."""
    try:
        ctx.version
    except (release_version.ReleaseVersionError, repo.RepoError) as e:
        return [str(e)]
    commit = tag_commit(ctx)
    if not commit:
        return [f"there is no tag {ctx.tag} in {ctx.webops}"]
    problems = []
    ref = f"refs/tags/{ctx.tag}"
    listed = _git(ctx.webops, "ls-remote", "origin", ref, f"{ref}^{{}}", check=False)
    remote = dict(reversed(line.split("\t", 1)) for line in listed.splitlines())
    if remote.get(f"{ref}^{{}}", remote.get(ref)) != commit:
        problems.append(f"the tag {ctx.tag} is not on origin as {commit[:12]}: push it")
    _git(ctx.webops, "fetch", "-q", "origin", "main", check=False)
    on_main = subprocess.run(["git", "-C", str(ctx.webops), "merge-base", "--is-ancestor",
                              commit, "refs/remotes/origin/main"], capture_output=True)
    if on_main.returncode:
        problems.append(f"the tag's commit {commit[:12]} is not on origin's main")
    if "success" not in ci_conclusions(ctx.webops, commit):
        problems.append(f"the tooling workflow has not passed on {commit[:12]}")
    for name, path in (("this repository", ctx.webops), ("src/ghost", ctx.src / "ghost")):
        head = _git(path, "rev-parse", "HEAD", check=False)
        if head != commit:
            problems.append(f"{name} is at {head[:12] or 'nothing'}, not at the tag's commit "
                            f"{commit[:12]}")
        if _git(path, "status", "--porcelain"):
            problems.append(f"{name} has uncommitted or untracked files")
    if _git(ctx.src, "status", "--porcelain", "--untracked-files=no"):
        problems.append(f"{ctx.src} has local changes, which no release may build: "
                        "commit them to the series or check them out")
    args = ctx.out / "args.gn"
    if args.exists() and args.read_text(encoding="utf-8") != render_args(ctx.identity,
                                                                         ctx.update_url):
        problems.append(f"{args} differs from this release's configuration: find out what "
                        "changed it")
    return problems
```

- [ ] **Step 4: Run the tests.** Same command as Step 2. Expected: `OK` (15 tests).

- [ ] **Step 5: Commit.**

```bash
cd /c/Users/robyv/Desktop/DLU/webops && git add tools/release.py tools/tests/test_release.py && python tools/lint.py && git commit -q -m "tools: release.py checks the repositories before a release"
```

---

### Task 7: `release.py`: the commands each stage runs, and the release notes

**Files:**
- Modify: `tools/release.py`
- Test: `tools/tests/test_release.py`

- [ ] **Step 1: Write the failing tests.** Append to `tools/tests/test_release.py`:

```python
SRC = Path(r"C:\w\chromium\src")
DEPOT = Path(r"C:\src\depot_tools")
WEBOPS = release.repo.REPO_ROOT  # its CHROMIUM_VERSION, 152.0.7977.149, gives the versions
CTX = release.Context(tag=TAG, src=SRC, webops=WEBOPS, depot_tools=DEPOT,
                      releases=Path(r"C:\r"), python="python.exe")


def tool(name):
    return str(DEPOT / (name + (".bat" if release.os.name == "nt" else "")))


class CommandTest(unittest.TestCase):
    def test_build(self):
        self.assertEqual(release.build_commands(CTX), [
            [tool("gn"), "gen", str(release.OUT)],
            [tool("autoninja"), "-C", str(release.OUT), "-j", "10", "chrome", "mini_installer",
             "chrome/updater/win/installer:installer", "chrome/updater/win:signing",
             "chrome/updater/win:updater", "ghost_unittests", "ghost_browsertests"]])

    def test_tests_run_on_the_bits_that_ship(self):
        results = Path(r"C:\r\results")
        commands = release.test_commands(CTX, results)
        self.assertEqual(commands[0], [str(SRC / release.OUT / "ghost_unittests.exe"),
                                       f"--test-launcher-summary-output="
                                       f"{results / 'ghost_unittests.json'}"])
        self.assertEqual(commands[1][0], str(SRC / release.OUT / "ghost_browsertests.exe"))
        self.assertEqual(commands[2], ["python.exe", str(WEBOPS / "tools" / "installer_smoke.py"),
                                       "sandbox", "--installer",
                                       str(SRC / release.OUT / "mini_installer.exe")])
        self.assertEqual(commands[3], ["python.exe", str(WEBOPS / "tools" / "egress_audit.py"),
                                       "run", "--chrome", str(SRC / release.OUT / "chrome.exe"),
                                       "--netlog", str(results / "netlog.json")])

    def test_sign_once_for_both_products(self):
        self.assertEqual(release.sign_command(CTX, "{c0ff4371-d9ab-461e-bffd-6b0dc2430b02}"), [
            "python.exe", str(WEBOPS / "tools" / "sign_release.py"), "--src", str(SRC),
            "--browser-out", str(release.OUT), "--updater-out", str(release.OUT),
            "--identity", "test", "--output", str(Path(r"C:\r") / TAG / "signed"),
            "--crx", "--offline-installer", "--version", "152.0.7977.14901",
            "--appid", "{c0ff4371-d9ab-461e-bffd-6b0dc2430b02}"])

    def test_a_draft_unless_public(self):
        publish, notes = Path(r"C:\r\p"), Path(r"C:\r\notes.md")
        draft = release.draft_command(CTX, publish, notes)
        self.assertEqual(draft[:10], ["gh", "release", "create", TAG, "--verify-tag", "--title",
                                      "Project Ghost 152.0.7977.14901 (test identity)",
                                      "--notes-file", str(notes), "--prerelease"])
        self.assertIn("--draft", draft)
        self.assertEqual(draft[-5:], [str(publish / name) for name in release.PUBLISHED])
        public = release.Context(tag=TAG, src=SRC, webops=WEBOPS, identity="prod", public=True)
        self.assertNotIn("--draft", release.draft_command(public, publish, notes))

    def test_stage_uploads_the_candidate(self):
        ctx = release.Context(tag=TAG, src=SRC, webops=WEBOPS, host="ghost@203.0.113.5",
                              fraction=0.01, python="python.exe")
        self.assertEqual(release.stage_command(ctx, Path(r"C:\r\update.crx3"), "{a}"), [
            "python.exe", "-m", "ghost_update.release", "--crx", str(Path(r"C:\r\update.crx3")),
            "--appid", "{a}", "--version", "152.0.7977.14901", "--identity", "test",
            "--host", "ghost@203.0.113.5", "--fraction", "0.01"])

    def test_admin_commands(self):
        self.assertEqual(release.admin_command("ghost@h", "set-fraction", "{a}", 0.05),
                         ["ssh", "ghost@h", "sudo ghost-update-admin set-fraction --appid '{a}' "
                                            "--fraction 0.05"])
        self.assertEqual(release.admin_command("ghost@h", "halt", "{a}"),
                         ["ssh", "ghost@h", "sudo ghost-update-admin halt --appid '{a}'"])


class NotesTest(unittest.TestCase):
    def test_the_test_identity_is_announced(self):
        notes = release.release_notes(CTX)
        self.assertIn("**Test identity: not for daily use.**", notes)
        self.assertIn("won't migrate", notes)
        self.assertIn("152.0.7977.149", notes)
        for name in release.PUBLISHED:
            self.assertIn(f"`{name}`", notes)
```

- [ ] **Step 2: Run them.** Same command as Task 6 Step 2. Expected: the new tests ERROR with `AttributeError: module 'release' has no attribute 'build_commands'` (and the others).

- [ ] **Step 3: Implement.** Add to `tools/release.py`, after `check`, and add `import os`, `import shlex` to the imports, `import offline_installer`, `import provenance`, `import sbom` to the module imports:

```python
SHIPPED_TARGETS = ("chrome", "mini_installer", "chrome/updater/win/installer:installer",
                   "chrome/updater/win:signing", "chrome/updater/win:updater")
# Where ghost_browsertests is built and run. out/release, unless the spike
# found its LTO link unaffordable (progress notes); then the development build.
BROWSERTESTS_OUT = OUT
SIGNED = "signed"
PUBLISH = "publish"
PUBLISHED = (offline_installer.OUTPUT_NAME, "update.crx3", sbom.DOCUMENT,
             provenance.PROVENANCE_FILE, provenance.SUMS_FILE)


def _tool(depot_tools: Path, name: str) -> str:
    return str(depot_tools / (f"{name}.bat" if os.name == "nt" else name))


def build_targets() -> tuple[str, ...]:
    tests = ("ghost_unittests",) + (("ghost_browsertests",) if BROWSERTESTS_OUT == OUT else ())
    return SHIPPED_TARGETS + tests


def build_commands(ctx: Context) -> list[list[str]]:
    return [[_tool(ctx.depot_tools, "gn"), "gen", str(OUT)],
            [_tool(ctx.depot_tools, "autoninja"), "-C", str(OUT), "-j", str(ctx.jobs),
             *build_targets()]]


def test_commands(ctx: Context, results: Path) -> list[list[str]]:
    commands = []
    if BROWSERTESTS_OUT != OUT:
        commands.append([_tool(ctx.depot_tools, "autoninja"), "-C", str(BROWSERTESTS_OUT),
                         "-j", str(ctx.jobs), "ghost_browsertests"])
    for suite, out in (("ghost_unittests", OUT), ("ghost_browsertests", BROWSERTESTS_OUT)):
        commands.append([str(ctx.src / out / f"{suite}.exe"),
                         f"--test-launcher-summary-output={results / (suite + '.json')}"])
    tools = ctx.webops / "tools"
    commands.append([ctx.python, str(tools / "installer_smoke.py"), "sandbox", "--installer",
                     str(ctx.out / "mini_installer.exe")])
    # The NetLog holds this machine's addresses: it stays in the release
    # directory, which is never published.
    commands.append([ctx.python, str(tools / "egress_audit.py"), "run", "--chrome",
                     str(ctx.out / "chrome.exe"), "--netlog", str(results / "netlog.json")])
    return commands


def sign_command(ctx: Context, appid: str) -> list[str]:
    return [ctx.python, str(ctx.webops / "tools" / "sign_release.py"), "--src", str(ctx.src),
            "--browser-out", str(OUT), "--updater-out", str(OUT), "--identity", ctx.identity,
            "--output", str(ctx.dir / SIGNED), "--crx", "--offline-installer",
            "--version", ctx.version, "--appid", appid]


def draft_command(ctx: Context, publish: Path, notes: Path) -> list[str]:
    title = f"Project Ghost {ctx.version}" + ("" if ctx.public else f" ({ctx.identity} identity)")
    command = ["gh", "release", "create", ctx.tag, "--verify-tag", "--title", title,
               "--notes-file", str(notes), "--prerelease"]
    if not ctx.public:
        command.append("--draft")
    return command + [str(publish / name) for name in PUBLISHED]


def stage_command(ctx: Context, crx: Path, appid: str) -> list[str]:
    return [ctx.python, "-m", "ghost_update.release", "--crx", str(crx), "--appid", appid,
            "--version", ctx.version, "--identity", ctx.identity, "--host", ctx.host,
            "--fraction", str(ctx.fraction)]


def admin_command(host: str, action: str, appid: str, fraction: float | None = None) -> list[str]:
    args = ["sudo", "ghost-update-admin", action, "--appid", appid]
    if fraction is not None:
        args += ["--fraction", str(fraction)]
    return ["ssh", host, shlex.join(args)]


def release_notes(ctx: Context) -> str:
    lines = [f"Project Ghost {ctx.version}: Chromium {ctx.chromium_version} with Ghost's patch "
             f"series, from the tag `{ctx.tag}`.", ""]
    if ctx.identity in NOT_PUBLIC:
        lines += ["**Test identity: not for daily use.** This build carries Phase 2's "
                  f"{ctx.identity} identity: test app IDs, a certificate only test machines "
                  "trust, and test update keys. Installs of it won't migrate to the final "
                  "product name.", ""]
    lines += [
        "| File | What it is |", "|---|---|",
        f"| `{offline_installer.OUTPUT_NAME}` | The signed offline installer |",
        "| `update.crx3` | The update package, with Ghost's publisher proof |",
        f"| `{sbom.DOCUMENT}` | The SBOM (SPDX 2.2) |",
        f"| `{provenance.PROVENANCE_FILE}` | How it was built (SLSA Build Level 1, unsigned) |",
        f"| `{provenance.SUMS_FILE}` | The SHA-256 of each file |", "",
        "Check them with `python tools/release.py verify <directory>` "
        "([docs/build/release.md](https://github.com/robyroro/project-ghost/blob/main/docs/"
        "build/release.md)).",
    ]
    return "\n".join(lines) + "\n"
```

- [ ] **Step 4: Run the tests.** Same command. Expected: `OK` (22 tests).

- [ ] **Step 5: Commit.**

```bash
cd /c/Users/robyv/Desktop/DLU/webops && git add tools/release.py tools/tests/test_release.py && python tools/lint.py && git commit -q -m "tools: the commands of each release stage, and the release notes"
```

---

### Task 8: `release.py`: the stages, resumable

**Files:**
- Modify: `tools/release.py`
- Test: `tools/tests/test_release.py`

- [ ] **Step 1: Write the failing tests.** Append to `tools/tests/test_release.py` (and `import datetime`, `import release_state` at its top):

```python
class Recorder:
    """Stands in for the runner: records each command, fails on request."""

    def __init__(self, fail_on: str | None = None):
        self.commands, self.fail_on = [], fail_on

    def __call__(self, argv, cwd=None):
        self.commands.append(list(argv))
        if self.fail_on and any(self.fail_on in str(a) for a in argv):
            raise subprocess.CalledProcessError(1, argv)


def stage(name, log, outputs=None, wanted=lambda ctx: True):
    def perform(ctx, state, run):
        log.append(name)
        return outputs or {name: "done"}
    return release.Stage(name, lambda ctx, state: {"input": name}, perform, wanted)


class RunStagesTest(unittest.TestCase):
    def setUp(self):
        import tempfile
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.state = release_state.State.load(Path(tmp.name) / "state.json", TAG)
        self.clock = iter(datetime.datetime(2026, 10, 5, 9, m, tzinfo=datetime.timezone.utc)
                          for m in range(60))

    def run_stages(self, stages, redo=None):
        release.run_stages(CTX, self.state, Recorder(), stages=stages, redo=redo,
                           now=lambda: next(self.clock))

    def test_runs_in_order_and_records(self):
        log = []
        self.run_stages([stage("a", log), stage("b", log)])
        self.assertEqual(log, ["a", "b"])
        self.assertTrue(self.state.is_done("b", {"input": "b"}))

    def test_a_rerun_skips_what_is_done(self):
        log = []
        self.run_stages([stage("a", log), stage("b", log)])
        self.run_stages([stage("a", log), stage("b", log)])
        self.assertEqual(log, ["a", "b"])

    def test_redo_runs_one_stage_again(self):
        log = []
        self.run_stages([stage("a", log), stage("b", log)])
        self.run_stages([stage("a", log), stage("b", log)], redo="a")
        self.assertEqual(log, ["a", "b", "a"])

    def test_a_failed_stage_is_not_recorded(self):
        def fail(ctx, state, run):
            raise release.ReleaseError("no")
        with self.assertRaises(release.ReleaseError):
            self.run_stages([release.Stage("a", lambda c, s: {}, fail, lambda c: True)])
        self.assertNotIn("a", self.state.stages)

    def test_an_unwanted_stage_is_skipped(self):
        log = []
        self.run_stages([stage("a", log, wanted=lambda ctx: False)])
        self.assertEqual(log, [])

    def test_the_stage_stage_runs_only_with_a_fraction(self):
        stage_stage = next(s for s in release.STAGES if s.name == "stage")
        self.assertFalse(stage_stage.wanted(CTX))
        self.assertTrue(stage_stage.wanted(release.Context(tag=TAG, src=SRC, webops=WEBOPS,
                                                           fraction=0.0, host="h")))

    def test_the_stage_names(self):
        self.assertEqual([s.name for s in release.STAGES],
                         ["sync", "apply", "build", "test", "sign", "describe", "draft",
                          "stage"])


class BuildStageTest(ReleaseRepos):
    def test_chrome_version_is_restored_when_the_build_fails(self):
        run = Recorder(fail_on="autoninja")
        state = release_state.State.load(self.tmp / "state.json", TAG)
        state.record("apply", {}, {"series": "d" * 64}, datetime.datetime.now(),
                     datetime.datetime.now())
        ctx = release.Context(tag=TAG, src=self.src, webops=self.webops, python="python")

        def write_version(argv, cwd=None):
            run(argv, cwd)
            if "release_version.py" in " ".join(argv):
                self.write(self.src, "chrome/VERSION", VERSION_FILE.replace("PATCH=149",
                                                                            "PATCH=14901"))

        with self.assertRaises(subprocess.CalledProcessError):
            release.build_perform(ctx, state, write_version)
        self.assertEqual((self.src / "chrome" / "VERSION").read_text(), VERSION_FILE)
        self.assertEqual((ctx.out / "args.gn").read_text(), release.render_args("test", None))
```

- [ ] **Step 2: Run them.** Expected: ERROR, `module 'release' has no attribute 'Stage'`.

- [ ] **Step 3: Implement.** Add to `tools/release.py` (imports: `datetime`, `shutil`, `from collections.abc import Callable`, and the modules `builder`, `patches`, `release_state`):

```python
Runner = Callable[..., None]  # run(argv, cwd=None); raises CalledProcessError


def _env(ctx: Context) -> dict[str, str]:
    env = dict(os.environ, DEPOT_TOOLS_WIN_TOOLCHAIN="0")
    env["PATH"] = str(ctx.depot_tools) + os.pathsep + env.get("PATH", "")
    return env


def runner_for(ctx: Context) -> Runner:
    env = _env(ctx)

    def run(argv: list[str], cwd: Path | None = None) -> None:
        print("    " + subprocess.list2cmdline([str(a) for a in argv]), flush=True)
        subprocess.run(argv, cwd=cwd, env=env, check=True)
    return run


@dataclass(frozen=True)
class Stage:
    name: str
    inputs: Callable[[Context, release_state.State], dict]
    perform: Callable[[Context, release_state.State, Runner], dict]
    wanted: Callable[[Context], bool] = lambda ctx: True


def _pin(ctx: Context) -> str:
    return repo.read_chromium_commit(ctx.webops)


def _series(ctx: Context) -> str:
    return builder.series_digest(ctx.webops / "patches", _pin(ctx))


def pgo_profile(ctx: Context) -> str:
    return (ctx.src / "chrome" / "build" / "win64.pgo.txt").read_text(encoding="utf-8").strip()


def sync_perform(ctx: Context, state: release_state.State, run: Runner) -> dict:
    pin = _pin(ctx)
    if not builder.is_synced(ctx.root, pin):
        run([ctx.python, str(ctx.webops / "tools" / "bootstrap.py"), "--root", str(ctx.root),
             "--depot-tools", str(ctx.depot_tools), "--pgo"])
        (ctx.root / builder.SYNC_STAMP).write_text(pin + "\n", encoding="utf-8")
    # The win64 hook from DEPS: idempotent, and quick when the profile is there.
    run([_tool(ctx.depot_tools, "vpython3"), "tools/update_pgo_profiles.py", "--target=win64",
         "update", "--gs-url-base=chromium-optimization-profiles/pgo_profiles"], cwd=ctx.src)
    return {"pin": pin, "profile": pgo_profile(ctx)}


def apply_perform(ctx: Context, state: release_state.State, run: Runner) -> dict:
    patches_dir = ctx.webops / "patches"
    if not patches.series_matches(ctx.src, ctx.base, patches_dir):
        run([ctx.python, str(ctx.webops / "tools" / "patches.py"), "apply", "--src",
             str(ctx.src), "--force"])
        if not patches.series_matches(ctx.src, ctx.base, patches_dir):
            raise ReleaseError("the series applied, but the branch still differs from patches/")
    (ctx.root / builder.APPLY_STAMP).write_text(_series(ctx) + "\n", encoding="utf-8")
    return {"series": _series(ctx)}


def build_inputs(ctx: Context, state: release_state.State) -> dict:
    return {"commit": tag_commit(ctx), "args.gn": render_args(ctx.identity, ctx.update_url),
            "series": state.outputs("apply")["series"], "targets": " ".join(build_targets())}


def build_perform(ctx: Context, state: release_state.State, run: Runner) -> dict:
    args = ctx.out / "args.gn"
    wanted = render_args(ctx.identity, ctx.update_url)
    if not args.exists() or args.read_text(encoding="utf-8") != wanted:
        args.parent.mkdir(parents=True, exist_ok=True)
        args.write_text(wanted, encoding="utf-8", newline="\n")
    try:
        run([ctx.python, str(ctx.webops / "tools" / "release_version.py"), "write", "--src",
             str(ctx.src), "--tag", ctx.tag])
        for argv in build_commands(ctx):
            run(argv, cwd=ctx.src)
    finally:
        _git(ctx.src, "checkout", "--", "chrome/VERSION")
    return {name: provenance.sha256_file(ctx.out / name)
            for name in ("mini_installer.exe", "UpdaterSetup.exe")}


def test_perform(ctx: Context, state: release_state.State, run: Runner) -> dict:
    results = ctx.dir / "results"
    results.mkdir(parents=True, exist_ok=True)
    for argv in test_commands(ctx, results):
        run(argv, cwd=ctx.src)
    return {"ghost_unittests": OUT.as_posix(), "ghost_browsertests": BROWSERTESTS_OUT.as_posix()}


def sign_perform(ctx: Context, state: release_state.State, run: Runner) -> dict:
    signed = ctx.dir / SIGNED
    if signed.exists():
        shutil.rmtree(signed)  # this stage's own output, from an earlier run
    run(sign_command(ctx, browser_appid(ctx.webops)), cwd=ctx.src)
    return {name: provenance.sha256_file(signed / name)
            for name in ("mini_installer.exe", "update.crx3", offline_installer.OUTPUT_NAME)}


def toolchain(ctx: Context) -> dict[str, str]:
    import check_env
    versions = {"clang": (ctx.src / "third_party" / "llvm-build" / "Release+Asserts"
                          / "cr_build_revision").read_text(encoding="utf-8").strip()}
    sdks = check_env.probe_sdk_include_versions()
    if sdks:
        versions["windows-sdk"] = max(sdks, key=repo.parse_version)
    _, _, satisfying = check_env.probe_vs(repo.load_requirements(ctx.webops)["windows"]
                                          ["visual_studio"])
    if satisfying:
        versions["visual-studio"] = satisfying[0].version
    return versions


def describe_perform(ctx: Context, state: release_state.State, run: Runner) -> dict:
    publish = ctx.dir / PUBLISH
    if publish.exists():
        shutil.rmtree(publish)  # this stage's own output, from an earlier run
    publish.mkdir(parents=True)
    for name in (offline_installer.OUTPUT_NAME, "update.crx3"):
        shutil.copy2(ctx.dir / SIGNED / name, publish / name)
    sbom.build(_tool(ctx.depot_tools, "vpython3"), ctx.src, ctx.out, ctx.tag, tag_commit(ctx),
               publish / sbom.DOCUMENT, env=_env(ctx))
    build = state.stages["build"]
    facts = provenance.BuildFacts(
        tag=ctx.tag, webops_commit=tag_commit(ctx), chromium_commit=_pin(ctx),
        series_digest=state.outputs("apply")["series"],
        depot_tools_commit=_git(ctx.depot_tools, "rev-parse", "HEAD"), toolchain=toolchain(ctx),
        args_gn=render_args(ctx.identity, ctx.update_url), identity=ctx.identity,
        tests=state.outputs("test"), started=datetime.datetime.fromisoformat(build["started"]),
        finished=datetime.datetime.fromisoformat(build["finished"]))
    provenance.write_release_files(
        publish, [offline_installer.OUTPUT_NAME, "update.crx3", sbom.DOCUMENT], facts)
    return provenance.parse_sums((publish / provenance.SUMS_FILE).read_text(encoding="utf-8"))


def _gh(ctx: Context, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run(["gh", *args], cwd=ctx.webops, capture_output=True)


def draft_perform(ctx: Context, state: release_state.State, run: Runner) -> dict:
    publish = ctx.dir / PUBLISH
    notes = ctx.dir / "notes.md"
    notes.write_text(release_notes(ctx), encoding="utf-8", newline="\n")
    ours = (publish / provenance.SUMS_FILE).read_bytes()
    existing = _gh(ctx, "release", "view", ctx.tag, "--json", "assets")
    if existing.returncode == 0:
        assets = {a["name"]: a for a in json.loads(existing.stdout)["assets"]}
        sums = assets.get(provenance.SUMS_FILE)
        theirs = _gh(ctx, "api", "-H", "Accept: application/octet-stream",
                     sums["apiUrl"]).stdout if sums else b""
        if set(assets) != set(PUBLISHED) or theirs != ours:
            raise ReleaseError(f"GitHub already has a release {ctx.tag} with other files; "
                               "compare it with this one before changing either")
    else:
        run(draft_command(ctx, publish, notes), cwd=ctx.webops)
    view = _gh(ctx, "release", "view", ctx.tag, "--json", "url")
    return {"url": json.loads(view.stdout)["url"] if view.returncode == 0 else ""}


def stage_perform(ctx: Context, state: release_state.State, run: Runner) -> dict:
    run(stage_command(ctx, ctx.dir / PUBLISH / "update.crx3", browser_appid(ctx.webops)),
        cwd=ctx.server_repo)
    return {"fraction": str(ctx.fraction), "host": ctx.host}


STAGES = (
    Stage("sync", lambda ctx, state: {"pin": _pin(ctx)}, sync_perform),
    Stage("apply", lambda ctx, state: {"series": _series(ctx), "pin": _pin(ctx)},
          apply_perform),
    Stage("build", build_inputs, build_perform),
    Stage("test", lambda ctx, state: dict(state.outputs("build")), test_perform),
    Stage("sign", lambda ctx, state: {**state.outputs("build"), "identity": ctx.identity},
          sign_perform),
    Stage("describe", lambda ctx, state: {**state.outputs("sign"),
                                          **{f"test {k}": v for k, v
                                             in state.outputs("test").items()}},
          describe_perform),
    Stage("draft", lambda ctx, state: {**state.outputs("describe"), "public": str(ctx.public)},
          draft_perform),
    Stage("stage", lambda ctx, state: {"update.crx3": state.outputs("sign")["update.crx3"],
                                       "fraction": str(ctx.fraction), "host": str(ctx.host)},
          stage_perform, wanted=lambda ctx: ctx.fraction is not None),
)


def _now() -> datetime.datetime:
    return datetime.datetime.now(datetime.timezone.utc)


def run_stages(ctx: Context, state: release_state.State, run: Runner,
               stages=STAGES, redo: str | None = None, now=_now) -> None:
    for stage in stages:
        if not stage.wanted(ctx):
            continue
        if stage.name == redo:
            state.forget(stage.name)
        inputs = stage.inputs(ctx, state)
        if state.is_done(stage.name, inputs):
            print(f"[{stage.name}] done before; skipped", flush=True)
            continue
        print(f"[{stage.name}]", flush=True)
        started = now()
        outputs = stage.perform(ctx, state, run)
        state.record(stage.name, inputs, outputs, started, now())
```

- [ ] **Step 4: Run the tests.** Expected: `OK` (30 tests).

- [ ] **Step 5: Commit.**

```bash
cd /c/Users/robyv/Desktop/DLU/webops && git add tools/release.py tools/tests/test_release.py && python tools/lint.py && git commit -q -m "tools: the release stages, recorded and resumable"
```

---

### Task 9: `release.py verify`

**Files:**
- Modify: `tools/release.py`
- Test: `tools/tests/test_release.py`

- [ ] **Step 1: Write the failing tests.** Add `import tempfile`, `import crx3`, `import provenance`, `import signing`, `import update_server` to the test module's imports, then append:

```python
FACTS = provenance.BuildFacts(
    tag=TAG, webops_commit="a" * 40, chromium_commit="b" * 40, series_digest="c" * 64,
    depot_tools_commit="d" * 40, toolchain={"clang": "x"}, args_gn="", identity="dev",
    tests={}, started=datetime.datetime(2026, 10, 5, tzinfo=datetime.timezone.utc),
    finished=datetime.datetime(2026, 10, 6, tzinfo=datetime.timezone.utc))


class VerifyTest(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.dir = Path(tmp.name)
        developer = signing.file_signer(update_server.CRX_KEY_FILE)
        publisher = signing.file_signer(update_server.CRX_BACKUP_KEY_FILE)  # pinned for dev
        (self.dir / "update.crx3").write_bytes(
            crx3.build({"mini_installer.exe": b"installer"}, developer, [publisher]))
        provenance.write_release_files(self.dir, ["update.crx3"], FACTS)

    def test_a_good_development_release(self):
        self.assertEqual(release.verify(self.dir, "dev"), [])

    def test_a_proof_by_another_key_fails(self):
        developer = signing.file_signer(update_server.CRX_KEY_FILE)
        other = signing.scalar_signer("other", update_server.OTHER_KEY)
        (self.dir / "update.crx3").write_bytes(
            crx3.build({"mini_installer.exe": b"installer"}, developer, [other]))
        provenance.write_release_files(self.dir, ["update.crx3"], FACTS)
        self.assertEqual(release.verify(self.dir, "dev"),
                         ["update.crx3: no valid proof by the dev identity's publisher keys"])

    def test_hash_failures_are_reported(self):
        (self.dir / "update.crx3").write_bytes(b"changed")
        self.assertIn("update.crx3: its SHA-256 differs from SHA256SUMS",
                      release.verify(self.dir, "dev"))
```

- [ ] **Step 2: Run them.** Expected: ERROR, `module 'release' has no attribute 'verify'`.

- [ ] **Step 3: Implement.** Add to `tools/release.py` (imports `hashlib`, `crx3`, `signing`):

```python
def verify(directory: Path, identity: str) -> list[str]:
    """What anyone with a release's files can check: hashes, provenance, signatures."""
    failures = provenance.check_files(directory)
    crx = directory / "update.crx3"
    if crx.is_file():
        pinned = signing.pinned_keys(identity).publisher_hashes
        try:
            keys = crx3.verified_keys(crx.read_bytes())
        except (ValueError, IndexError):
            keys = []
        if not any(hashlib.sha256(key).digest() in pinned for key in keys):
            failures.append(f"update.crx3: no valid proof by the {identity} identity's "
                            "publisher keys")
    setup = directory / offline_installer.OUTPUT_NAME
    if setup.is_file() and identity != "dev":
        if os.name != "nt":
            failures.append(f"{setup.name}: Authenticode can only be checked on Windows")
        else:
            import authenticode
            certificate = (repo.REPO_ROOT / "branding" / "signing"
                           / f"{identity}_codesign.cer").read_bytes()
            failures += authenticode.verify([setup], authenticode.thumbprint(certificate),
                                            require_trusted=False)
    return failures
```

- [ ] **Step 4: Run the tests.** Expected: `OK` (33 tests).

- [ ] **Step 5: Commit.**

```bash
cd /c/Users/robyv/Desktop/DLU/webops && git add tools/release.py tools/tests/test_release.py && python tools/lint.py && git commit -q -m "tools: release.py verify"
```

---

### Task 10: `release.py`'s command line

**Files:**
- Modify: `tools/release.py`
- Test: `tools/tests/test_release.py`

- [ ] **Step 1: Write the failing tests.** Append (and `import contextlib`, `import io` at the top):

```python
class MainTest(unittest.TestCase):
    def main(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = release.main(list(argv))
        return code, out.getvalue() + err.getvalue()

    def test_public_is_refused_with_the_test_identity(self):
        code, text = self.main("run", "--tag", TAG, "--src", str(SRC), "--public")
        self.assertEqual(code, 2)
        self.assertIn("the test identity never makes a public release", text)

    def test_stage_needs_a_host(self):
        code, text = self.main("run", "--tag", TAG, "--src", str(SRC), "--stage", "0.01")
        self.assertEqual(code, 2)
        self.assertIn("--stage and --host go together", text)

    def test_a_fraction_is_between_zero_and_one(self):
        code, text = self.main("rollout", "--host", "h", "--fraction", "1.5")
        self.assertEqual(code, 2)
        self.assertIn("between 0 and 1", text)

    def test_rollout_commands_go_to_the_server(self):
        calls = []
        with unittest.mock.patch.object(release.subprocess, "run",
                                        lambda argv, **kw: calls.append(argv)
                                        or subprocess.CompletedProcess(argv, 0)):
            self.assertEqual(self.main("halt", "--host", "ghost@h")[0], 0)
        self.assertEqual(calls, [release.admin_command("ghost@h", "halt",
                                                       release.browser_appid())])
```

(and `import unittest.mock` at the top).

- [ ] **Step 2: Run them.** Expected: ERROR, `module 'release' has no attribute 'main'`.

- [ ] **Step 3: Implement.** Add to the end of `tools/release.py` (import `argparse`):

```python
def _fraction(text: str) -> float:
    value = float(text)
    if not 0 <= value <= 1:
        raise argparse.ArgumentTypeError("a fraction is between 0 and 1")
    return value


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    r = sub.add_parser("run", help="make a release from a tag")
    r.add_argument("--tag", required=True)
    r.add_argument("--src", type=Path, required=True, help="the Chromium checkout's src")
    r.add_argument("--identity", choices=("dev", "test"), default="test")
    r.add_argument("--update-url", help="the update server's URL the build uses")
    r.add_argument("--redo", choices=[s.name for s in STAGES])
    r.add_argument("--stage", type=_fraction, metavar="FRACTION",
                   help="offer the release to this fraction of update checks")
    r.add_argument("--host", help="USER@HOST of the update server")
    r.add_argument("--public", action="store_true", help="a public release, not a draft")
    r.add_argument("--releases", type=Path, default=DEFAULT_RELEASES)
    r.add_argument("--depot-tools", type=Path, default=DEFAULT_DEPOT_TOOLS)
    r.add_argument("--server-repo", type=Path, default=DEFAULT_SERVER_REPO)
    r.add_argument("--jobs", type=int, default=10)
    v = sub.add_parser("verify", help="check a release's files")
    v.add_argument("directory", type=Path)
    v.add_argument("--identity", choices=("dev", "test"), default="test")
    for action in ("rollout", "halt", "promote", "drop"):
        a = sub.add_parser(action, help=f"{action} the update server's candidate")
        a.add_argument("--host", required=True, help="USER@HOST of the update server")
        a.add_argument("--appid", default=None, help="default: branding/updater.gni")
        if action == "rollout":
            a.add_argument("--fraction", type=_fraction, required=True)
    try:
        args = parser.parse_args(argv)
    except SystemExit as e:
        return int(e.code or 0)

    if args.command == "verify":
        failures = verify(args.directory, args.identity)
        for failure in failures:
            print(f"FAILED  {failure}")
        print("verified" if not failures else f"{len(failures)} failure(s)")
        return 1 if failures else 0
    if args.command != "run":
        appid = args.appid or browser_appid()
        action = "set-fraction" if args.command == "rollout" else args.command
        command = admin_command(args.host, action, appid, getattr(args, "fraction", None))
        return 0 if subprocess.run(command).returncode == 0 else 1

    if args.public and args.identity in NOT_PUBLIC:
        print(f"release: the {args.identity} identity never makes a public release "
              "(docs/licensing.md#release-gates)", file=sys.stderr)
        return 2
    if (args.stage is None) != (args.host is None):
        print("release: --stage and --host go together", file=sys.stderr)
        return 2
    ctx = Context(tag=args.tag, src=args.src.resolve(), identity=args.identity,
                  update_url=args.update_url, depot_tools=args.depot_tools,
                  releases=args.releases, server_repo=args.server_repo, host=args.host,
                  fraction=args.stage, public=args.public, jobs=args.jobs)
    problems = check(ctx)
    for problem in problems:
        print(f"release: {problem}", file=sys.stderr)
    if problems:
        return 1
    try:
        state = release_state.State.load(ctx.dir / "state.json", ctx.tag)
        run_stages(ctx, state, runner_for(ctx), redo=args.redo)
    except (subprocess.CalledProcessError, ReleaseError, release_state.StateError,
            OSError) as e:
        print(f"release: {e}", file=sys.stderr)
        return 1
    print(f"release {ctx.tag} ({ctx.version}) done; its files are in {ctx.dir / PUBLISH}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

argparse prints its own message for a fraction out of range ("a fraction is between 0 and 1") and exits 2, which `main` returns.

- [ ] **Step 4: Run the whole suite and lint.**

Run: `cd /c/Users/robyv/Desktop/DLU/webops && python -m unittest discover -s tools/tests -t tools 2>&1 | tail -3 && git add tools && python tools/lint.py`
Expected: `OK`, `lint: 0 problem(s)`.

- [ ] **Step 5: A dry look at the real checkout** (nothing is built): `python tools/release.py run --tag 152.0.7977.149-1 --src chromium/src` must stop at the checks, listing at least "there is no tag 152.0.7977.149-1" (the tag comes in Task 18).

- [ ] **Step 6: Commit.**

```bash
cd /c/Users/robyv/Desktop/DLU/webops && git commit -q -m "tools: release.py's command line" && git log --oneline -1
```

---
### Task 11: The server offers a candidate to a fraction of checks

**Files (in `SRV`):**
- Modify: `ghost_update/protocol.py`, `ghost_update/service.py`
- Test: `tests/test_protocol.py`

- [ ] **Step 1: Write the failing tests.** In `tests/test_protocol.py`, replace `RELEASES = {BROWSER_APPID: RELEASE, UPDATER_APPID: None}` with:

```python
RELEASES = {BROWSER_APPID: protocol.Offer(RELEASE), UPDATER_APPID: protocol.Offer(None)}
CANDIDATE = protocol.Release("152.0.7977.14903", "browser-152.0.7977.14903.crx3", 2000,
                             "cd" * 32, "mini_installer.exe", "")
```

and append, before `if __name__ == "__main__":`:

```python
def offered(offer: protocol.Offer, version: str, draw: float) -> str:
    body = json.dumps({"request": {"protocol": "4.0", "apps": [
        {"appid": BROWSER_APPID, "version": version, "updatecheck": {}}]}}).encode()
    payload = protocol.respond(body, {BROWSER_APPID: offer}, BASE, TODAY, draw=lambda: draw)
    check = json.loads(payload[5:])["response"]["apps"][0]["updatecheck"]
    return check.get("nextversion", check["status"])


class CandidateTest(unittest.TestCase):
    def test_a_check_under_the_fraction_gets_the_candidate(self):
        offer = protocol.Offer(RELEASE, CANDIDATE, 0.05)
        self.assertEqual(offered(offer, "152.0.7977.14901", 0.04), "152.0.7977.14903")
        self.assertEqual(offered(offer, "152.0.7977.14901", 0.05), "152.0.7977.14902")

    def test_fraction_zero_offers_the_candidate_to_no_one(self):
        offer = protocol.Offer(RELEASE, CANDIDATE, 0.0)
        self.assertEqual(offered(offer, "152.0.7977.14901", 0.0), "152.0.7977.14902")

    def test_fraction_one_offers_it_to_every_check(self):
        offer = protocol.Offer(RELEASE, CANDIDATE, 1.0)
        self.assertEqual(offered(offer, "152.0.7977.14901", 0.999999), "152.0.7977.14903")

    def test_a_client_on_the_candidate_is_never_sent_back(self):
        offer = protocol.Offer(RELEASE, CANDIDATE, 0.0)
        self.assertEqual(offered(offer, "152.0.7977.14903", 0.5), "noupdate")

    def test_a_candidate_without_an_active_release(self):
        offer = protocol.Offer(None, CANDIDATE, 0.0)
        self.assertEqual(offered(offer, "152.0.7977.14901", 0.5), "noupdate")

    def test_events_draw_nothing(self):
        draws = []
        body = json.dumps({"request": {"protocol": "4.0", "apps": [
            {"appid": BROWSER_APPID, "version": "1.0.0.0", "event": []}]}}).encode()
        protocol.respond(body, RELEASES, BASE, TODAY, draw=lambda: draws.append(1) or 0.0)
        self.assertEqual(draws, [])
```

- [ ] **Step 2: Run them.**

Run: `cd /c/Users/robyv/Desktop/DLU/project-ghost-update-server && python -m unittest discover -s tests -t . -p test_protocol.py 2>&1 | tail -3`
Expected: ERROR, `module 'ghost_update.protocol' has no attribute 'Offer'`.

- [ ] **Step 3: Implement.** In `ghost_update/protocol.py`: add `import random` and `from collections.abc import Callable`; after `Release`, add:

```python
@dataclass(frozen=True)
class Offer:
    """An app's releases: the active one, and a candidate offered to a fraction of checks.

    Requests carry no identifier, so the fraction applies to each check, not
    to a stable group of clients: at five checks a day, 0.01 reaches about 5 %
    of clients a day. 0 halts the candidate."""
    active: Release | None
    candidate: Release | None = None
    fraction: float = 0.0

    def choose(self, draw: Callable[[], float]) -> Release | None:
        if self.candidate is not None and draw() < self.fraction:
            return self.candidate
        return self.active


_RANDOM = random.SystemRandom()
```

Replace `answer` and `respond` with:

```python
def answer(app: dict, offers: dict[str, Offer], download_base: str,
           draw: Callable[[], float]) -> dict:
    """One app's answer. `offers` maps lowercase app IDs to their releases."""
    appid = app["appid"]
    if appid.lower() not in offers:
        return {"appid": appid, "status": "error-unknownApplication"}
    entry: dict = {"appid": appid, "status": "ok"}
    if "updatecheck" not in app:
        return entry  # an event or a ping: acknowledged, nothing recorded
    release = offers[appid.lower()].choose(draw)
    installed = parse_version(app.get("version") or NULL_VERSION)
    if release is None or installed >= parse_version(release.version):
        entry["updatecheck"] = {"status": "noupdate"}
        return entry
    entry["updatecheck"] = {
        "status": "ok", "nextversion": release.version,
        "pipelines": [{"pipeline_id": "full", "operations": [
            {"type": "download", "size": release.size, "out": {"sha256": release.sha256},
             "urls": [{"url": f"{download_base}/{release.file}"}]},
            {"type": "crx3", "in": {"sha256": release.sha256}, "path": release.installer,
             "arguments": release.arguments}]}]}
    return entry


def respond(body: bytes, offers: dict[str, Offer], download_base: str,
            today: datetime.date, draw: Callable[[], float] = _RANDOM.random) -> bytes:
    """The response body for a request body. Raises InvalidRequest. Nothing about
    the choice is recorded."""
    apps = [answer(app, offers, download_base, draw) for app in parse_request(body)]
    response = {"response": {"protocol": "4.0", "server": "ghost",
                             "daystart": {"elapsed_days": (today - _DAY_ZERO).days},
                             "apps": apps}}
    return (RESPONSE_PREFIX + json.dumps(response, separators=(",", ":"))).encode()
```

Update the module docstring's last line to: `No I/O: the service passes in the request body and each app's offer.` In `ghost_update/service.py`, `do_POST`: replace `self.server.store.current().releases()` with `self.server.store.current().offers()` (the manifest gains `offers()` in Task 12; until then the service tests fail, which Task 12 fixes before committing).

- [ ] **Step 4: Run the protocol tests.** Same command as Step 2. Expected: `OK`.

Don't commit yet: Task 12 completes the change (the service needs `offers()`).

---

### Task 12: `releases.json` holds the candidate

**Files (in `SRV`):**
- Modify: `ghost_update/manifest.py`
- Test: `tests/test_manifest.py`

- [ ] **Step 1: Write the failing tests.** Append to `tests/test_manifest.py`, before `if __name__ == "__main__":` (and `from ghost_update.protocol import Offer, Release` replaces the `Release` import):

```python
class CandidateTest(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.dir)
        self.active = release("152.0.7977.14902", self.dir)
        self.next = release("152.0.7977.14903", self.dir)

    def test_round_trip_and_offers(self):
        original = manifest.Manifest({
            BROWSER_APPID: manifest.AppEntry(self.active, (), manifest.Candidate(self.next, 0.05)),
            UPDATER_APPID: manifest.AppEntry(None, ())})
        manifest.write(original, self.dir)
        parsed = manifest.parse((self.dir / manifest.FILE_NAME).read_bytes(), self.dir)
        self.assertEqual(parsed, original)
        self.assertEqual(parsed.offers(), {BROWSER_APPID: Offer(self.active, self.next, 0.05),
                                           UPDATER_APPID: Offer(None)})
        doc = json.loads((self.dir / manifest.FILE_NAME).read_text())
        self.assertEqual(doc["apps"][BROWSER_APPID]["candidate"]["fraction"], 0.05)
        self.assertIsNone(doc["apps"][UPDATER_APPID]["candidate"])

    def test_a_file_without_candidates_still_parses(self):
        data = json.dumps({"apps": {BROWSER_APPID: {"active": None, "previous": []}}}).encode()
        self.assertIsNone(manifest.parse(data, self.dir).apps[BROWSER_APPID].candidate)

    def test_invalid_candidates(self):
        good = {"version": "152.0.7977.14903", "file": self.next.file, "size": 10,
                "sha256": "ab" * 32, "installer": "mini_installer.exe",
                "arguments": "--do-not-launch-chrome"}
        active = {**good, "version": "152.0.7977.14902", "file": self.active.file}
        for name, candidate in (("no fraction", good), ("fraction 2", {**good, "fraction": 2}),
                                ("fraction -0.1", {**good, "fraction": -0.1}),
                                ("fraction true", {**good, "fraction": True}),
                                ("not newer", {**active, "fraction": 0.5})):
            with self.subTest(name):
                data = json.dumps({"apps": {BROWSER_APPID: {
                    "active": active, "previous": [], "candidate": candidate}}}).encode()
                with self.assertRaises(manifest.ManifestError):
                    manifest.parse(data, self.dir)
```

- [ ] **Step 2: Run them.**

Run: `cd /c/Users/robyv/Desktop/DLU/project-ghost-update-server && python -m unittest discover -s tests -t . -p test_manifest.py 2>&1 | tail -3`
Expected: ERROR, `module 'ghost_update.manifest' has no attribute 'Candidate'`.

- [ ] **Step 3: Implement.** In `ghost_update/manifest.py`:
  - the import becomes `from ghost_update.protocol import InvalidRequest, Offer, Release, parse_version`;
  - the docstring's format line becomes:

```
    {"apps": {"{app id}": {"active": <release> | null, "previous": [<release>, ...],
                           "candidate": <release with "fraction"> | null}}}
```

    and add a paragraph: `The candidate, when there is one, is newer than the active release and is offered to its fraction of update checks (sub-project E). Files written before it existed have no "candidate".`
  - replace `AppEntry` and `Manifest` with:

```python
@dataclass(frozen=True)
class Candidate:
    release: Release
    fraction: float


@dataclass(frozen=True)
class AppEntry:
    active: Release | None
    previous: tuple[Release, ...]
    candidate: Candidate | None = None


@dataclass(frozen=True)
class Manifest:
    apps: dict[str, AppEntry]  # lowercase app ID -> entry

    def releases(self) -> dict[str, Release | None]:
        return {appid: entry.active for appid, entry in self.apps.items()}

    def offers(self) -> dict[str, Offer]:
        return {appid: Offer(entry.active, entry.candidate.release, entry.candidate.fraction)
                if entry.candidate else Offer(entry.active)
                for appid, entry in self.apps.items()}
```

  - add after `_release`:

```python
def _candidate(value: object, releases_dir: Path, active: Release | None) -> Candidate:
    if not isinstance(value, dict) or "fraction" not in value:
        raise ManifestError("a candidate is a release with a fraction")
    fraction = value["fraction"]
    if isinstance(fraction, bool) or not isinstance(fraction, (int, float)) \
            or not 0 <= fraction <= 1:
        raise ManifestError(f"bad fraction {fraction!r}: from 0 to 1")
    release = _release({k: v for k, v in value.items() if k != "fraction"}, releases_dir)
    if active and parse_version(release.version) <= parse_version(active.version):
        raise ManifestError(f"the candidate {release.version} is not newer than the active "
                            f"{active.version}")
    return Candidate(release, float(fraction))
```

  - in `parse`, replace the entry check and construction:

```python
        if not isinstance(entry, dict) or not {"active", "previous"} <= set(entry) \
                or not set(entry) <= {"active", "previous", "candidate"}:
            raise ManifestError(f"{appid}: needs active and previous, and may have candidate")
        previous = entry["previous"]
        if not isinstance(previous, list) or len(previous) > KEPT - 1:
            raise ManifestError(f"{appid}: previous is a list of at most {KEPT - 1}")
        active = None if entry["active"] is None else _release(entry["active"], releases_dir)
        candidate = (None if entry.get("candidate") is None
                     else _candidate(entry["candidate"], releases_dir, active))
        out[appid] = AppEntry(active, tuple(_release(p, releases_dir) for p in previous),
                              candidate)
```

  - replace `serialize`:

```python
def serialize(manifest: Manifest) -> bytes:
    def candidate(entry: AppEntry) -> dict | None:
        if entry.candidate is None:
            return None
        return {**asdict(entry.candidate.release), "fraction": entry.candidate.fraction}

    doc = {"apps": {appid: {"active": asdict(entry.active) if entry.active else None,
                            "previous": [asdict(p) for p in entry.previous],
                            "candidate": candidate(entry)}
                    for appid, entry in sorted(manifest.apps.items())}}
    return (json.dumps(doc, indent=2) + "\n").encode()
```

- [ ] **Step 4: Run all of `SRV`'s tests and lint.**

Run: `cd /c/Users/robyv/Desktop/DLU/project-ghost-update-server && python -m unittest discover -s tests -t . 2>&1 | tail -3 && python tools/lint.py`
Expected: `OK`, lint clean. The service now calls `offers()`.

- [ ] **Step 5: Commit both tasks.**

```bash
cd /c/Users/robyv/Desktop/DLU/project-ghost-update-server && git add ghost_update tests && git commit -q -s -m "protocol: a candidate release, offered to a fraction of update checks" && git log --oneline -1
```

---

### Task 13: `ghost-update-admin` moves the candidate

**Files (in `SRV`):**
- Modify: `ghost_update/admin.py`
- Test: `tests/test_admin.py`

- [ ] **Step 1: Write the failing tests.** Append to `tests/test_admin.py`'s `AdminTest` class:

```python
    def admin(self, *args: str) -> tuple[int, str]:
        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
            try:
                code = admin.main(["--releases-dir", str(self.dir), *args])
            except SystemExit as e:  # argparse refusing an argument
                code = e.code
        return code, out.getvalue()

    def stage(self, version: str, fraction: str = "0.01", content: bytes = b"candidate") -> int:
        staged = self.dir / "staging" / f"browser-{version}.crx3"
        staged.write_bytes(content)
        return self.admin("stage", "--staged", str(staged), "--appid", BROWSER_APPID,
                          "--version", version, "--fraction", fraction)[0]

    def test_stage_makes_a_candidate_beside_the_active_release(self):
        self.assertEqual(self.activate("152.0.7977.14902"), 0)
        self.assertEqual(self.stage("152.0.7977.14903"), 0)
        entry = self.current().apps[BROWSER_APPID]
        self.assertEqual(entry.active.version, "152.0.7977.14902")
        self.assertEqual((entry.candidate.release.version, entry.candidate.fraction),
                         ("152.0.7977.14903", 0.01))
        self.assertEqual(entry.candidate.release.sha256, hashlib.sha256(b"candidate").hexdigest())

    def test_one_candidate_at_a_time(self):
        self.assertEqual(self.stage("152.0.7977.14903"), 0)
        code, text = self.admin("stage", "--staged", "x", "--appid", BROWSER_APPID,
                                "--version", "152.0.7977.14904", "--fraction", "0")
        self.assertEqual(code, 1)
        self.assertIn("a candidate exists", text)

    def test_a_candidate_must_be_newer(self):
        self.assertEqual(self.activate("152.0.7977.14903"), 0)
        self.assertEqual(self.stage("152.0.7977.14902"), 1)

    def test_set_fraction_and_halt(self):
        self.stage("152.0.7977.14903")
        self.assertEqual(self.admin("set-fraction", "--appid", BROWSER_APPID,
                                    "--fraction", "0.25")[0], 0)
        self.assertEqual(self.current().apps[BROWSER_APPID].candidate.fraction, 0.25)
        code, text = self.admin("halt", "--appid", BROWSER_APPID)
        self.assertEqual(code, 0)
        self.assertIn("halted", text)
        self.assertEqual(self.current().apps[BROWSER_APPID].candidate.fraction, 0.0)

    def test_a_fraction_out_of_range_is_refused(self):
        self.stage("152.0.7977.14903")
        self.assertEqual(self.admin("set-fraction", "--appid", BROWSER_APPID,
                                    "--fraction", "1.5")[0], 2)

    def test_promote(self):
        for version in ("152.0.7977.14901", "152.0.7977.14902", "152.0.7977.14903"):
            self.assertEqual(self.activate(version, content=version.encode()), 0)
        self.stage("152.0.7977.14904")
        self.assertEqual(self.admin("promote", "--appid", BROWSER_APPID)[0], 0)
        entry = self.current().apps[BROWSER_APPID]
        self.assertEqual(entry.active.version, "152.0.7977.14904")
        self.assertEqual([p.version for p in entry.previous],
                         ["152.0.7977.14903", "152.0.7977.14902"])
        self.assertIsNone(entry.candidate)
        names = sorted(p.name for p in self.dir.glob("*.crx3"))
        self.assertEqual(len(names), 3)
        self.assertFalse(any("14901" in name for name in names))

    def test_drop(self):
        self.stage("152.0.7977.14903")
        name = self.current().apps[BROWSER_APPID].candidate.release.file
        self.assertEqual(self.admin("drop", "--appid", BROWSER_APPID)[0], 0)
        self.assertIsNone(self.current().apps[BROWSER_APPID].candidate)
        self.assertFalse((self.dir / name).exists())

    def test_commands_without_a_candidate_are_refused(self):
        for command in (["promote"], ["drop"], ["halt"], ["set-fraction", "--fraction", "0.5"]):
            with self.subTest(command[0]):
                code, text = self.admin(command[0], "--appid", BROWSER_APPID, *command[1:])
                self.assertEqual(code, 1)
                self.assertIn("no candidate", text)

    def test_activate_is_refused_while_a_candidate_exists(self):
        self.stage("152.0.7977.14903")
        self.assertEqual(self.activate("152.0.7977.14904"), 1)

    def test_list_shows_the_candidate(self):
        self.stage("152.0.7977.14903", fraction="0.05")
        code, text = self.admin("list")
        self.assertIn("candidate 152.0.7977.14903 at 5%", text)
```

(The existing `activate` helper takes `content`; it already does.)

- [ ] **Step 2: Run them.**

Run: `cd /c/Users/robyv/Desktop/DLU/project-ghost-update-server && python -m unittest discover -s tests -t . -p test_admin.py 2>&1 | tail -3`
Expected: FAIL/ERROR: `invalid choice: 'stage'` exits 2 and the assertions fail.

- [ ] **Step 3: Implement.** In `ghost_update/admin.py`:
  - the docstring's command list gains:

```
  stage --staged F --appid A --version V --fraction P [--installer I] [--arguments ARGS]
                             make a staged package the app's candidate, offered to
                             fraction P of update checks
  set-fraction --appid A --fraction P
                             the candidate's fraction
  halt --appid A             fraction 0: the candidate goes to no new client
  promote --appid A          the candidate becomes the active release
  drop --appid A             forget the candidate and delete its package
```

  - replace `activate` with these functions:

```python
def _load(releases_dir: Path) -> manifest.Manifest:
    return manifest.parse((releases_dir / manifest.FILE_NAME).read_bytes(), releases_dir)


def _save(releases_dir: Path, updated: manifest.Manifest) -> None:
    manifest.parse(manifest.serialize(updated), releases_dir)  # validate before writing
    manifest.write(updated, releases_dir)


def _place(releases_dir: Path, staged: Path, appid: str, version: str, installer: str,
           arguments: str) -> Release:
    name = f"{appid.strip('{}')}-{version}.crx3"
    release = Release(version, name, staged.stat().st_size, _sha256(staged), installer,
                      arguments)
    os.replace(staged, releases_dir / name)
    return release


def _entry(current: manifest.Manifest, appid: str) -> manifest.AppEntry | None:
    return current.apps.get(appid.lower())


def _newer(version: str, entry: manifest.AppEntry) -> str | None:
    """Why the version can't follow the entry's active release, or None."""
    try:
        new = parse_version(version)
    except InvalidRequest:
        return "the version is not four dotted integers"
    if entry.active and new <= parse_version(entry.active.version):
        return f"{version} is not newer than the active {entry.active.version}"
    return None


def activate(releases_dir: Path, staged: Path, appid: str, version: str, installer: str,
             arguments: str) -> int:
    appid = appid.lower()
    current = _load(releases_dir)
    entry = _entry(current, appid)
    if entry is None:
        return _error(f"unknown app {appid}")
    if entry.candidate:
        return _error(f"a candidate exists ({entry.candidate.release.version}): promote or "
                      "drop it first")
    problem = _newer(version, entry)
    if problem:
        return _error(problem)
    release = _place(releases_dir, staged, appid, version, installer, arguments)
    kept = ((entry.active,) if entry.active else ()) + entry.previous
    _save(releases_dir, manifest.Manifest({**current.apps, appid: manifest.AppEntry(
        release, kept[:manifest.KEPT - 1])}))
    for old in kept[manifest.KEPT - 1:]:
        (releases_dir / old.file).unlink(missing_ok=True)
    print(f"{appid} {version} active")
    return 0


def stage(releases_dir: Path, staged: Path, appid: str, version: str, installer: str,
          arguments: str, fraction: float) -> int:
    appid = appid.lower()
    current = _load(releases_dir)
    entry = _entry(current, appid)
    if entry is None:
        return _error(f"unknown app {appid}")
    if entry.candidate:
        return _error(f"a candidate exists ({entry.candidate.release.version}): promote or "
                      "drop it first")
    problem = _newer(version, entry)
    if problem:
        return _error(problem)
    release = _place(releases_dir, staged, appid, version, installer, arguments)
    _save(releases_dir, manifest.Manifest({**current.apps, appid: manifest.AppEntry(
        entry.active, entry.previous, manifest.Candidate(release, fraction))}))
    print(f"{appid} {version} staged for {fraction:.0%} of update checks")
    return 0


def set_fraction(releases_dir: Path, appid: str, fraction: float, word: str = "set") -> int:
    appid = appid.lower()
    current = _load(releases_dir)
    entry = _entry(current, appid)
    if entry is None or entry.candidate is None:
        return _error(f"{appid} has no candidate")
    _save(releases_dir, manifest.Manifest({**current.apps, appid: manifest.AppEntry(
        entry.active, entry.previous, manifest.Candidate(entry.candidate.release, fraction))}))
    print(f"{appid} {entry.candidate.release.version} {word}: {fraction:.0%} of update checks")
    return 0


def promote(releases_dir: Path, appid: str) -> int:
    appid = appid.lower()
    current = _load(releases_dir)
    entry = _entry(current, appid)
    if entry is None or entry.candidate is None:
        return _error(f"{appid} has no candidate")
    kept = ((entry.active,) if entry.active else ()) + entry.previous
    _save(releases_dir, manifest.Manifest({**current.apps, appid: manifest.AppEntry(
        entry.candidate.release, kept[:manifest.KEPT - 1])}))
    for old in kept[manifest.KEPT - 1:]:
        (releases_dir / old.file).unlink(missing_ok=True)
    print(f"{appid} {entry.candidate.release.version} active")
    return 0


def drop(releases_dir: Path, appid: str) -> int:
    appid = appid.lower()
    current = _load(releases_dir)
    entry = _entry(current, appid)
    if entry is None or entry.candidate is None:
        return _error(f"{appid} has no candidate")
    _save(releases_dir, manifest.Manifest({**current.apps, appid: manifest.AppEntry(
        entry.active, entry.previous)}))
    (releases_dir / entry.candidate.release.file).unlink(missing_ok=True)
    print(f"{appid} {entry.candidate.release.version} dropped")
    return 0
```

  - `list_releases` prints the candidate:

```python
def list_releases(releases_dir: Path) -> int:
    current = _load(releases_dir)
    for appid, entry in sorted(current.apps.items()):
        active = entry.active.version if entry.active else "none"
        kept = ", ".join(p.version for p in entry.previous) or "none"
        candidate = (f"  candidate {entry.candidate.release.version} at "
                     f"{entry.candidate.fraction:.0%}" if entry.candidate else "")
        print(f"{appid}  active {active}  kept {kept}{candidate}")
    return 0
```

  - in `main`, add a fraction type and the new subcommands:

```python
def _fraction(text: str) -> float:
    value = float(text)
    if not 0 <= value <= 1:
        raise argparse.ArgumentTypeError("a fraction is from 0 to 1")
    return value
```

```python
    s = sub.add_parser("stage")
    s.add_argument("--staged", type=Path, required=True)
    s.add_argument("--appid", required=True)
    s.add_argument("--version", required=True)
    s.add_argument("--fraction", type=_fraction, required=True)
    s.add_argument("--installer", default=DEFAULT_INSTALLER)
    s.add_argument("--arguments", default=DEFAULT_ARGUMENTS)
    f = sub.add_parser("set-fraction")
    f.add_argument("--appid", required=True)
    f.add_argument("--fraction", type=_fraction, required=True)
    for name in ("halt", "promote", "drop"):
        sub.add_parser(name).add_argument("--appid", required=True)
```

    (rename the existing `find-address` parser variable from `f` to `fa`), and dispatch:

```python
    if args.command == "stage":
        return stage(args.releases_dir, args.staged, args.appid, args.version, args.installer,
                     args.arguments, args.fraction)
    if args.command == "set-fraction":
        return set_fraction(args.releases_dir, args.appid, args.fraction)
    if args.command == "halt":
        return set_fraction(args.releases_dir, args.appid, 0.0, word="halted")
    if args.command == "promote":
        return promote(args.releases_dir, args.appid)
    if args.command == "drop":
        return drop(args.releases_dir, args.appid)
```

`main` lets argparse exit 2 on a fraction out of range, which the test's `admin` helper turns into the return code.

- [ ] **Step 4: Run all of `SRV`'s tests and lint.** Expected: `OK`, lint clean.

- [ ] **Step 5: Commit.**

```bash
cd /c/Users/robyv/Desktop/DLU/project-ghost-update-server && git add ghost_update/admin.py tests/test_admin.py && git commit -q -s -m "admin: stage, set-fraction, halt, promote, drop"
```

---

### Task 14: The release CLI uploads a candidate

**Files (in `SRV`):**
- Modify: `ghost_update/release.py`, `README.md`
- Test: `tests/test_release.py`

`--fraction` uploads the package as the candidate. Without it, the package is activated, as before: C's remaining tasks (the first deployment) use that.

- [ ] **Step 1: Write the failing test.** Append to `CommandsTest`:

```python
    def test_with_a_fraction_the_package_becomes_the_candidate(self):
        staged = "/srv/releases/staging/c0ff4371-d9ab-461e-bffd-6b0dc2430b02-152.0.7977.14902.crx3"
        steps = release.commands(Path("update.crx3"), BROWSER_APPID, "152.0.7977.14902",
                                 "ghost@203.0.113.5", "mini_installer.exe", "", fraction=0.01)
        self.assertEqual(steps[1], ["ssh", "ghost@203.0.113.5",
                                    f"sudo ghost-update-admin stage --staged {staged} --appid "
                                    "'{c0ff4371-d9ab-461e-bffd-6b0dc2430b02}' --version "
                                    "152.0.7977.14902 --installer mini_installer.exe "
                                    "--arguments '' --fraction 0.01"])
```

- [ ] **Step 2: Run it.** Expected: `TypeError: commands() got an unexpected keyword argument 'fraction'`.

- [ ] **Step 3: Implement.** In `ghost_update/release.py`, `commands` gains `fraction: float | None = None`; the admin command becomes:

```python
    action = "activate" if fraction is None else "stage"
    admin_args = ["sudo", "ghost-update-admin", action, "--staged", staged, "--appid", appid,
                  "--version", version, "--installer", installer, "--arguments", arguments]
    if fraction is not None:
        admin_args += ["--fraction", str(fraction)]
    return [["scp", "-q", str(crx), f"{host}:{staged}"], ["ssh", host, shlex.join(admin_args)]]
```

`main` gains `parser.add_argument("--fraction", type=float, help="upload as the candidate, offered to this fraction of update checks")` and passes `args.fraction`. The docstring's usage line gains `[--fraction F]`. In `README.md`, after "Publishing a release", add:

```markdown
## Rolling a release out

With `--fraction F`, the release CLI uploads the package as the app's **candidate**, beside the active release. The server offers it to fraction F of update checks: requests carry no identifier, so each check draws on its own, and at about five checks a day F = 0.01 reaches about 5 % of clients a day. Then, on the server:

    sudo ghost-update-admin set-fraction --appid <app ID> --fraction 0.1
    sudo ghost-update-admin halt --appid <app ID>       # fraction 0: no new client gets it
    sudo ghost-update-admin promote --appid <app ID>    # it becomes the active release
    sudo ghost-update-admin drop --appid <app ID>       # forget it and delete its package

The browser's `tools/release.py rollout|halt|promote|drop --host <host>` runs these over SSH. A client is never offered a version older than its own, so a halted release stays on the clients that took it.
```

- [ ] **Step 4: Run all tests and lint.** Expected: `OK`, lint clean.

- [ ] **Step 5: Commit and ask to push `SRV`.** Commit, then ask the user whether to push; after a push, wait for both CI jobs to finish and check they pass.

```bash
cd /c/Users/robyv/Desktop/DLU/project-ghost-update-server && git add ghost_update/release.py tests/test_release.py README.md && git commit -q -s -m "release: upload as the candidate, with a fraction"
```

---

### Task 15: The server repository's service in the end-to-end test

**Files:**
- Create: `tools/candidate_server.py`
- Test: `tools/tests/test_candidate_server.py`

The end-to-end test runs the real service, so the rollout logic it checks is the server's own. `candidate_server.py` imports `ghost_update` from a copy of `SRV`, serves its packages as Caddy would, and records each request and each answer to the browser app.

- [ ] **Step 1: Write the failing tests** `tools/tests/test_candidate_server.py`. They need `SRV` beside `WEBOPS` (or `GHOST_UPDATE_SERVER` naming it) and are skipped elsewhere, as in CI:

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import json
import os
import tempfile
import unittest
import urllib.request
from pathlib import Path

import candidate_server
import repo
import update_server

SERVER_REPO = Path(os.environ.get("GHOST_UPDATE_SERVER",
                                  repo.REPO_ROOT.parent / "project-ghost-update-server"))
APPID = "{c0ff4371-d9ab-461e-bffd-6b0dc2430b02}"


@unittest.skipUnless((SERVER_REPO / "ghost_update" / "service.py").is_file(),
                     "needs project-ghost-update-server beside this repository")
class CandidateServerTest(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.tmp = Path(tmp.name)
        (self.tmp / "update.crx3").write_bytes(b"package")
        self.server = candidate_server.CandidateServer(
            SERVER_REPO, self.tmp / "releases", APPID, "152.0.7977.14902",
            self.tmp / "update.crx3", update_server.CUP_KEY_FILE, 1, self.tmp / "log.jsonl",
            port=0, file_port=0)
        self.addCleanup(self.server.shutdown)

    def check(self) -> dict:
        body = json.dumps({"request": {"protocol": "4.0", "apps": [
            {"appid": APPID, "version": "152.0.7977.14901", "updatecheck": {}}]}}).encode()
        url = f"http://127.0.0.1:{self.server.port}/update?cup2key=1:123"
        with urllib.request.urlopen(urllib.request.Request(url, body)) as r:
            payload = r.read()
        return json.loads(payload[5:])["response"]["apps"][0]["updatecheck"]

    def test_fraction_zero_offers_nothing(self):
        self.assertEqual(self.check(), {"status": "noupdate"})
        self.assertEqual(self.server.answers, ["noupdate"])

    def test_fraction_one_offers_and_serves_the_candidate(self):
        self.server.set_fraction(1.0)
        check = self.check()
        self.assertEqual(check["nextversion"], "152.0.7977.14902")
        url = check["pipelines"][0]["operations"][0]["urls"][0]["url"]
        with urllib.request.urlopen(url) as r:
            self.assertEqual(r.read(), b"package")
        self.assertEqual(self.server.answers, ["152.0.7977.14902"])

    def test_requests_are_logged_for_the_allow_list(self):
        self.check()
        [line] = (self.tmp / "log.jsonl").read_text().splitlines()
        self.assertEqual(json.loads(line)["body"]["request"]["apps"][0]["appid"], APPID)
```

- [ ] **Step 2: Run them.**

Run: `cd /c/Users/robyv/Desktop/DLU/webops && python -m unittest discover -s tools/tests -t tools -p test_candidate_server.py -v 2>&1 | tail -3`
Expected: ERROR, `No module named 'candidate_server'`.

- [ ] **Step 3: Implement** `tools/candidate_server.py`:

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""The update server repository's service on loopback, for the end-to-end test (sub-project E).

Runs ghost_update.service from a copy of project-ghost-update-server, with
one app's candidate release, and serves the packages under /releases/ as
Caddy does on the real server. It records every request body (for the
allow-list check) and every answer to the app's update checks.
"""

from __future__ import annotations

import functools
import hashlib
import http.server
import json
import os
import shutil
import sys
import threading
from pathlib import Path

ARGUMENTS = "--verbose-logging --do-not-launch-chrome"


class _Files(http.server.SimpleHTTPRequestHandler):
    def translate_path(self, path: str) -> str:
        prefix = "/releases/"
        if not path.startswith(prefix):
            return os.path.join(self.directory, "__not_served__")
        return super().translate_path("/" + path[len(prefix):])

    def log_message(self, format: str, *args) -> None:
        pass


class CandidateServer:
    def __init__(self, server_repo: Path, releases: Path, appid: str, version: str, crx: Path,
                 cup_key: Path, cup_version: int, log: Path, port: int = 8484,
                 file_port: int = 8485):
        if str(server_repo) not in sys.path:
            sys.path.insert(0, str(server_repo))
        from ghost_update import cup, identity, manifest, protocol, service
        self._manifest, self._protocol, self._identity = manifest, protocol, identity
        self.appid, self.releases, self.log = appid.lower(), releases, log
        self.answers: list[str] = []
        releases.mkdir(parents=True, exist_ok=True)
        name = f"{self.appid.strip('{}')}-{version}.crx3"
        shutil.copy(crx, releases / name)
        data = (releases / name).read_bytes()
        self.release = protocol.Release(version, name, len(data),
                                        hashlib.sha256(data).hexdigest(), "mini_installer.exe",
                                        ARGUMENTS)
        self.set_fraction(0.0)
        self._respond = protocol.respond
        protocol.respond = self._recording
        files = http.server.ThreadingHTTPServer(
            ("127.0.0.1", file_port), functools.partial(_Files, directory=str(releases)))
        self.service = service.Service(port, manifest.Store(releases),
                                       {cup_version: cup.load_key(cup_key)},
                                       f"http://127.0.0.1:{files.server_address[1]}")
        self.port = self.service.server_address[1]
        self._servers = [files, self.service]
        for server in self._servers:
            threading.Thread(target=server.serve_forever, daemon=True).start()

    def _recording(self, body: bytes, *args, **kwargs) -> bytes:
        payload = self._respond(body, *args, **kwargs)
        with open(self.log, "a", encoding="utf-8") as f:
            f.write(json.dumps({"body": json.loads(body)}) + "\n")
        prefix = len(self._protocol.RESPONSE_PREFIX)
        for app in json.loads(payload[prefix:])["response"]["apps"]:
            if app["appid"].lower() == self.appid and "updatecheck" in app:
                check = app["updatecheck"]
                self.answers.append(check.get("nextversion", check["status"]))
        return payload

    def set_fraction(self, fraction: float) -> None:
        m = self._manifest
        m.write(m.Manifest({
            self.appid: m.AppEntry(None, (), m.Candidate(self.release, fraction)),
            self._identity.UPDATER_APPID: m.AppEntry(None, ())}), self.releases)

    def shutdown(self) -> None:
        for server in self._servers:
            server.shutdown()
            server.server_close()
        self._protocol.respond = self._respond
```

- [ ] **Step 4: Run the tests.** Same command. Expected: `OK` (3 tests), not skipped (`SRV` is beside `WEBOPS` here). Then the whole suite: `OK`.

- [ ] **Step 5: Commit.**

```bash
cd /c/Users/robyv/Desktop/DLU/webops && git add tools/candidate_server.py tools/tests/test_candidate_server.py && python tools/lint.py && git commit -q -m "tools: the update server's own service, for the end-to-end test"
```

---

### Task 16: The end-to-end test halts, then rolls out

**Files:**
- Modify: `tools/update_smoke.py`
- Test: `tools/tests/test_update_smoke.py`

- [ ] **Step 1: Write the failing tests.** Append to `tools/tests/test_update_smoke.py`:

```python
class RolloutArgumentsTest(unittest.TestCase):
    BASE = ["sandbox", "--offline-installer", "s.exe", "--release-version", "1.0.0.1",
            "--update-crx", "u.crx3", "--update-version", "1.0.0.2", "--appid", "{a}"]

    def problem(self, *extra):
        return update_smoke.argument_problem(update_smoke.parser().parse_args(self.BASE
                                                                             + list(extra)))

    def test_rollout_with_the_offline_installer_and_an_update(self):
        self.assertIsNone(self.problem("--rollout-server", "srv", "--cup-version", "2"))

    def test_rollout_serves_its_own_package(self):
        self.assertIn("no --server", self.problem("--rollout-server", "srv", "--server", "x"))

    def test_rollout_and_the_recovery_drill_are_separate_runs(self):
        self.assertIn("separate", self.problem("--rollout-server", "srv", "--recovery-crx", "r",
                                               "--recovery-version", "1.0.0.3"))

    def test_the_run_command_carries_rollout(self):
        args = update_smoke.parser().parse_args(["run", "--payload", "p", "--results", "r",
                                                 "--appid", "{a}", "--rollout",
                                                 "--cup-version", "2"])
        self.assertTrue(args.rollout)
        self.assertEqual(args.cup_version, 2)
```

- [ ] **Step 2: Run them.** Expected: `error: unrecognized arguments: --rollout-server`.

- [ ] **Step 3: Implement.** In `tools/update_smoke.py`:
  - import `candidate_server`;
  - the docstring's usage gains `[--rollout-server SRV [--cup-version N]]` and this paragraph: `With --rollout-server, the test runs the update server repository's own service (a copy of SRV) instead of tools/update_server.py, and offers the update as its candidate: first halted, at fraction 0, when the updater must stay on V1, then at fraction 1, when it must take V2 (sub-project E).`;
  - `run` gains the parameters `rollout: bool = False, cup_version: int = 1`. Replace the block that starts the local server with:

```python
    local = candidate = None
    if rollout:
        candidate = candidate_server.CandidateServer(
            payload, Path(tempfile.mkdtemp(prefix="releases-")), appid, update_version,
            payload / "update.crx3", payload / CUP_KEY, cup_version, log)
    elif server is None:
        local = update_server.UpdateServer(
            ("127.0.0.1", 8484),
            update_server.Offer(appid, update_version, payload / "update.crx3",
                                "mini_installer.exe", "--verbose-logging --do-not-launch-chrome"),
            update_server.load_key(payload / CUP_KEY), log)
        threading.Thread(target=local.serve_forever, daemon=True).start()
```

  - replace `if not update_to(update_version, "update"):` and its `return result` (inside `if update_version:`) with:

```python
            if candidate:
                subprocess.run([str(updater), "--update-apps", "--enable-logging"],
                               timeout=1800)
                asked = _wait(lambda: bool(candidate.answers), 600)
                time.sleep(60)  # time for an update the server did offer to land
                failures = [] if asked else ["the updater made no update check"]
                failures += [f"the server offered {answer}" for answer in candidate.answers
                             if answer != "noupdate"]
                pv = _registered_version(exp, appid)
                failures += [] if pv == exp.release_version else [
                    f"pv is {pv!r}, expected {exp.release_version!r}"]
                if not step("halted (fraction 0)", failures):
                    return result
                candidate.set_fraction(1.0)
                if not update_to(update_version, "rolled out (fraction 1)"):
                    return result
            elif not update_to(update_version, "update"):
                return result
```

  - in the `finally:` block, after `if local: local.shutdown()`, add `if candidate: candidate.shutdown()`;
  - `run_in_sandbox` gains `rollout_server: Path | None = None, cup_version: int = 1`; after copying the recovery CRX:

```python
    if rollout_server:
        shutil.copytree(rollout_server / "ghost_update", payload / "ghost_update",
                        ignore=shutil.ignore_patterns("__pycache__"))
```

    and the script gains `+ (f" --rollout --cup-version {cup_version}" if rollout_server else "")`;
  - the parser: `sandbox` gains `s.add_argument("--rollout-server", type=Path, help="project-ghost-update-server: run its service, halted then rolled out")` and `s.add_argument("--cup-version", type=int, default=1, help="the CUP key version the build pins (test identity: 2)")`; `run` gains `r.add_argument("--rollout", action="store_true")` and `r.add_argument("--cup-version", type=int, default=1)`;
  - `argument_problem` begins with:

```python
    if getattr(args, "rollout_server", None):
        if args.server:
            return "the rollout serves its package itself; no --server"
        if args.recovery_crx:
            return "the rollout and the recovery drill are separate runs"
        if not (args.offline_installer and args.update_crx and args.update_version):
            return "the rollout needs --offline-installer, --update-crx and --update-version"
```

  - `main` passes `rollout_server=args.rollout_server, cup_version=args.cup_version` to `run_in_sandbox`, and `rollout=args.rollout, cup_version=args.cup_version` to `run`.

- [ ] **Step 4: Run the whole suite and lint.** Expected: `OK`, lint clean.

- [ ] **Step 5: Commit.**

```bash
cd /c/Users/robyv/Desktop/DLU/webops && git add tools/update_smoke.py tools/tests/test_update_smoke.py && python tools/lint.py && git commit -q -m "tools: the end-to-end test halts a candidate, then rolls it out"
```

---
### Task 17: The spike, part 2: the test suites in the official build

Runs once Task 1's build has finished.

- [ ] **Step 1: Build `ghost_browsertests` in `out/release`, timed,** with the memory sampler of Task 1 (copy `spike_build.ps1` to `spike_browsertests.ps1`, set `$targets = "ghost_browsertests"`, and write to `$log\browsertests.log`). Start it detached as in Task 1 Step 5.

- [ ] **Step 2: Decide where `ghost_browsertests` runs.** If its build finished with exit 0, in under three hours and with available memory never under 2 GB, it stays in `out/release` (`BROWSERTESTS_OUT = OUT` in `tools/release.py`, as written). Otherwise set `BROWSERTESTS_OUT = Path("out") / "vanilla"` in `tools/release.py`, update `CommandTest.test_build` and `test_tests_run_on_the_bits_that_ship` in `tools/tests/test_release.py` to expect the extra `autoninja -C out\vanilla … ghost_browsertests` command and no `ghost_browsertests` in the release build, run the suite, and commit with the measured reason in the message.

- [ ] **Step 3: Run both suites from `out/release`** (or `ghost_browsertests` from `out/vanilla` per Step 2):

```powershell
cd C:\Users\robyv\Desktop\DLU\webops\chromium\src
.\out\release\ghost_unittests.exe 2>$null | Select-String "SUCCESS|FAILED|tests ran"
.\out\release\ghost_browsertests.exe 2>$null | Select-String "SUCCESS|FAILED|tests ran"
```

Expected: `SUCCESS: all tests passed.` for each. A test that fails only in the official build is a finding: debug it (superpowers:systematic-debugging) before Task 18; two browser tests are known to hit `EXCESSIVE_OUTPUT` on a first run and pass on retry (roadmap).

- [ ] **Step 4: Record and commit.** Add to the progress notes' table: the `ghost_browsertests` build (time, peak memory), where it runs and why, and both suites' results.

```bash
cd /c/Users/robyv/Desktop/DLU/webops && git add docs/superpowers/specs/2026-10-05-release-pipeline-spike.md && python tools/lint.py && git commit -q -m "docs: the release pipeline's spike, the test suites in the official build"
```

---

### Task 18: The first release, `152.0.7977.149-1`

The user is present: this task pushes a tag, asks for the publisher key's PIN, and creates a draft release on GitHub (visible only to the repository's maintainers).

- [ ] **Step 1: Push the work.** Ask the user to push `WEBOPS` and `SRV` (Tasks 2 to 17). After the push, wait for both tooling jobs in each repository to finish and check they pass (`gh run list -L 3` and `gh run view <id> --json jobs`).

- [ ] **Step 2: Ask, then tag.** Ask the user for approval to create and push the annotated tag `152.0.7977.149-1` on `main`'s head. With approval:

```bash
cd /c/Users/robyv/Desktop/DLU/webops && git tag -a -m "Release 152.0.7977.149-1 (test identity)" 152.0.7977.149-1 && git push origin 152.0.7977.149-1 && git -C chromium/src/ghost pull -q --ff-only && git -C chromium/src/ghost log --oneline -1
```

Expected: `src/ghost` at the tagged commit.

- [ ] **Step 3: Check without building.** Every check must pass before anything long starts:

```bash
cd /c/Users/robyv/Desktop/DLU/webops && python -c "import sys; sys.path.insert(0, 'tools'); import release; from pathlib import Path; print(release.check(release.Context(tag='152.0.7977.149-1', src=Path('chromium/src').resolve())))"
```

Expected: `[]`. Anything else: fix the cause (push, wait for CI, `git -C chromium/src status`), never the check.

- [ ] **Step 4: Ask, then run the release.** Tell the user what the run does: it builds (an LTO respin), runs the tests (one Windows Sandbox, the egress audit's browser window for about 15 minutes; nobody may use that window), asks for the PIN once, and creates the draft release. With approval, start it detached, logging to the scratchpad:

```powershell
Start-Process powershell -ArgumentList "-NoProfile","-Command","cd C:\Users\robyv\Desktop\DLU\webops; `$env:PATH = 'C:\src\depot_tools;' + `$env:PATH; python tools\release.py run --tag 152.0.7977.149-1 --src chromium\src *> $SCRATCH\release-1.log; 'exit ' + `$LASTEXITCODE | Out-File -Append $SCRATCH\release-1.log" -WindowStyle Minimized
```

Watch `Get-Content $SCRATCH\release-1.log -Tail 5`. Expected stages in order: `[sync]`, `[apply]`, `[build]`, `[test]`, `[sign]`, `[describe]`, `[draft]`, then `release 152.0.7977.149-1 (152.0.7977.14901) done`. On a failure, read the log, fix the cause and re-run the same command: the finished stages are skipped.

- [ ] **Step 5: Verify the published files.**

```bash
cd /c/Users/robyv/Desktop/DLU/webops && python tools/release.py verify "$HOME/ProjectGhostReleases/152.0.7977.149-1/publish"
```

Expected: `verified`. Then `gh release view 152.0.7977.149-1` lists the five files, as a draft and a pre-release.

- [ ] **Step 6: Record** in the progress notes, from `state.json`: each stage's start and finish; the size of each published file; the number of packages in the SBOM (`python -c "import json; print(len(json.load(open(r'…\sbom.spdx.json'))['packages']))"`); the egress audit's result and the installer smoke test's. Commit the notes.

---

### Task 19: The second release, `152.0.7977.149-2`

The same source as `-1`, a respin: the update for the end-to-end test.

- [ ] **Step 1: Ask, then tag** `152.0.7977.149-2` on the same commit, as in Task 18 Step 2. (If commits were added since `-1`, such as the notes, the tag goes on `main`'s new head after its CI passed, and `src/ghost` is pulled.)
- [ ] **Step 2: Check** as in Task 18 Step 3, with the new tag. Expected: `[]`.
- [ ] **Step 3: Ask, then run** as in Task 18 Step 4, with `--tag 152.0.7977.149-2` and the log `release-2.log`.
- [ ] **Step 4: Verify** as in Task 18 Step 5, with `152.0.7977.149-2`. Expected: `verified`.
- [ ] **Step 5: Record** the stage times (the build stage is the respin's cost) and commit the notes.

---

### Task 20: End to end: halted, then rolled out

`SRV` must be at Task 14's commit. Leave 3 minutes after the last Sandbox run.

- [ ] **Step 1: Run the test.**

```bash
cd /c/Users/robyv/Desktop/DLU/webops && R="$HOME/ProjectGhostReleases" && python tools/update_smoke.py sandbox --offline-installer "$R/152.0.7977.149-1/publish/ProjectGhostOfflineSetup.exe" --tagged --codesign-cert branding/signing/test_codesign.cer --cup-key "$HOME/ProjectGhostKeys/test/cup_key_2.json" --cup-version 2 --release-version 152.0.7977.14901 --update-crx "$R/152.0.7977.149-2/publish/update.crx3" --update-version 152.0.7977.14902 --rollout-server ../project-ghost-update-server --appid "{c0ff4371-d9ab-461e-bffd-6b0dc2430b02}"
```

Expected, every step `ok`: clean machine, trust the signing certificate, install, signatures, halted (fraction 0), rolled out (fraction 1), signatures after the update, launch, privacy, uninstall. On a failure, read `run.log`, `result.json` and `updater.log` in the results directory before changing anything (superpowers:systematic-debugging).

- [ ] **Step 2: Record** each step and the run's duration in the progress notes; commit.

---

### Task 21: Mutation checks

Each change must make its check fail; each is undone before the next. Leave 3 minutes between Sandbox runs.

- [ ] **Step 1: A local change in the Chromium checkout.** `README.md` isn't built, so undoing it costs no rebuild:

```bash
cd /c/Users/robyv/Desktop/DLU/webops && echo "mutation" >> chromium/src/README.md && python tools/release.py run --tag 152.0.7977.149-2 --src chromium/src; echo "exit $?"; git -C chromium/src checkout -- README.md && git -C chromium/src status --short --untracked-files=no
```

Expected: `release: …\src has local changes, which no release may build…`, `exit 1`, then an empty status.

- [ ] **Step 2: A server that ignores the fraction.** In `SRV/ghost_update/protocol.py`, `Offer.choose`, replace `draw() < self.fraction` with `True`. Run Task 20 Step 1's command. Expected: `halted (fraction 0)` fails with `the server offered 152.0.7977.14902`. Then `git -C ../project-ghost-update-server checkout -- ghost_update/protocol.py`.

- [ ] **Step 3: A wrong hash in the provenance.**

```bash
cd /c/Users/robyv/Desktop/DLU/webops && M3="$SCRATCH/m3" && rm -rf "$M3" && cp -r "$HOME/ProjectGhostReleases/152.0.7977.149-2/publish" "$M3" && python - "$M3/provenance.intoto.json" <<'EOF'
import json, sys
path = sys.argv[1]
doc = json.load(open(path, encoding="utf-8"))
doc["subject"][0]["digest"]["sha256"] = "0" * 64
open(path, "w", encoding="utf-8").write(json.dumps(doc, indent=2) + "\n")
EOF
python tools/release.py verify "$M3"; echo "exit $?"
```

(`$SCRATCH` is the session scratchpad.) Expected: `FAILED  ProjectGhostOfflineSetup.exe: the provenance names another SHA-256` and `FAILED  provenance.intoto.json: its SHA-256 differs from SHA256SUMS`, `exit 1`.

- [ ] **Step 4: `--public` with the test identity.**

```bash
cd /c/Users/robyv/Desktop/DLU/webops && python tools/release.py run --tag 152.0.7977.149-2 --src chromium/src --public; echo "exit $?"
```

Expected: `release: the test identity never makes a public release (docs/licensing.md#release-gates)`, `exit 2`.

- [ ] **Step 5: Record** each check and its failing message in the progress notes; commit.

---

### Task 22: Documentation

**Files:**
- Create: `docs/build/release.md`
- Modify: `docs/threat-model.md`, `docs/architecture.md`, `docs/testing.md`, `docs/build/windows.md`, `docs/build/build-host.md`, `docs/roadmap.md`, `docs/superpowers/specs/2026-10-05-release-pipeline-design.md`, `docs/superpowers/specs/2026-10-05-release-pipeline-spike.md`

- [ ] **Step 1: Write `docs/build/release.md`.** Its headings `## The release machine` and `## Provenance` are the anchors `tools/provenance.py` writes into every provenance document (`BUILDER_ID`, `BUILD_TYPE`); keep them. Fill each `<…>` with the values measured in Tasks 1, 17, 18 and 19:

````markdown
# Making a release

A release turns a tag into files people can install and update to: an official build, tested on the bits that ship, signed, with an SBOM and a provenance document, kept as a GitHub release, and offered by the update server. `tools/release.py` does all of it. Design: [release pipeline](../superpowers/specs/2026-10-05-release-pipeline-design.md).

## The release machine

Releases are built and signed on the machine that holds the signing keys ([signing](../signing/README.md)), not on the CI builder, which holds no secrets ([build-host.md](build-host.md#security)). Until the project has more, that is the reference machine (Ryzen 5 3600, 6 cores, 32 GB), building in `out/release` of its development checkout. `release.py` checks the checkout's state before every release instead of trusting it.

What one release costs there:

| Stage | Time |
|---|---|
| The first official build of a Chromium version | <Task 1> |
| A respin (the build stage of a later release) | <Tasks 18 and 19> |
| Tests: unit and browser suites, installer smoke test, egress audit | <Task 18> |
| Signing (`sign_release.py`, one PIN) | <Task 18> |
| SBOM, provenance, draft | <Task 18> |

`out/release` takes <Task 1> GB.

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

## What each stage does

| Stage | What it does |
|---|---|
| `sync` | Syncs Chromium when the pin moved (`bootstrap.py --pgo`), and fetches the PGO profile |
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
````

- [ ] **Step 2: The other documents.**
  - `docs/threat-model.md`, "Supply chain", replace the **Release builds** bullet and its sub-bullets with:

```markdown
- **Release builds** are made by `tools/release.py` from a pushed tag whose commit passed CI, on the machine that holds the signing keys ([release.md](build/release.md)). It refuses a checkout with local changes or at another commit.
  - They are signed with hardware-held keys ([signing](signing/README.md)). Once the project has a second maintainer, releases to Stable require two maintainers' approval.
  - Each release publishes an SBOM (SPDX) and build provenance (SLSA Build Level 1: complete, not yet signed by a build platform).
```

  - `docs/architecture.md`, "Updates and signed data", after the **Two publisher keys** bullet:

```markdown
- **Staged rollout.** The server holds a candidate release beside the active one and offers it to a fraction of update checks, drawn per check because requests carry no identifier; a fraction of 0 halts it ([release.md](build/release.md#rolling-out)).
```

  - `docs/testing.md`, the updater's end-to-end test: add a bullet:

```markdown
  - **Rollout** ([release.md](build/release.md#rolling-out)): `--rollout-server` runs the update server repository's own service in the Sandbox and offers the update as its candidate. **halted (fraction 0):** the updater checks and stays on the installed release; **rolled out (fraction 1):** it takes the update. Four mutation checks cover the release pipeline: a local change in the Chromium checkout, a server ignoring the fraction, a wrong hash in the provenance, and `--public` with the test identity ([progress notes](superpowers/specs/2026-10-05-release-pipeline-spike.md#mutation-checks)).
```

  - `docs/build/windows.md`, at the start of "Signed releases", add this paragraph:

```markdown
For a release, `tools/release.py` does all of this and more: see [release.md](release.md). The steps below sign a development build by hand.
```

  - `docs/build/build-host.md`, "Security": in the bullet that begins "The builder holds no secrets", replace its last clause, which points releases at a separate builder from Phase 2, with:

```markdown
Signing keys never go on this machine: releases are built and signed on the release machine ([release.md](release.md#the-release-machine)).
```
  - `docs/roadmap.md`, item 5 (E): add

```markdown
   **Done <date>.** `tools/release.py`, `release_state.py`, `sbom.py`, `provenance.py`, `candidate_server.py`; `build/args/release.gn`; in the update server, the candidate and `stage`, `set-fraction`, `halt`, `promote`, `drop`; [design](superpowers/specs/2026-10-05-release-pipeline-design.md), [progress notes](superpowers/specs/2026-10-05-release-pipeline-spike.md), [release.md](build/release.md).
   - Official builds (ThinLTO, PGO) on the reference machine: <first build> for a Chromium version, <respin> per respin.
   - `152.0.7977.149-1` and `-2` released as GitHub drafts, each with an SBOM (SPDX), SLSA Build Level 1 provenance and checksums; the egress audit on the official build finds no unexpected host.
   - In Windows Sandbox, the update server's own service holds `-2` as a candidate: halted, the updater stays on `-1`; at fraction 1, it takes `-2`.
   - Four mutation checks fail as required.
```

  - The spec: its status line becomes the first line below; under "In `project-ghost-update-server`", the `ghost_update/release.py` row's text becomes the second; and the `symbol_level` row of the configuration table gives the value chosen, 1, and why.

```markdown
- Status: done <date> ([progress notes](2026-10-05-release-pipeline-spike.md)); design approved 2026-10-05
With `--fraction`, uploads as the candidate; without it, activates as before (C's first deployment uses that)
```
  - The progress notes: sections **Releases** (Tasks 18, 19), **The end-to-end run** (Task 20), **Mutation checks** (Task 21), and **Findings** (anything the implementation found).

- [ ] **Step 3: Check and commit.**

```bash
cd /c/Users/robyv/Desktop/DLU/webops && python -m unittest discover -s tools/tests -t tools 2>&1 | tail -3 && git add docs/ && python tools/lint.py && git status --short && git commit -q -m "docs: making a release"
```

- [ ] **Step 4: Ask the user to push** `WEBOPS` and `SRV`. After a push, wait for both tooling jobs in each repository to finish and check they pass.

---

## Self-review notes

- **Spec coverage:** official builds and PGO (Tasks 1, 17); the local tool with resumable stages (Tasks 3, 6–10); the checks, including the series and a clean checkout (Tasks 2, 6); tests on the bits that ship, the egress audit on the official build (Tasks 7, 17, 18); one signing run (Task 7); SBOM (Task 5); provenance, SLSA Build Level 1, `SHA256SUMS`, `verify` (Tasks 4, 9); draft releases and the `--public` refusal (Tasks 7, 8, 10); the candidate, its fraction and the admin commands (Tasks 11–14); `rollout`/`halt`/`promote`/`drop` from the release machine (Tasks 7, 10); the end-to-end test with the server's own service (Tasks 15, 16, 20); the four mutation checks (Task 21); the documents (Task 22).
- **One change from the spec:** the server's release CLI keeps activating without `--fraction` (Task 14), because C's deferred first deployment uses it; Task 22 records it in the spec.
- **`symbol_level`:** the spec left it to the spike; the plan sets 1 up front (Task 1), since the default for official Windows builds (2) would not fit the free disk, and changing it later means a full rebuild. The spike measures what 1 costs.
- **Long-running work:** Task 1's build runs in the background while Tasks 2–16 proceed; none of them builds in `SRC`.
