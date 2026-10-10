# Protection Levels Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Blocking, tracking-parameter stripping and the referrer follow a per-site level (Off, Standard, Strict) resolved from the profile's mode and the user's choice ([spec](../specs/2026-10-10-protection-levels-design.md)).

**Architecture:** `//ghost/components/privacy_policy` resolves a level into an `EffectivePolicy` (pure). `//ghost/browser/privacy_policy` stores per-site choices in a profile pref and answers `GetPolicy(context, page)`. The request filter asks per request, through a callback bound to the profile's weak pointer; the query filter's throttle asks per navigation. Patch 0029 passes the `BrowserContext` to the filter.

**Tech Stack:** C++ (Chromium 152.0.7977.158), gtest unit and browser tests.

**Conventions:** as 3A–3C (webops first, `sync.py`, `out/vanilla`, no trailers; Chromium commits signed off with `Why:`/`Upstream:`). **Commits stay local until the user approves the push.**

**Settled while planning:** `content::BrowserContext::GetWeakPtr()` exists; the filter resolves the page per request (`RequestFilter::SourceOf`), so the policy is resolved per request too; the referrer rule compares the request's `referrer` with its URL (what the header would reveal), and sets `referrer_policy = NO_REFERRER` so redirects carry none either.

---

### Task 1: The pure policy

**Files:** `components/privacy_policy/{protection_level,effective_policy}.{h,cc}`, `components/privacy_policy/{protection_level,effective_policy}_unittest.cc`, `components/privacy_policy/BUILD.gn`; root `BUILD.gn` (`ghost_unittests` deps).

```cpp
enum class ProtectionLevel { kOff, kStandard, kStrict };
std::string_view ToPrefString(ProtectionLevel level);          // "off", "standard", "strict"
std::optional<ProtectionLevel> ParseProtectionLevel(std::string_view text);

struct EffectivePolicy {
  bool block_requests = true;
  bool check_same_site = false;
  bool strip_click_identifiers = true;
  bool strip_campaign_parameters = false;
  bool cross_site_referrer = true;
};
EffectivePolicy PolicyFor(ProtectionLevel level, bool campaign_pref);
ProtectionLevel ResolveLevel(ProtectionLevel mode_default,
                             std::optional<ProtectionLevel> site_override);
```

Tests first: each level's policy (Off: nothing; Standard: block, click, campaign only with the pref, referrer kept; Strict: everything); `ResolveLevel` prefers the override; parsing round-trips and rejects `"OFF"`, `""`, `"strictest"`. Commit: `privacy_policy: protection levels and the policy each gives`.

### Task 2: Per-site storage

**Files:** `browser/privacy_policy/site_levels.{h,cc}`, `site_levels_unittest.cc`, `BUILD.gn`; `browser/prefs/profile_prefs.cc` (registers the pref); root `BUILD.gn`.

```cpp
inline constexpr char kSiteLevelsPref[] = "ghost.privacy_policy.site_levels";
void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);  // a dictionary, not synced
ProtectionLevel ModeDefault(content::BrowserContext* context);          // Strict off the record
std::optional<ProtectionLevel> GetSiteLevel(content::BrowserContext* context, const GURL& page);
ProtectionLevel GetLevel(content::BrowserContext* context, const GURL& page);
void SetLevel(content::BrowserContext* context, const GURL& page, ProtectionLevel level);
void ClearLevel(content::BrowserContext* context, const GURL& page);
EffectivePolicy GetPolicy(content::BrowserContext* context, const GURL& page);
```

Keyed by `ghost::RegistrableDomain(page.host())`; a page without a host has the mode default. `GetPolicy` passes 3B's campaign pref. Tests on `TestingProfile` and its Incognito profile: keying, defaults, inheritance, a write in Incognito not reaching the regular profile, an unknown stored string ignored. Commit: `privacy_policy: per-site levels in a profile pref`.

### Task 3: Blocking follows the level

**Files:** `browser/blocking/request_filter.{h,cc}`, `browser/blocking/BUILD.gn`; Chromium `chrome/browser/chrome_content_browser_client.cc` (patch 0029); tests in `browser/privacy_policy/protection_levels_browsertest.cc`.

- `MaybeProxyURLLoaderFactory(content::BrowserContext*, const net::IsolationInfo&, network::URLLoaderFactoryBuilder&)` binds `PolicyCallback = base::RepeatingCallback<privacy_policy::EffectivePolicy(const GURL& page)>` to the context's weak pointer (a gone profile: the Standard policy).
- `CreateLoaderAndStart`: resolves the source page, then its policy. Off: pass the request on untouched. `!cross_site_referrer` and a referrer on another site than the URL: copy the request, clear `referrer`, set `referrer_policy = NO_REFERRER`. `InFlight` receives `check_same_site`, used by `NeedsVerdict(request, url, source, check_same_site)` at the start and at each redirect.
- Patch 0029 amended: `ghost::blocking::MaybeProxyURLLoaderFactory(browser_context, isolation_info, factory_builder);` (a fixup commit, `rebase --autosquash` with `GIT_SEQUENCE_EDITOR=:`), export, `check`: only 0029 changes.

Browser tests first (fixture as 3A's: hosts mapped to the embedded server, a request recorder, a test list `||tracker.test^$third-party\n/own.js\n`):
`AnOffSiteLoadsTrackers`, `StrictBlocksTheSitesOwnTrackers`, `StandardLeavesTheSitesOwnRequests`, `StrictSendsNoCrossSiteReferrer` (a cross-site image: no `Referer`; a same-site image: `Referer` kept), `IncognitoIsStrictByDefault`, `ALevelAppliesAtTheNextLoad`. Commit: `blocking: the request filter follows the page's protection level`.

### Task 4: Tracking parameters and navigations follow the level

**Files:** `browser/query_filter/query_filter_throttle.{h,cc}`, `BUILD.gn`; tests.

- `MaybeCreateQueryFilterThrottle`: the destination's policy: Off → no throttle (unless the initiator's policy drops the referrer); scope from `strip_campaign_parameters`.
- The throttle takes `drop_referrer` = the initiator's site is Strict and the navigation is cross-site; `WillStartRequest` clears `request->referrer` and sets `referrer_policy = NO_REFERRER`.

Tests first: `AnOffSiteKeepsItsParameters`, `StrictStripsCampaignParametersInNormal`, `AStrictPageNavigatesWithoutReferrer` (the server sees no `Referer`; record `document.referrer` on the destination for the notes). 3B's tests keep passing (Incognito's campaign stripping now comes from Strict). Commit: `query_filter: the destination's level decides, and Strict pages send no referrer`.

### Task 5: Mutation checks, audit, docs

- [ ] M1 blocking ignores Off; M2 Strict doesn't check same-site; M3 Strict keeps the cross-site referrer (both enforcement points); M4 Incognito defaults to Standard. Each made, failing, undone with `sync.py`.
- [ ] Full suites, egress audit.
- [ ] Docs: privacy-model.md (the levels as built, defaults, storage, Incognito inheritance, referrer scope), architecture.md, testing.md, roadmap.md (3D split; 3D-1 done), progress notes, spec and plan status. Commit locally; **don't push** until the user approves.
