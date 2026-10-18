# Breakage controls: design

- Status: done 2026-10-18 ([progress notes](2026-10-18-breakage-controls-spike.md)), the look approved by the user. Design approved 2026-10-18. Amended while planning ([plan](../plans/2026-10-18-breakage-controls.md)): the rule that blocked each request is found when the report is asked for, by a temporary engine with debug info built from the same lists (the filtering engine has none, and one for every request would cost memory and check time); the scriptlets' names leave the report, since they follow from the address and the lists' versions; every field of the issue form is an input or a text area, which pre-fill from the URL, and the form carries its label itself.
- Phase 4, sub-project 4D-2 ([roadmap](../../roadmap.md#phase-4-content-protections)), after [4D-1, the compatibility corpus](2026-10-15-compatibility-corpus-design.md). Phase 4 completes with it.
- Builds on [3D-1, the protection levels](2026-10-10-protection-levels-design.md), [3D-3, the protections panel](2026-10-10-protections-panel-design.md), and 4A–4C's content protections.

## Goal

When a site doesn't work, the user can fix it in two clicks and tell us about it. The protections panel lowers the site's level by one and reloads the page. It then asks whether that helped and offers a report: a pre-filled GitHub issue, shown to the user before anything leaves the machine. Reports feed `shade.txt`, code fixes and the corpus, as 4D-1's findings did.

## Decisions

Settled in discussion on 2026-10-18:

- **Reports go to GitHub issues in `robyroro/project-ghost`,** through an issue form, `.github/ISSUE_TEMPLATE/breakage.yml`, labelled `breakage`. Shade opens the form pre-filled; the user submits it there. It is free, needs no server, and triage happens next to the corpus and `shade.txt`. The report is public, and the user needs a GitHub account. Rejected: email (most Windows machines have no mail client set up), copy-only (almost nobody reports), and our own endpoint (it waits for sub-project C's server). A separate reports repository was rejected too: one more repository to keep, and the name Shade in a public URL before the trademark search.
- **The report holds what triage needs and nothing personal:**
  - the page's address without query or fragment;
  - the level;
  - whether one level lower helped;
  - Shade's version and the lists' versions;
  - what broke: a category and the user's text;
  - the hosts blocked on the page, each with the rule that blocked it;
  Never full request URLs. Never the page's query, which carries tokens and session ids.
- **A guided step before the report.** "Site not working?" offers the level one lower (Strict to Standard, Standard to Off) and reloads. After the reload the panel asks "Is it working now?". Yes keeps the new level; No puts the old one back. Either way the report follows, saying which happened, which tells us whether Shade was at fault. At Off, the button goes straight to the report. Rejected: report only (reports wouldn't say whether lowering helped) and a temporary Off (one more state to explain; neither Firefox nor Brave has one).
- **Approach: everything in the existing panel, the URL built in the browser.** The panel shows the guided step and the report's preview. The browser builds the GitHub URL from what it holds and opens the tab; the panel's renderer never supplies a URL ([threat model](../../threat-model.md): renderer-reported data is display-only). Rejected: a separate `chrome://` report page (two previews, ours and GitHub's form, and more patches) and a bare GitHub link with no preview in Shade. The bare link breaks the [privacy model](../../privacy-model.md#blocking)'s promise that a report is shown before anything is sent: GitHub receives the URL's contents when the form opens, before Submit.
- **The entry point is a button under the levels,** "Site not working?" (the user's choice from mockups; a footer link was the other option).

## The flow

| State | The panel shows | Buttons |
|---|---|---|
| 1. Normal | The count, the levels, the level's description, then the button **Site not working?** | |
| 2. Offer | "Try *Standard* on example.com: the page reloads with fewer protections." | Cancel · Reload with Standard |
| 3. Check | "Is it working now? example.com is at *Standard*." The panel reopens on its own once the reload has loaded. | No, put Strict back · Yes |
| 4. Report | A category (page content missing or broken, video or audio, sign-in or forms, something else), an optional text, the address (editable), and the rest of the report as read-only text. "Opens a public issue on GitHub, filled in with the above. Nothing is sent until you submit it there." | Cancel · Open on GitHub |

At Off, state 1's button leads to state 4, and the report says the site was already Off.

## Components

| Unit | What it does |
|---|---|
| `PageProtections` (3D-3, extended) | Beside the per-tab count, keeps the page's blocked requests, the first of each host (the rule is found when a report is asked for): at most 50 distinct hosts, cleared when a new page starts, never sent anywhere by itself. |
| `BreakageCheck` (new, a `WebContentsUserData`) | The guided step's state for a tab: the site, its choice before the step (a level, or none), the level it moved to. `TryLowerLevel` records it, sets the level and reloads; when the reload has loaded, it asks the protections button to reopen the panel in state 3. `KeepLevel` drops the record; `RestoreLevel` writes the old choice back, removing the site's entry when it had none. A navigation to another site drops the record and keeps the new level. |
| `protections.mojom` (extended) | `TryLowerLevel()`, `KeepLevel()`, `RestoreLevel()`, `GetReport() => (Report)`, `OpenReport(Category category, string details, string address)`. `State` gains the step, so the panel opens in the right state. |
| `BreakageReport` (new, `//ghost/browser/protections`) | Builds the report from the tab's data, and the issue URL from the report: `https://github.com/robyroro/project-ghost/issues/new?template=breakage.yml&labels=breakage&title=…&<field id>=…`. Each value is percent-encoded. Above 8,000 characters the host list is cut, ending with "+N more". |
| The panel's WebUI (3D-3, extended) | States 2–4, in the panel's Lit app, with the strings in `protections_strings.cc`. |
| `.github/ISSUE_TEMPLATE/breakage.yml` | The issue form. The field ids are fixed: `site`, `level`, `outcome`, `version`, `lists`, `blocked`, `category`, `details`. |
| List versions | Read from the lists' `! Version:` headers when the engine loads them; `shade.txt` is identified by Shade's version, which ships it. |

## Data flow

1. The panel opens and calls `GetState()`, as today.
2. *Site not working?* → state 2 (in the panel). *Reload with…* → `TryLowerLevel()`: the browser records the step, sets the level, reloads. The panel closes with the reload.
3. The reload loads; `BreakageCheck` reopens the panel; `GetState()` says state 3. *Yes* → `KeepLevel()`; *No* → `RestoreLevel()`. Both lead to state 4.
4. State 4 calls `GetReport()` and shows it. *Open on GitHub* → `OpenReport(category, details, address)`. The browser checks the inputs, builds the URL from its own report and opens it in a new foreground tab of the same window. In Incognito it stays Incognito.

## Errors and limits

- **Pages where protections don't apply** (not http(s)): the panel already says so and shows no button.
- **The user leaves before answering** (another site, a closed tab): the lowered level stays. The user chose it, and the panel shows it; nothing changes silently.
- **No blocked request** on the page: the report says so in those words. It is a sign the breakage isn't Shade's.
- **Bad input from the panel** (its renderer isn't trusted): an unknown category, details over 2,000 characters, or an address that isn't an http(s) URL. Each is a bad message. An address on another site than the page's is replaced by the page's origin.
- **Opening the issue URL sends its contents to GitHub,** before Submit. That's why the address is edited in Shade's preview, not on GitHub's form, and why the preview says what will happen.

## Security and privacy

- The browser builds the only URL that leaves. The panel sends a category from a fixed enum, a bounded text, and an address the browser checks against the page's site.
- Nothing is sent without the user's click on *Open on GitHub*, and the issue isn't created without their Submit on GitHub.
- The blocked-host list stays in the browser process for the page's lifetime.
- The privacy model's "Breakage reports" line is rewritten to say exactly what a report holds and when it leaves.

## Testing

- **`ghost_unittests`:**
  - the report drops the query and fragment, and an edited address on another site falls back to the origin;
  - the URL's encoding, and the cut at 8,000 characters with "+N more";
  - the host list keeps at most 50 distinct hosts, the first rule for each, and empties for a new page.
- **`ghost_browsertests`,** on the 4A–4C test pages:
  - `TryLowerLevel` moves Strict to Standard and reloads; `RestoreLevel` puts the exact old choice back, including "none"; `KeepLevel` keeps the new level;
  - the panel reopens in state 3 after the reload;
  - a report on a page with a tracker names its host and rule;
  - `OpenReport` opens a tab whose URL is the expected one (the navigation is intercepted; nothing reaches GitHub);
  - bad input is a bad message;
  - in Incognito the tab opens Incognito.
- **Tooling:** a Python test checks that `breakage.yml`'s field ids are the ones `BreakageReport` writes.
- **Mutation checks**, each of which must fail a test:
  - the page's query left in the address;
  - `RestoreLevel` writing a level instead of removing the site's entry;
  - the same-site check on the edited address removed;
  - the 50-host cap removed.
- **The egress audit** finds no unexpected host: the flow sends nothing without a click.
- **By hand:** the user approves the panel's look. The issue URL is opened to check that GitHub's form is filled in, without submitting it. A real test issue only with the user's approval.

## Documentation

- The privacy model: what a report holds, and when it leaves.
- Architecture: the new units.
- Testing: the new tests.
- The roadmap: 4D-2 done, Phase 4 complete.

## Done when

- The flow works end to end in the dev build and the user approves its look.
- The tests pass with no retry, the mutation checks fail as required, and the egress audit finds no unexpected host.
- The issue form, opened from Shade, arrives filled in.
- The documentation is updated.

## Out of scope

- Sending anything automatically, and our own report endpoint (sub-project C's server).
- A temporary Off.
- Fixing Strict's 7 broken sites from 4D-1. The guided step covers them for users, and their reports say what to fix.
- Triage tooling for incoming reports, beyond the issue form's fields.
