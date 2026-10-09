# Security Releases Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A scheduled workflow opens an issue when Chromium's Extended Stable moves past the pin; `tools/upstream.py bump` moves the pin, the Chromium branch and the patch series to a security release in one command; a runbook ties that to `release.py`; and the whole procedure is run for real on 152.0.7977.158, timed.

**Architecture:** `tools/upstream.py` (standard library only) has three commands: `check` (chromiumdash vs `CHROMIUM_VERSION`), `report` (opens the issue through `gh`, deduplicated by title) and `bump` (verifies the tag with `bootstrap.record_tag`, runs `patches.canary`, cuts `ghost/<version>` with `patches.apply`, re-exports, writes the pin, commits). `.github/workflows/upstream.yml` runs `check` and `report` every three hours with `contents: read` and `issues: write`. The release itself is `release.py`, unchanged. Spec: [2026-10-09-security-release-design.md](../specs/2026-10-09-security-release-design.md).

**Tech Stack:** Python 3.11+ (`unittest`, `urllib`), git, GitHub Actions and the GitHub CLI, the existing release pipeline.

---

## Conventions

- `WEBOPS` = `C:\Users\robyv\Desktop\DLU\webops`, `SRC` = `$WEBOPS\chromium\src`, `SCRATCH` = the session's scratchpad.
- Tests: `python -m unittest discover -s tools/tests -t tools`; lint `python tools/lint.py` after `git add`. Commits small, no AI trailers.
- Don't edit files with backslashes through a Bash heredoc (it collapses `\\`); use the editor.
- Never run `bump` or anything else that rewrites `SRC` while a build runs there.
- The user said on 2026-10-09 "write the plan and do everything": pushing `main`, the drill's tag and its draft are approved for this drill. The publisher key's PIN needs the user at the machine (Task 9).

## File map

| File | Responsibility |
|---|---|
| `tools/bootstrap.py` | `record_tag()`, moved out of `execute()`, so `bump` uses the same tag check and fetch |
| `tools/upstream.py` | `check`, `report`, `bump` |
| `tools/tests/test_upstream.py` | Unit tests, saved replies and temporary repositories |
| `.github/workflows/upstream.yml` | The schedule |
| `tools/tests/test_workflows.py` | The workflow's permissions, schedule, pinned actions, order |
| `docs/build/security-release.md` | The runbook |
| `docs/security/drills/2026-10-09-152.0.7977.158.md` | The drill's record |
| `docs/patching.md`, `docs/build/release.md`, `docs/roadmap.md` | Point at the runbook; F's first half done |

---

### Task 1: The corrected spec and this plan

- [ ] **Step 1:** Commit the spec's correction (bump moves the Chromium branch, so gclient's rebase is a no-op) with this plan.

```bash
git add docs/superpowers/specs/2026-10-09-security-release-design.md docs/superpowers/plans/2026-10-09-security-release.md
python tools/lint.py
git commit -m "docs: security releases: the plan; bump moves the Chromium branch"
```

---

### Task 2: `bootstrap.record_tag`

**Files:** Modify `tools/bootstrap.py` (`execute`, the `tag_from_remote` branch). Tests: the existing `tools/tests/test_bootstrap.py` tag tests cover it through `execute`.

- [ ] **Step 1: Move the branch into a function.** Above `execute`:

```python
def record_tag(repo_dir: Path, ref: str, expected: str, env: dict[str, str]) -> None:
    """Fails unless origin's tag `ref` peels to `expected`, then records the
    tag locally. A checkout that already holds the commit gets only the ref:
    fetching the tag would download its pack a second time, and on Windows git
    cannot rename that over the identical, read-only pack. Otherwise the tag is
    fetched shallowly."""
    proc = subprocess.run(["git", "-C", str(repo_dir), "ls-remote", "origin", ref,
                           f"{ref}^{{}}"], capture_output=True, text=True, env=env, check=True)
    found = dict(reversed(line.split("\t", 1)) for line in proc.stdout.splitlines())
    # An annotated tag is listed twice; its peeled line names the commit.
    _check_pin(f"{ref} at origin", found.get(f"{ref}^{{}}", found.get(ref, "")), expected)
    present = subprocess.run(["git", "-C", str(repo_dir), "cat-file", "-e",
                              f"{expected}^{{commit}}"], capture_output=True, env=env)
    if present.returncode == 0:
        subprocess.run(["git", "-C", str(repo_dir), "update-ref", ref, expected],
                       env=env, check=True)
    else:
        subprocess.run(["git", "-C", str(repo_dir), "fetch", "--depth=1", "--no-tags",
                        "origin", f"+{ref}:{ref}"], env=env, check=True)
```

