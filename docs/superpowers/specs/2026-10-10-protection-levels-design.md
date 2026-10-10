# Protection levels per site: design

- Status: done 2026-10-10 ([progress notes](2026-10-10-protection-levels-spike.md)), committed locally; design approved 2026-10-10 (the meaning of the levels by the user; the rest decided by Claude at the user's request, "choose the rest yourself", each decision with its reason below)
- Phase 3, sub-project 3D-1 ([roadmap](../../roadmap.md#phase-3-network-protections)); [privacy-model.md](../../privacy-model.md#blocking) defines the levels
- 3D is four sub-projects, in order (decided 2026-10-10): **3D-1** the per-site policy and the levels (this); **3D-2** WebSocket and WebTransport; **3D-3** the protections panel; **3D-4** settings (the default level, GPC, campaign parameters)

## Goal

Every protection Shade has reads one policy, resolved per site from the profile's mode and the user's choice for that site: Off, Standard or Strict. 3D-1 has no UI; the panel (3D-3) and settings (3D-4) are its controls.

## The levels now

Decided by the user: Strict is real today with what the engine and the lists provide, and grows in Phase 4 (cosmetic filtering, our own list) without changing its definition.

| | Off | Standard | Strict |
|---|---|---|---|
| Blocking (3A) | none | third parties only (requests to the page's own site aren't checked) | every request, the page's own site included: EasyPrivacy's first-party rules apply |
| Tracking parameters (3B) | not stripped | click identifiers (and campaign parameters if the profile's pref asks) | click identifiers and campaign parameters |
| Referrer | Chromium's | Chromium's `strict-origin-when-cross-origin` | no `Referer` header on cross-site requests |
| GPC, third-party cookies, HTTPS-First | unchanged by the level: they don't break sites | | |

**Defaults by mode** (privacy-model.md's table): a regular profile is Standard; Incognito, which becomes Private mode, is Strict. Incognito already strips campaign parameters and has strict HTTPS-First (3B, 3C); 3D-1 adds same-site blocking and the referrer rule.

## Decisions

Decided by Claude, 2026-10-10:

- **The site is the page's registrable domain** (`ghost::RegistrableDomain` of the top-level page), as everywhere since 3A: Off on `news.com` applies to everything a `news.com` page loads, whoever serves it.
- **Storage is a profile pref**, `ghost.privacy_policy.site_levels`: a dictionary from registrable domain to `"off"`, `"standard"` or `"strict"`; a site without an entry has the mode's default. Rejected: a new `ContentSettingsType` (a patch to Chromium's enum, its registry and its histogram tables, for pattern matching and site-settings UI that 3D doesn't use; the panel is ours). Not synced.
- **Incognito starts from the regular profile's choices**, and its own last for the session: Chromium's Incognito prefs read the regular profile's value until written, and a write stays in memory (the pref isn't on Incognito's write-through list). A site the user set to Off in Normal, because it broke, is Off in Incognito too.
- **A change applies at the page's next load**, as in Brave: the policy is read when a page's loaders are created and when a navigation starts. The panel (3D-3) reloads the page after a change.
- **Referrer at Strict means the header**: cross-site requests leave without `Referer`, for subresources (the request filter, 3A) and navigations (the query filter's throttle, 3B). The spike checks what `document.referrer` shows on the destination page; if the throttle can't reach it, the progress notes say so.
- **Patch 0029 changes**: the request filter needs the profile, so `MaybeProxyURLLoaderFactory` takes the `BrowserContext` `WillCreateURLLoaderFactory` already has. Amending 0029 replays only patches 0030–0033, small files.

## Components

### `//ghost/components/privacy_policy/` (no `//content`)

| Unit | What it does |
|---|---|
| `protection_level.{h,cc}` | `enum class ProtectionLevel { kOff, kStandard, kStrict }`, its pref string and parsing. |
| `effective_policy.{h,cc}` | `struct EffectivePolicy { bool block_requests; bool check_same_site; bool strip_click_identifiers; bool strip_campaign_parameters; bool cross_site_referrer; }`, `PolicyFor(level, campaign_pref)` (the table above), `ResolveLevel(mode_default, site_override)`. |

### `//ghost/browser/privacy_policy/`

| Unit | What it does |
|---|---|
| `site_levels.{h,cc}` | The pref, registered through `ghost::RegisterProfilePrefs` (3B's hook). `GetLevel(context, page_url)`, `SetLevel(context, page_url, level)`, `ClearLevel(context, page_url)`, `GetPolicy(context, page_url)`. The mode default: Strict for an Incognito profile, Standard otherwise. |

### Enforcement

- **Blocking:** `MaybeProxyURLLoaderFactory(context, isolation_info, builder)` reads the policy of the top frame's site: Off installs no filter; Standard checks third parties; Strict checks every request. The filter also drops `Referer` from cross-site requests when the policy says so.
- **Tracking parameters:** `MaybeCreateQueryFilterThrottle` reads the policy of the navigation's destination: Off creates no throttle; the scope follows the policy. At a cross-site navigation from a Strict page (the initiator's site), the throttle drops `Referer`.

## For the spike

1. That `WillStartRequest` can clear `request->referrer` for a navigation (the header leaves empty), and what `document.referrer` the destination sees.
2. That the top frame's origin is in the `IsolationInfo` for every factory the filter proxies (frames, workers, service workers); where it is absent, the regular profile's mode default applies.
3. That the Incognito pref overlay behaves as decided: reads fall through to the regular profile, writes stay in memory.

## Testing

**Unit** (`ghost_unittests`): `PolicyFor` for each level, with and without the campaign pref; `ResolveLevel`; the pref's parsing (an unknown string is ignored); `SetLevel` on a `TestingProfile` keys by registrable domain (`www.a.test` and `shop.a.test` are one site); Incognito's default is Strict, it inherits the regular profile's Off, and its own change doesn't reach the regular profile.

**Browser** (`ghost_browsertests`, `ProtectionLevelsBrowserTest`), with the request recorder of 3B's tests:
- Off: a tracker's script loads on an Off site; a link to an Off site keeps `fbclid`.
- Strict: a same-site script matching a rule without `$third-party` is blocked; `utm_source` is stripped in a regular profile; a cross-site image and a cross-site navigation from a Strict page leave without `Referer`, and a same-site request keeps it.
- Incognito is Strict by default (the same-site rule blocks).
- A level set while the page is open applies after a reload.

**Mutation checks**, each must fail: M1 blocking ignores Off; M2 Strict doesn't check same-site requests; M3 Strict keeps the cross-site referrer; M4 Incognito defaults to Standard.

**The egress audit** stays at no unexpected host.

## Documentation

privacy-model.md (the levels as they are now, Incognito's default, how a choice is kept), architecture.md (`privacy_policy`, the amended patch 0029), testing.md, roadmap.md (3D's four sub-projects; 3D-1 done), progress notes.

## Done when

- [x] The tests pass and the four mutation checks fail as required.
- [x] The egress audit finds no unexpected host.
- [ ] The documentation is updated; everything is committed (pushed when the user approves).

## Out of scope

- Any UI (3D-3, 3D-4); a default-level setting (3D-4).
- WebSocket and WebTransport (3D-2).
- Strict's growth in Phase 4 (cosmetic filtering, our own list).
- Per-identity policy (Phase 6).
