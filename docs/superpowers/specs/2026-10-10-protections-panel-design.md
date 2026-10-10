# The protections panel: design

- Status: design approved 2026-10-10 (each choice by the user, from mockups); not started
- Phase 3, sub-project 3D-3 ([roadmap](../../roadmap.md#phase-3-network-protections)); it controls the levels of 3D-1 ([design](2026-10-10-protection-levels-design.md)) and counts what 3A and 3D-2 block
- Shade's first UI: it also builds the WebUI pipeline (grit, TypeScript, Mojo, a top-chrome page) that 3D-4's settings and Phase 8's privacy report reuse

## Goal

One click from the toolbar shows how many requests Shade blocked on the current page and sets the site's level: Off, Standard or Strict.

## Decisions

Decided by the user, 2026-10-10, from mockups:

- **The panel shows the level and one count**, the requests blocked on the current page load. Rejected: the level alone (nothing shows the protections at work); the level with a list of blocked domains and parameters (the per-category accounting is Phase 8's privacy report, which the panel grows into).
- **A toolbar button opens it**: a shield right of the omnibox, carrying the count as a badge. Rejected: an icon inside the omnibox (crowded, no room for the count); a row in the page info bubble (two clicks away, while [privacy-model.md](../../privacy-model.md#blocking) promises one).
- **The panel is WebUI hosted in a Views bubble**, as [architecture.md](../../architecture.md#ui) says. Rejected: a native Views bubble, rewritten as WebUI in Phase 8 anyway, while 3D-4 needs the WebUI pipeline regardless.

Decided by Claude, approved with the design:

- **Choosing the mode's default clears the site's entry** (`ClearLevel`) instead of writing it, so that the default level setting of 3D-4 applies to the site again. The default is labelled "default" in the control.
- **A change reloads the page** (3D-1 decided it applies at the next load); the panel stays open and its count starts again from zero.
- **The button stays where it is on pages without protections** (chrome://, file:, the new tab page): disabled, with a tooltip, so the toolbar doesn't shift.
- **The count is per page load**: the primary page of the tab, its subframes included; a navigation to a new document resets it. Blocked requests of shared and service workers aren't counted: nothing ties them to a tab.

## What the user sees

| State | Button | Panel |
|---|---|---|
| A site at its mode's default (Standard) | blue shield, badge with the count | the registrable domain; the count, "requests blocked on this page"; Off / Standard · default / Strict; one sentence on what the level does |
| A site set to Off | grey shield, struck through, no badge | "Off, nothing is blocked on this site"; the sentence says cookies, HTTPS and GPC protections still apply |
| Incognito (Strict by default) | the shield and badge of its level, in Incognito's colours | the same content, with Strict marked "default", plus "Changes here last until you close all Incognito windows." |
| A page without protections | disabled; tooltip "Protections don't apply to this page" | doesn't open |

The level sentences:

- *Off:* Trackers and ads load. Links keep their tracking parameters. Cookies, HTTPS and GPC protections still apply.
- *Standard:* Blocks third-party trackers and ads. Removes click identifiers from links to this site.
- *Strict:* Also blocks the site's own trackers. Removes campaign parameters. Sends no referrer to other sites.

The button's accessible name gives the level and the count ("Protections: Standard, 23 requests blocked"); the levels are a radio group. Colours come from the colour pipeline (light, dark, high contrast), none hard-coded.

## Components

### Counting

| Unit | What it does |
|---|---|
| **Patch 0037** (`chrome_content_browser_client.cc`) | `WillCreateURLLoaderFactory` passes its `frame` to `MaybeProxyURLLoaderFactory`. It changes only the line 0029 added: 0029 stays as it is, so nothing after it is replayed. |
| `//ghost/browser/blocking/request_filter` | `MaybeProxyURLLoaderFactory` takes the frame; `RequestFilter` keeps its `GlobalRenderFrameHostId` and reports each blocked request against it. |
| `//ghost/browser/blocking/connection_filter` | `FilterWebSocket` and `FilterWebTransport` already have the frame (0034); they report a blocked connection against it. |
| `//ghost/browser/protections/page_protections` | `PageProtections`, a `WebContentsUserData`: `RecordBlocked(GlobalRenderFrameHostId)` finds the frame's outermost main frame and counts it only when that frame's page is the tab's primary page (not a page being left, not a prerendered one); `PrimaryPageChanged` resets the count; observers hear each change. |

### The button

| Unit | What it does |
|---|---|
| **Patch 0035** (`toolbar_view.cc`, the toolbar's `BUILD.gn`) | `ToolbarView::Init` adds Shade's button after the location bar; `//chrome/browser/ui/views/toolbar:impl` links `//ghost/browser/ui/protections`, which depends only on the toolbar's and the bubble's header targets (no cycle). |
| `//ghost/browser/ui/views/protections/protections_button` | A `ToolbarButton`: the shield in its three looks (on, Off, disabled), the badge, the accessible name. Follows the active tab's `PageProtections` and level; a click shows the bubble through a `WebUIBubbleManager`, which preloads the page. |

### The panel

| Unit | What it does |
|---|---|
| `//ghost/browser/ui/webui/protections/protections.mojom` | `PageHandlerFactory.CreatePageHandler(Page, PageHandler)`; `PageHandler.GetState() => State`, `PageHandler.SetLevel(Level)`, `PageHandler.ShowUI()`; `Page.OnStateChanged(State)`. `State`: the site, the level, the mode's default, whether protections apply, the count, whether the profile is off the record. |
| `//ghost/browser/ui/webui/protections/protections_ui`, `protections_page_handler` | A `TopChromeWebUIController` and its config for `chrome://protections.top-chrome`. The handler builds `State` from `site_levels` and `PageProtections` for the tab the bubble belongs to; `SetLevel` writes the level (or clears it at the default) and reloads the tab. |
| `//ghost/browser/resources/protections/` | The page: TypeScript and Lit, built by `build_webui`; header, count, radio group, sentence, Incognito note. |
| `//ghost/browser/ui/protections/protections_strings` | Shade's strings, English only, in one C++ file: the page reads them through `loadTimeData`. A grd would need its own locale paks (another patch) for no translation yet; it replaces this file when translations come (amended while planning). |
| **Patch 0036** (`chrome_web_ui_configs.cc`, `chrome_browser_interface_binders_webui.cc`, `chrome_paks.gni`, `resource_ids.spec`, `histograms.xml`, Lit's `BUILD.gn`) | Registers the page in Chromium's lists, which have no hook: its config, Mojo binder, pak, resource ids, its name on the top-chrome allow-list, and Lit's visibility list. Amended while planning: the spec first had the config registered from `//ghost/browser/startup`, which can't depend on UI code. |

## For the spike

1. That `build_webui` and grit work from `//ghost`, outside `chrome/browser/resources`, and the result lands in a pak the browser loads.
2. That a `TopChromeWebUIConfig` registered from `ghost::BrowserMainExtraParts` serves the page and that the bubble manager preloads it.
3. How to draw the badge on a `ToolbarButton` (the extensions toolbar draws one).
4. That `RecordBlocked` sees the frame of a blocked subresource while its page is primary, for main frames, subframes and dedicated workers.

## Testing

**Unit** (`ghost_unittests`):
- `PageProtections` counts blocks from the main frame and subframes, ignores frames of another page, and resets on navigation.
- The panel's state: the site is the registrable domain; protections apply to http(s) only (not chrome://, file:, the new tab page); the mode's default is Standard, or Strict off the record.
- `SetLevel` at the mode's default clears the site's entry.

**Browser** (`ghost_browsertests`, with the `tracker.test` servers of 3A and 3D-2):
- The button exists; its badge counts the blocked requests of a page with trackers, in subframes too, and a blocked WebSocket.
- The count is back to zero after a navigation; a background tab doesn't change the active tab's badge.
- The bubble's page loads and shows the count. Clicking Off in it (script in the page) writes the pref, reloads the tab and turns the button to Off.
- On chrome://settings the button is disabled.
- Incognito's default is Strict, and the note is shown.
- The button's accessible name.

**Mutation checks**, each must fail: M1 the count isn't reset on navigation; M2 blocks are counted for the active tab, not their frame's; M3 `SetLevel` writes the mode's default instead of clearing it; M4 blocked WebSockets aren't counted.

**The egress audit** stays at no unexpected host: the page and its preloading make no network request.

**The look**, checked by the user in a dev build: light and dark themes, 100% and 150% scaling.

## Documentation

privacy-model.md (the panel; what the count counts, and that workers aren't in it), architecture.md (UI as built: the button, the bubble, the page, patches 0035–0037), testing.md, roadmap.md (3D-3 done), progress notes.

## Done when

- [ ] The tests pass and the four mutation checks fail as required.
- [ ] The egress audit finds no unexpected host.
- [ ] The user has approved the look.
- [ ] The documentation is updated; everything is committed (pushed when the user approves).

## Out of scope

- Settings: the default level, the GPC switch, campaign parameters (3D-4).
- A list of blocked domains, per-category counts, the score (Phase 8).
- Translations of Shade's strings.
- Breakage reports.
