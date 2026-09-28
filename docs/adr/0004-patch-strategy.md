# 0004. Hook-first changes and a git-native patch series

- Status: Accepted
- Date: 2026-09-28

## Context

Every line we change in Chromium's own files can conflict at each milestone move ([ADR 0003](0003-upstream-extended-stable.md)). Downstream browsers use three broad strategies:

1. **A maintained fork branch** that is merged or rebased continuously. It's simple, but the divergence is invisible until it conflicts.
2. **A patch series**, as in ungoogled-chromium and Cromite. Each change is explicit and reviewable. Applying stale patches with fuzz produces silent mis-applications, and refreshing them is tedious.
3. **Include-path overrides**, as in Brave's `chromium_src`. Replacement files `#include` the upstream file and redefine symbols with macros. Few textual conflicts, but hidden coupling: an override can silently stop matching upstream code.

Chromium also offers many extension points that need no source change at all: `ContentBrowserClient` and `ChromeContentBrowserClient` overrides, `ChromeBrowserMainExtraParts`, `KeyedService` factories, `NavigationThrottle`, `URLLoaderThrottle`, `WillCreateURLLoaderFactory`, pref default overrides, and feature overrides during variations setup.

## Decision

1. **Prefer upstream extension points.** Code that uses them lives entirely in `//ghost`.
2. **Otherwise, write a hook patch.** It's the smallest change to an upstream file that calls into `//ghost`, typically a few lines.
3. **Keep patches as a `git format-patch` series** in `patches/`, managed by `tools/patches.py`.
   - Developers work on a branch cut from the pinned tag, and `export` regenerates the files.
   - Moving to a new tag is `git rebase --onto <new> <old>`, which resolves with real three-way merges instead of fuzzy application.
   - Export pins every git setting that affects its output, so all developers produce byte-identical patches.
4. **Every patch carries two trailers:**
   - `Why:` explains why no extension point suffices;
   - `Upstream:` links an upstream bug, or says `not upstreamable: <reason>`.
5. **Budget and review.** `patches.py stats` reports the size of our divergence. Patches over 50 changed lines, or touching security-sensitive directories, need two reviewers ([CONTRIBUTING.md](../../CONTRIBUTING.md#review-requirements)).
6. **Include-path overrides are not used.** A later ADR may adopt them, but only if measured rebase time exceeds two engineer-days per milestone for two consecutive milestones.

## Consequences

- Most feature code (blocking, identities, privacy report) lives in `//ghost` and never conflicts.
- Blink changes for fingerprinting are unavoidable patches. The Blink side is kept to one-line calls into `//ghost/third_party/blink/renderer/ghost`, which a single build patch adds to Blink's sources.
- Reviewers see every upstream change as an explicit file with its justification.
- The series must stay linear, with no merge commits. `export` refuses merges.

## Alternatives considered

- **A long-lived fork branch without exported patches.** Divergence isn't reviewable as a unit, and history grows unboundedly with upstream merges.
- **Quilt-style patches applied with fuzz.** Fuzzy application can succeed while applying a hunk in the wrong place. Three-way merge through `git am -3` and `git rebase` either merges correctly or stops.
- **Brave-style `chromium_src` overrides from day one.** Powerful for a large divergence, but the macro redefinitions are hard to review and can drift silently from the code they override. Our divergence is small; we'd adopt this only with evidence that it's needed.
