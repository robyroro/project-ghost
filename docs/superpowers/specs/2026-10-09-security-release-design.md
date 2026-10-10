# Security releases: design

- Status: done 2026-10-10 ([the drill's record](../../security/drills/2026-10-09-152.0.7977.158.md)); design approved 2026-10-09
- Phase 2, sub-project F, first half ([roadmap](../../roadmap.md#phase-2-release-engineering)); the second half, one measured milestone move to 154, waits for Extended Stable 154 (expected around 2026-10-20)
- Depends on sub-project E ([release pipeline](2026-10-05-release-pipeline-design.md)) and on the product name ([Shade](2026-10-09-shade-rebrand-design.md)): the drill's release is the first official build named Shade

## Goal

When Chromium ships a security release for our milestone, we learn of it within hours, move to it with one command, and turn it into a verified release with the pipeline E built, inside the 72 hours [the roadmap](../../roadmap.md#security-release-sla) promises. The procedure is written as a runbook and proved by running it for real on 152.0.7977.158, with every step timed.

## Decisions

Settled in discussion on 2026-10-08 and 2026-10-09:

- **The runbook is written first, then run for real on 152.0.7977.158** (Extended, published 2026-10-06 20:06 local time; the pin is 152.0.7977.149). The drill measures every step against 72 hours from our start; against upstream's publication the SLA was already missed when the drill began (about 62 hours had passed), and the record says both.
- **The milestone move waits for Extended 154,** one real move, measured; no rehearsal on Stable.
- **Detection runs on GitHub, not on the reference machine:** a scheduled workflow checks chromiumdash every three hours and opens an issue. GitHub notifies the user by email or on the phone; the issue's time is when we knew. It needs no machine to be on, and costs nothing.
- **One tool, `tools/upstream.py`, with two commands:** `check` (what the workflow runs, and anyone can run locally) and `bump` (moves the pin to a security release of the same milestone). Rejected: a runbook of manual edits only (every release would hand-edit three files, and a mistyped commit would surface only when `bootstrap.py` stops, minutes later), and a `bump` stage inside `release.py` (a pin move is a commit on `main` that must pass CI before it can be tagged, which `release.py` already requires of its input).
- **`bump` refuses a milestone change.** A new milestone needs the toolchain requirements re-derived and a review of new web APIs and profile services ([patching.md](../../patching.md#a-new-milestone)); that's F's second half.
- **`bump` moves the Chromium branch too.** `release.py`'s `sync` runs `gclient sync --revision src@<new commit>`, and gclient rebases the checked-out branch onto that commit; on a shallow checkout the old and new tags share no history, so the series' branch must already sit on the new tag ([patching.md](../../patching.md#a-new-security-release-of-the-same-milestone), "Rebase before syncing"). Once the canary has shown the series applies, `bump` applies it with `patches.py`'s own `apply` on a new branch `ghost/<version>` cut from the new tag, and re-exports `patches/`, whose context lines may have moved. (Settled while planning, 2026-10-09: the first version of this design left the branch to `release.py`, which would have let gclient rebase it.)
- **On call is one person, the user,** notified by GitHub. The runbook's times are those of a one-person project.

## Components

| Unit | What it does |
|---|---|
| `tools/upstream.py` | `check`, `report` (opens the issue a check calls for: the workflow's step, kept in Python so it's tested) and `bump`, below. Standard library only. |
| `.github/workflows/upstream.yml` | Every three hours (`cron: "17 */3 * * *"`) and on demand: runs `upstream.py check --json`, then `upstream.py report`, which, when the verdict isn't `current`, opens the issue with `gh issue create`, unless an issue with the same title exists, open or closed. Permissions: `contents: read`, `issues: write`, nothing else. Actions pinned to commit SHAs, as in `tooling.yml`. |
| `tools/tests/test_upstream.py` | Unit tests, with saved chromiumdash replies and temporary git repositories; no network. |
| `tools/tests/test_workflows.py` | Reads `upstream.yml`: its permissions, its schedule, the title it deduplicates on. |
| `docs/build/security-release.md` | The runbook. |
| `docs/security/drills/2026-10-09-152.0.7977.158.md` | The drill's record. |
| `docs/patching.md` | "A new security release of the same milestone" becomes: `upstream.py bump`, then the runbook. |

### `upstream.py check`

1. Reads `https://chromiumdash.appspot.com/fetch_releases?channel=Extended&platform=Windows&num=1`: `version`, `milestone`, `hashes.chromium`, `time` (milliseconds since the epoch, UTC).
2. Compares with `CHROMIUM_VERSION`. The verdict:
   - `current`: the same version, or older (chromiumdash never goes back, but a reply is data, not truth);
   - `security-release`: the same milestone, a newer version;
   - `milestone`: a newer milestone.
3. Prints a summary, or with `--json` the verdict, the version, the commit, the publication time and, when not `current`, the issue's title, labels and body.

**The issue.** Title `Security release: Chromium 152.0.7977.158` or `Milestone: Chromium 154 on Extended`; label `security-release` or `milestone`. Its body: the version and commit; published at (UTC); detected at (UTC, the workflow's time); the deadline, publication plus 72 hours; a search link to the Chrome Releases blog for that version (`https://chromereleases.googleblog.com/search?q=<version>`), where the triage reads whether a fix is exploited in the wild; and the runbook's steps as a checklist, linking `docs/build/security-release.md`.

**Errors.** No reply, a reply that isn't JSON, an empty list, or a release missing a field: exit 2 with the reason. The workflow then fails, and GitHub's failure notice reaches the user; no issue is opened from a broken reply.

### `upstream.py bump --to VERSION --src SRC`

In order; nothing is written until every check has passed:

1. **Refuses** when this repository has uncommitted changes; when VERSION isn't newer than `CHROMIUM_VERSION`; when VERSION's milestone differs from the pin's (it names the milestone procedure).
2. **Finds VERSION's commit** in chromiumdash (the Extended and Stable lists, newest first, until VERSION) and **checks the tag:** `git ls-remote` on the upstream remote of SRC must resolve `refs/tags/VERSION` (peeled) to that commit. A difference stops everything: either the tag moved or chromiumdash is wrong, and someone must find out which.
3. **Fetches the tag** into SRC, shallow, the way `bootstrap.py` does (an update of the ref when the commit is already present, else `git fetch --depth=1`). This logic moves from `bootstrap.execute` into a function both call.
4. **Runs the canary:** `patches.py canary --onto refs/tags/VERSION` (a scratch index; the checkout isn't touched). Any `CONFLICT` or `FAILED` stops `bump`, printing the canary's report; nothing has changed.
5. **Moves the branch:** `patches.apply` cuts `ghost/VERSION` from the new tag and applies the series with `git am -3`. If that fails although the canary passed, `bump` aborts the `am`, returns SRC to the branch it was on, deletes the new one, and stops. The old branch stays, as after every move. This rewrites files in SRC: never while a build runs there.
6. **Re-exports** `patches/` from the new branch (context lines may have moved).
7. **Writes the pin:** `CHROMIUM_VERSION`, `CHROMIUM_COMMIT`, and the version in `build/requirements.json` (`chromium_version` and its two source links; the toolchain requirements are a milestone's, so the rest stays).
8. **Commits** the pin and `patches/` as `build: move to Chromium VERSION`, with the publication time, the tag's commit and the canary's counts in the message. It doesn't push.

After `bump`, everything is `release.py`'s, unchanged: `sync` sees the moved pin and runs `bootstrap.py --pgo` (gclient's rebase is now a no-op), `apply` finds the branch is the series and skips, `build` builds `out/release` and, for `ghost_browsertests`, `out/vanilla`.

## The runbook

`docs/build/security-release.md`, each step with who does it, the command and the expected time:

| Step | Who | What |
|---|---|---|
| 0 | GitHub | The issue opens. T₀ is upstream's publication; the deadline is T₀ + 72 h. |
| 1 | the user | Triage: the Chrome Releases post. A fix exploited in the wild makes the deadline 24–48 h. |
| 2 | engineer | `upstream.py bump --to VERSION --src SRC`; push to `main` once the user agrees. |
| 3 | GitHub | The tooling workflow passes on the commit. |
| 4 | the user approves, engineer runs | Tag `VERSION-1`, push it. |
| 5 | engineer | `release.py run --tag VERSION-1 --src SRC`, started so that it outlives the terminal (WMI on the reference machine, [release.md](../../build/release.md#making-a-release)). |
| 6 | the user | The publisher key's PIN at the `sign` stage. |
| 7 | engineer | `release.py verify --tag VERSION-1`: the draft on GitHub is the files on disk. |
| 8 | the user | Closes the issue: shipped. |

Each step's time goes into the issue as a comment as it happens; `release.py`'s `state.json` keeps what each stage did.

## The drill on 152.0.7977.158

The runbook, run for real, from the issue that the workflow's first run opens. Its record, `docs/security/drills/2026-10-09-152.0.7977.158.md`:

- the timeline, each step's duration, and two totals: from our start (the drill's measure) and from T₀ (the SLA, already missed for this version);
- what went wrong, and what changes because of it.

**What differs from a real release, and is written down as such:**

- The test signing identity: the draft is a draft, and the name's trademark gate is still open.
- No rollout: the update server's deployment (sub-project C) is deferred, so `release.py`'s `stage` isn't run.
- No update test from `152.0.7977.149-4` to the new release: `-4` predates the name and installs under `ProjectGhost`, so an update to Shade has nothing to find. The development end-to-end update test passed on 2026-10-09.

## Error handling

- `check`: a broken or empty reply exits 2 and fails the workflow (above). An existing issue with the same title means no new issue: the workflow says so and succeeds.
- `bump`: every refusal names its reason and changes nothing. A failure after the pin is written (the commit itself) leaves the three files as written, says so, and `git checkout` restores them.
- The runbook says what to do when a step fails: the canary reports a conflict (resolve it in SRC as [patching.md](../../patching.md#daily-workflow) says, export, then `bump` again); CI fails; a `release.py` stage fails (fix, run again: finished stages are skipped).

## Testing

**Unit tests, written first, no network:**

- `check`, on saved replies: current, a security release, a new milestone, an older version, an empty list, broken JSON, a missing field. The verdict, and the issue's title, labels, deadline and link.
- `bump`, in temporary git repositories standing in for upstream and SRC, with chromiumdash replies saved: refuses another milestone, an older version, a dirty repository; a tag at another commit stops it with nothing changed; a canary conflict changes nothing; the happy path leaves SRC on `ghost/VERSION` with the series on the new tag, and webops with one new commit holding the pin and `patches/`.
- The workflow file: permissions exactly `contents: read` and `issues: write`, the schedule, every action pinned to a commit SHA, deduplication by title.

**Mutation checks**, each must fail:

- M1: `check` ignores the milestone, so 154 looks like a security release.
- M2: `bump` skips the tag's commit check.
- M3: the workflow with `permissions: write-all`.

**The drill** is the end-to-end test: the issue, `bump`, the tag, `release.py run` with every test it runs (unit and browser tests, the installer smoke test, the egress audit), the signed draft, `verify`.

## Documentation

- `docs/build/security-release.md` (new) and the drill's record.
- [patching.md](../../patching.md): the same-milestone procedure is `bump`, then the runbook.
- [release.md](../../build/release.md): links the runbook.
- [roadmap.md](../../roadmap.md): F's first half done, with the measured times; the Security release SLA section names the detection and the on-call arrangement; the third SLA row (fixes on Stable not yet on Extended) noted as not yet automated.

## Done when

- [ ] `upstream.py`, the workflow and their tests are on `main`, tooling CI green; the three mutation checks fail as required.
- [ ] The workflow opened the issue for 152.0.7977.158 by itself.
- [ ] `152.0.7977.158-1` is a verified draft on GitHub, every test in `release.py` passed.
- [ ] The drill's record, the runbook and the docs above are written.

## Out of scope

- The milestone move to 154 (F's second half).
- Fixes released on Stable but not yet on Extended (the SLA's third row): reading Stable's security notes and cherry-picking. Noted in the roadmap.
- Rollout to the update server (sub-project C).
- A self-hosted builder running the release unattended.

**Note on scheduled workflows:** GitHub disables a schedule after 60 days without activity in the repository. While the project is active this never happens; the runbook says to check the workflow is enabled after a quiet period.
