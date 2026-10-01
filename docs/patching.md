# Carrying changes to Chromium

This is the working guide for the patch series in `patches/`. The reasoning is in [ADR 0004](adr/0004-patch-strategy.md), and the upstream channel in [ADR 0003](adr/0003-upstream-extended-stable.md).

## Current pin

| | |
|---|---|
| `CHROMIUM_VERSION` | `152.0.7977.149` |
| `CHROMIUM_COMMIT` | `ba6d62e0eacecd63aed843a9bf735a148efb93c4` (checked against the tag on every bootstrap; a mismatch stops the build) |
| Channel | Windows Extended Stable |
| Upstream release date | 2026-09-29 |
| Pinned on | 2026-10-01 |
| Toolchain for this tag | `build/requirements.json`, from the tag's `docs/windows_build_instructions.md` and `build/vs_toolchain.py` |

## When a patch is acceptable

Try these first, in order:

1. An upstream extension point used from `//ghost`. The list is in [architecture.md](architecture.md#how-ghost-code-reaches-chromium).
2. A default change through `SetDefaultPrefValue`, or a feature override.
3. A hook patch: the smallest edit to an upstream file that calls into `//ghost`.

**A patch is wrong if:**
- it contains feature logic that could live behind a hook;
- it touches files under `ghost/` (`patches.py check` rejects this);
- it contains binary files;
- it mixes unrelated concerns. One concern per patch, so it can be dropped or upstreamed on its own.

## Patch format

Patches are produced by `tools/patches.py export`. Never write or edit them by hand. The commit message in the Chromium checkout becomes the patch header:

```
area: imperative summary

What the hook does and why no extension point suffices, in prose.

Why: <one line: the reason this must be a patch>
Upstream: <crbug link to an upstream request> | not upstreamable: <reason>
```

`patches.py check` (run by `tools/lint.py`) enforces:
- contiguous `NNNN-slug.patch` names;
- the zero-hash `From` line that `export` writes;
- a `[PATCH] ` subject;
- non-empty `Why:` and `Upstream:` trailers;
- LF-only headers;
- no binary diffs;
- no paths under `ghost/`.

It warns when a patch touches a file with CR line endings.

## Daily workflow

```
cd <root>/src
python ghost/tools/patches.py apply --src .          # branch ghost/<version> from the tag + the series
# edit, build, test; commit as usual, fixups and rebase -i are fine
python ghost/tools/patches.py export --src .         # regenerate ghost/patches/
python ghost/tools/patches.py check
python ghost/tools/patches.py stats                  # size, and which patches need a second reviewer
```

**`apply`**
- refuses to run with uncommitted changes;
- refuses to overwrite an existing branch unless you pass `--force`, which discards commits on that branch that were never exported.

**If a patch doesn't apply**, `git am` stops with the conflict in place:
1. Resolve it.
2. Run `git am --continue`.
3. Run `export` to record the result.

**Commit the regenerated `patches/`** in this repository together with the `//ghost` changes that use them.

## Moving to a new release

### Before moving: the canary

`patches.py canary` reports how the series would fare on another upstream revision. It applies each patch in a scratch index and never touches the checkout. A real rebase would rewrite every patched file, and the build tool rebuilds whatever it sees modified.

```
git -C src fetch --depth=1 --no-tags origin +refs/tags/<version>:refs/tags/<version>
python ghost\tools\patches.py canary --src src --onto refs/tags/<version>
```

Each patch is reported as:
- `clean`: it applies as it is;
- `merged`: its context moved, and a three-way merge applies it;
- `CONFLICT` and its files: `git rebase` will stop there;
- `FAILED`: it can't be applied at all, for example because upstream deleted the file.

Patches that fail are left out, and the ones after them are tried on top of the rest. The command exits 1 when any patch needs work.

### A new security release of the same milestone

1. Update `CHROMIUM_VERSION`, and `CHROMIUM_COMMIT` to the commit that chromiumdash lists for that release (`git ls-remote origin refs/tags/<new>` must agree).
2. Fetch the new tag (see [Shallow checkouts](#shallow-checkouts)). On a new branch named after it, run `git rebase --onto refs/tags/<new> refs/tags/<old>`. There are usually no conflicts.
3. `patches.py export`. The diff of `patches/` should show only context changes.
4. Re-run `tools/bootstrap.py`. It checks the tag against `CHROMIUM_COMMIT` and syncs the dependencies by hash.
5. Build, run the full test suite, release. The SLA is in [roadmap.md](roadmap.md#security-release-sla).

**Rebase before syncing.** The working branch tracks no remote, so `gclient sync --revision src@<hash>` runs `git rebase <hash>` on it. That is a no-op once the branch sits on the new tag. On a shallow checkout the old and new tags share no history, and git would replay the old tag's whole tree as well as the series.

### A new milestone

1. Run the canary onto the new tag (above), or read the nightly canary report once the builder runs one. It lists the patches expected to conflict.
2. Re-derive `build/requirements.json` from the new tag's `docs/windows_build_instructions.md` and `build/vs_toolchain.py`. `tools/repo.py` refuses to load requirements that name a different version.
3. Rebase, export and sync as above, in that order. Resolve conflicts, preferring to shrink a patch rather than grow it.
4. **Review the milestone for new web-exposed APIs and new profile-scoped services.**
   - New APIs are classified for fingerprinting exposure ([privacy-model.md](privacy-model.md#limitations)).
   - New storage or services are classified in the isolation contract ([architecture.md](architecture.md#testing-isolation)).
5. Build, run the full suite plus the compatibility and performance runs.
6. Record the rebase time. Two consecutive milestones over two engineer-days trigger the ADR 0004 review.

### Shallow checkouts

A `--no-history` checkout may not contain the previous tag. Fetch it:

```
git fetch --depth=1 origin tag <version>
```

Rebasing needs history between the two tags. The machine that does milestone moves should keep a full-history checkout.