and in `execute`, the `elif step.tag_from_remote:` branch becomes `record_tag(*step.tag_from_remote, env)`. The `Step.tag_from_remote` comment shortens to "(repository, tag ref, expected commit): record_tag()".

- [ ] **Step 2:** `python -m unittest discover -s tools/tests -t tools -p test_bootstrap.py` passes unchanged.
- [ ] **Step 3:** Commit `tools: bootstrap's tag check is a function bump can call`.

---

### Task 3: `upstream.py check` and `report`

**Files:** Create `tools/upstream.py`, `tools/tests/test_upstream.py`.

- [ ] **Step 1: Tests first** (`CheckTest`, `ReportTest`): saved chromiumdash entries built by a helper `entry(version, milestone, commit, time)`, a fake `fetch(url)` that answers per channel, a temporary webops with `CHROMIUM_VERSION`.
  - current (same version), older reply → `current`, no issue;
  - `152.0.7977.158` against `.149` → `security-release`; title `Security release: Chromium 152.0.7977.158`; label `security-release`; body has `Published: 2026-10-06 17:06 UTC`, `Deadline: 2026-10-09 17:06 UTC`, the detection time given, the Chrome Releases search link, the runbook link;
  - `154.0.8037.100` → `milestone`, title `Milestone: Chromium 154 on Extended`;
  - an empty list, a reply that isn't a list, an entry without `hashes`, a commit that isn't 40 hex digits → `UpstreamError`; `main(["check"])` returns 2 on it;
  - `report`: current → no `gh` call; an existing issue with the same title → no creation; one whose title merely contains it → creation; creation makes the label, then the issue with title, body and label.
