# Shipping a security release

What to do when Chromium ships a security release for our milestone, from the moment we hear of it to a verified release. The SLA is in [roadmap.md](../roadmap.md#security-release-sla); the design is [the security releases design](../superpowers/specs/2026-10-09-security-release-design.md).

## When this applies

Chromium's Extended Stable channel, which we follow ([ADR 0003](../adr/0003-upstream-extended-stable.md)), released a new version of the milestone we're on: `152.0.7977.149` to `152.0.7977.158`, say. A new milestone (152 to 154) is a different procedure: [patching.md, A new milestone](../patching.md#a-new-milestone).

## How we hear of it

`.github/workflows/upstream.yml` runs `tools/upstream.py check` every three hours. When Extended Stable's latest Windows release is newer than `CHROMIUM_VERSION`, it opens an issue, `Security release: Chromium <version>`, labelled `security-release`, and GitHub notifies the maintainer. The issue gives the publication time, the deadline and a link to the release notes, and carries the steps below as a checklist. A second run doesn't open a second issue.

A newer milestone opens `Milestone: Chromium <milestone> on Extended` when it is the pin's milestone plus two, the only one Extended Stable can move to. Any other milestone opens `Unconfirmed: Chromium <version> on Extended`, labelled `unconfirmed`, with no deadline: confirm it on Chrome Releases before anything moves. chromiumdash once listed `156.0.8078.13` as Extended's latest while we were on 152 and Chrome Releases announced no 156 ([#2](https://github.com/robyroro/project-ghost/issues/2)).

- Run the same check locally: `python tools/upstream.py check`.
- Run the workflow now: `gh workflow run upstream.yml`.
- GitHub disables a schedule after 60 days without activity in the repository. After a quiet period, check that the workflow is enabled (`gh workflow list`).

**On call** is one person, the maintainer, notified by GitHub. The times below are those of a one-person project.

## Deadlines

T₀ is upstream's publication, in the issue.

| Release | Deadline |
|---|---|
| A security release of our milestone | T₀ + 72 hours |
| One that fixes a bug exploited in the wild ("Google is aware that an exploit … exists in the wild" in the release notes) | T₀ + 24 to 48 hours: start at once |

## Steps

Note each step's time in the issue as a comment as you go (`gh issue comment <n> --body "…"`); the drill records come from them.

| Step | Who | What | Expected time |
|---|---|---|---|
| 0 | GitHub | The issue opens. | within 3 h of T₀ |
| 1 | maintainer | **Triage.** Read the release notes from the issue's link. Extended Stable's post lists no fixes; they're in the Stable post of the same week, whose CVEs Extended takes when they apply to its milestone. Exploited in the wild? Then the shorter deadline. | 10 min |
| 2 | engineer | **Move the pin**, from this repository: `python tools/upstream.py bump --to <version> --src <src>`. Never while a build runs in `<src>`. Then push `main`. | 5–15 min |
| 3 | GitHub | The tooling workflow passes on the commit. | 5 min |
| 4 | maintainer approves, engineer runs | **Tag** `<version>-1` and push it: `git tag -a -m "Release <version>-1" <version>-1`, `git push origin <version>-1`. Then `git -C <src>\ghost pull --ff-only`. | 2 min |
| 5 | engineer | **Release**: `python tools\release.py run --tag <version>-1 --src <src>`, with depot_tools first on `PATH`, started so it outlives the terminal ([release.md](release.md#making-a-release)). | about 16 h on the reference machine: [the first drill](../security/drills/2026-10-09-152.0.7977.158.md) |
| 6 | maintainer | The publisher key's PIN, when `release.py` reaches `sign`. `sign` comes after the tests; be at the machine when they end, or the release waits. | 2 min |
| 7 | engineer | **Verify** what people download: `gh release download <version>-1 --dir <empty dir>`, then `python tools\release.py verify <that dir>`. | 5 min |
| 8 | maintainer | Close the issue: shipped. | |

**What `bump` does**, in order, writing nothing until its checks pass:

1. Refuses when this repository has uncommitted changes, when the version isn't newer than the pin, or when it's another milestone.
2. Finds the version's commit on chromiumdash, and checks that the upstream tag is that commit. A difference stops everything: either the tag moved or chromiumdash is wrong; find out which before building anything.
3. Records the tag in `<src>`, fetching it shallowly if needed.
4. Runs `patches.py canary` onto the tag. A conflict stops it; nothing has changed.
5. Cuts `ghost/<version>` from the tag and applies the series (`patches.py apply`), so that the `gclient sync` in `release.py`'s `sync` stage finds the branch already on the new tag. The old branch stays.
6. Re-exports `patches/`, writes `CHROMIUM_VERSION`, `CHROMIUM_COMMIT` and the version in `build/requirements.json`, and commits them. It doesn't push.

**Then `release.py`**, unchanged: `sync` runs `bootstrap.py --pgo` for the new pin, `apply` finds the branch is the series, `build` builds `out\release` and, for `ghost_browsertests`, `out\vanilla`, then `test`, `sign`, `describe` and `draft`.

## When a step fails

- **The canary reports a conflict.** Resolve it by hand: on a branch from the new tag, apply the series (`patches.py apply --base refs/tags/<version> --branch ghost/<version>`), fix the patch that stops, `git am --continue`, `patches.py export`; check the result with `patches.py check`. Then commit the pin as `bump` would (the three files and `patches/`), or fix the conflict upstream of the series and run `bump` again.
- **`bump` says the tag doesn't match chromiumdash.** Don't build. Compare `git ls-remote https://chromium.googlesource.com/chromium/src refs/tags/<version>` with chromiumdash's entry, and find out which moved.
- **CI fails.** Fix it on `main`; the tag comes after a green run.
- **A `release.py` stage fails.** Fix the cause and run the same command: finished stages are skipped. `release.md` lists each stage.
- **The deadline will be missed.** Say so in the issue, with the reason and the expected time.

## Drills

A drill is this runbook run for real, timed, with a record in `docs/security/drills/`: the timeline from the comments, each step's duration, the totals from the drill's start and from T₀, and what changes because of what went wrong.