- [ ] **Step 2:** Run them: `ModuleNotFoundError`.
- [ ] **Step 3: Implement** `Release`, `fetch_json`, `releases`, `latest_extended`, `verdict`, `issue`, `check`, `gh`, `report`, `main` (code in the commit; the shapes are the spec's "upstream.py check").
- [ ] **Step 4:** Tests pass; lint; commit `tools: upstream.py notices Extended Stable's releases`.

---

### Task 4: The workflow

**Files:** Create `.github/workflows/upstream.yml`, `tools/tests/test_workflows.py`.

- [ ] **Step 1: Tests first:** the top-level `permissions:` block is exactly `contents: read` and `issues: write`, and no job sets its own; the schedule `17 */3 * * *` and `workflow_dispatch`; every `uses:` pinned to a 40-hex commit; `upstream.py check --json` runs before `upstream.py report`.
- [ ] **Step 2:** Run: fails (no file).
- [ ] **Step 3: The workflow:**

```yaml
# Watches Chromium's Extended Stable channel for Windows and opens an issue
# when it moves past CHROMIUM_VERSION: a security release to ship within the
# SLA, or a new milestone (docs/build/security-release.md).
name: upstream

on:
  schedule:
    # Every three hours, off the hour: GitHub delays runs scheduled at :00.
    - cron: "17 */3 * * *"
  workflow_dispatch:

# Reads the repository and writes issues and their labels; nothing else.
permissions:
  contents: read
  issues: write

concurrency:
  group: upstream
  cancel-in-progress: false

jobs:
  check:
    runs-on: ubuntu-24.04
    timeout-minutes: 10
    steps:
      # Actions are pinned to commit SHAs, as in tooling.yml.
      - uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1  # v7.0.1
        with:
          persist-credentials: false
      - uses: actions/setup-python@5fda3b95a4ea91299a34e894583c3862153e4b97  # v7.0.0
        with:
          python-version: "3.11"
      - name: Compare Extended Stable with the pin
        run: python tools/upstream.py check --json > "$RUNNER_TEMP/check.json"
      - name: Open the issue it calls for
        env:
          GH_TOKEN: ${{ github.token }}
          GH_REPO: ${{ github.repository }}
        run: python tools/upstream.py report --check "$RUNNER_TEMP/check.json"
```

- [ ] **Step 4:** Tests pass; commit `ci: a schedule watches Extended Stable`.

---

### Task 5: `upstream.py bump`

**Files:** Modify `tools/upstream.py`, `tools/tests/test_upstream.py`.

- [ ] **Step 1: Tests first** (`BumpTest(GitTestCase)`): an upstream repository with annotated tags `152.0.7977.149` and `152.0.7977.158` on successive commits; `src` a clone whose `origin` is it, on `ghost/152.0.7977.149` with one patch commit; a webops repository with the pin, `build/requirements.json` naming the old version in `chromium_version` and a source link, and `patches/` exported from `src`. A fake `fetch` lists `.158` with the upstream commit.
  - happy path: `src` on `ghost/152.0.7977.158`, its parent the new tag, the patch applied, the old branch kept; webops' `CHROMIUM_VERSION`, `CHROMIUM_COMMIT`, `requirements.json` (both places) moved; one new commit `build: move to Chromium 152.0.7977.158` holding only those files and `patches/`; clean tree;
  - refuses: uncommitted webops; `152.0.7977.140` (not newer); `154.0.8037.100` (milestone) — each with nothing changed;
  - chromiumdash naming another commit than the tag: stops, nothing changed, no new branch;
  - canary conflict (mocked): stops, nothing changed;
  - `git am` failing though the canary said clean (a real conflict upstream, canary mocked clean): `src` back on the old branch, the new branch gone, webops unchanged.
- [ ] **Step 2:** Run them: they fail (`bump` missing).
- [ ] **Step 3: Implement** `find_release`, `bump`, `_move_branch`, `_write_pin`, `_commit_message`, and the `bump` command in `main`, following the spec's eight steps.
- [ ] **Step 4:** Tests pass, whole suite; commit `tools: upstream.py bump moves the pin to a security release`.

---

### Task 6: Mutation checks

Each made, run, undone; nothing committed.

- [ ] **M1:** `verdict()` ignores the milestone (returns `security-release` for any newer version) → the milestone test fails.
- [ ] **M2:** `bump` replaces `bootstrap.record_tag(...)` with a plain `git fetch origin +<tag>:<tag>` → the moved-tag test fails.
- [ ] **M3:** `permissions: write-all` in the workflow → `test_workflows` fails.

---

### Task 7: The runbook and the docs it changes

**Files:** Create `docs/build/security-release.md`; modify `docs/patching.md` ("A new security release of the same milestone"), `docs/build/release.md` (links the runbook).

- [ ] **Step 1:** The runbook: when it applies; T₀ and deadlines (72 h; 24–48 h for a fix exploited in the wild); the steps table of the spec with each command, who, expected time; what to do when a step fails; the 60-day schedule note; what a drill records.
- [ ] **Step 2:** `patching.md`'s same-milestone section: `upstream.py bump`, what it does, then the runbook; the manual commands kept as what `bump` automates, for when it can't run.
- [ ] **Step 3:** Lint, tests, commit `docs: the security release runbook`.
- [ ] **Step 4: Push** (approved), wait for both tooling jobs: `gh run watch`.

---

### Task 8: The drill, part 1: the issue and `bump`

- [ ] **Step 1: Disk.** `Get-PSDrive C`: at least 40 GB free (the move rebuilds most of `out/release` and `out/vanilla`). If less, stop and ask.
- [ ] **Step 2: The issue.** `gh workflow run upstream.yml`, wait for the run (`gh run watch`), then `gh issue list --label security-release`. Expected: `Security release: Chromium 152.0.7977.158`. Note the run's start as the drill's start.
- [ ] **Step 3: `bump`.** From `WEBOPS`: `python tools/upstream.py bump --to 152.0.7977.158 --src chromium/src`. Expected: the canary's report (27 clean or merged), `Applied 27 patch(es) onto refs/tags/152.0.7977.158 as ghost/152.0.7977.158`, the export summary, the commit. Comment the time on the issue: `gh issue comment <n> --body "…"`.
- [ ] **Step 4:** Push; wait for tooling CI; comment the times.
- [ ] **Step 5:** Tag `152.0.7977.158-1`, push it; `git -C chromium/src/ghost pull --ff-only`; comment.

### Task 9: The drill, part 2: the release

- [ ] **Step 1:** Launch `release.py run --tag 152.0.7977.158-1 --src chromium\src` through WMI (as for -3/-4, so it outlives this session's shells), output to `SCRATCH\release-158-1.log`, with depot_tools first on `PATH`.
- [ ] **Step 2:** Watch the log; comment each stage's end on the issue (sync, apply, build, test, sign, describe, draft). The build is hours; nothing else builds in `SRC` meanwhile.
- [ ] **Step 3:** At `sign`, the user enters the PIN.
- [ ] **Step 4:** `python tools/release.py verify --tag 152.0.7977.158-1`; comment; the user closes the issue.

### Task 10: The record and the roadmap

**Files:** Create `docs/security/drills/2026-10-09-152.0.7977.158.md`; modify `docs/roadmap.md`, the spec's status, the memory.

- [ ] **Step 1:** The record: timeline (issue, bump, CI, tag, each stage, draft, verify), durations, the two totals (from the drill's start; from T₀ = 2026-10-06 17:06 UTC), what differed from a real release, what went wrong and what changes.
- [ ] **Step 2:** Roadmap: F's first half done with the measured times; the SLA section names the workflow and on-call; the SLA's third row noted as not automated.
- [ ] **Step 3:** Lint, tests, commit, push, CI.
