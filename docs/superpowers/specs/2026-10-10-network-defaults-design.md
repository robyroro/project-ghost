# Network defaults: design

- Status: design approved 2026-10-10
- Phase 3, sub-project 3C ([roadmap](../../roadmap.md#phase-3-network-protections)); what it enforces is in [privacy-model.md](../../privacy-model.md#cookies-and-storage) and [Connections and DNS](../../privacy-model.md#connections-and-dns)
- Follows 3B, [tracking-parameter stripping](2026-10-10-query-filter-design.md)

## Goal

Every promise of privacy-model.md's "Cookies and storage" and "Connections and DNS" holds in Shade by default and is enforced by a test of the behavior: the three defaults still missing are set, and the ones already in place (from Phase 1 or from upstream) are proven.

## Decisions

Settled in discussion on 2026-10-10:

- **Three defaults change; everything else is proven.** By the 2026-10-10 survey of Chromium 152:

  | Promise | State before 3C |
  |---|---|
  | Third-party cookies blocked | Phase 1 (`kCookieControlsMode`), tested by setting only |
  | Privacy Sandbox advertising APIs off (Topics, Protected Audience, ad measurement) | Upstream: the `privacy_sandbox.m1.*_enabled` prefs default to `false`; untested |
  | **Related Website Sets** | **On upstream**: listed groups of sites get cross-site cookie access, against "third-party cookies blocked in every mode" |
  | Bounce-tracking mitigation stays on | Upstream: `DIPS` enabled; untested |
  | HTTPS-First, balanced in Normal | Phase 1 (`kHttpsFirstBalancedModeAutoEnable`), tested |
  | Strict HTTPS-First in Incognito (Private mode's promise) | Upstream: `https_first_mode_incognito_enabled` defaults to `true` and `kHttpsFirstModeIncognito` is enabled; untested |
  | DNS-over-HTTPS "automatic", upstream's providers | Upstream default; untested |
  | `strict-origin-when-cross-origin` referrer | Upstream default; untested |
  | **WebRTC limited to the default public interface** | **Not set**: upstream's policy is `default` |
  | **Global Privacy Control** | **Not set**: Chromium 152 implements `Sec-GPC` and `navigator.globalPrivacyControl` behind `blink::features::kGlobalPrivacyControlForce`, off |
  | DNS prefetch, preconnect, preloading off | Phase 1 (`kNetworkPredictionOptions`), tested by setting only |

- **Strict HTTPS-First in Incognito now**, as Private mode will have it (Phase 5): upstream already does it, so 3C proves it.
- **The mechanisms are Phase 1's** (approach A of three): a pref default in `browser/prefs/pref_defaults.cc` (patch 0002's hook), a feature override in `browser/features/feature_overrides.cc` (patch 0003's). No new patch; the user can change each in settings. Rejected: enterprise policies (locked values, and settings would say "managed by your organization") and patches to upstream's defaults (patch surface for nothing).

## The changes

| What | Where | Value |
|---|---|---|
| Global Privacy Control | `feature_overrides.cc` | `blink::features::kGlobalPrivacyControlForce` enabled: `Sec-GPC: 1` on every request, `navigator.globalPrivacyControl` true in pages and workers. A legally meaningful opt-out in some jurisdictions (California, Colorado). |
| WebRTC | `pref_defaults.cc` | `prefs::kWebRTCIPHandlingPolicy` = `blink::kWebRTCIPHandlingDefaultPublicInterfaceOnly`: only the interface the system routes through by default, so WebRTC reveals no other address (a VPN's physical interface, other networks); calls still work. |
| Related Website Sets | `pref_defaults.cc` | `prefs::kPrivacySandboxRelatedWebsiteSetsEnabled` = `false`. Sign-in flows that need cross-site access use the Storage Access API, which asks. |

## For the spike

Before the rest:

1. That `kGlobalPrivacyControlForce` alone sends the header on navigations and subresources and exposes the property in workers (`content/browser/global_privacy_control_browsertest.cc` shows upstream's own checks).
2. That `pref_defaults.cc` can change `kWebRTCIPHandlingPolicy` and `kPrivacySandboxRelatedWebsiteSetsEnabled`: both must be registered inside chrome's `RegisterProfilePrefs()`, not by a keyed service later (pref_defaults.h explains the constraint).
3. `DIPS`'s triggering action at 152 and how a test reads it.
4. The test helper for HTTPS-First's warning page, and an HTTP-only host for it (the embedded server without HTTPS).
5. That a test server's connection listener sees a preconnect (the test is negative, so it must be shown to be able to fail: with `kNetworkPredictionOptions` at upstream's default, mutation check M4).

## Testing

**Browser tests**, `test/network_defaults_browsertest.cc` in `ghost_browsertests`, checking behavior where it can be observed and Chromium's own decision API where it can't:

| Promise | Test |
|---|---|
| GPC | The embedded server receives `Sec-GPC: 1` on a navigation and on a subresource; `navigator.globalPrivacyControl` is true in a page and in a worker. |
| WebRTC | A tab's renderer preferences, which WebRTC reads, carry `default_public_interface_only`, in Normal and Incognito. |
| Related Website Sets | `PrivacySandboxSettings::AreRelatedWebsiteSetsEnabled()` is false. |
| Privacy Sandbox | `IsTopicsAllowed()`, `IsFledgeAllowed()` and `IsAttributionReportingEverAllowed()` are false; `document.browsingTopics()` rejects in a page. |
| Third-party cookies | A third-party frame sets a cookie; its next request arrives without it. |
| Bounce-tracking mitigation | `DIPS` is enabled with a triggering action other than none, and the profile has its service. |
| HTTPS-First | Normal: balanced (existing test). Incognito: an HTTP-only site shows the HTTPS-First warning instead of loading. |
| DoH | The effective secure-DNS mode (`StubResolverConfigReader`) is automatic. |
| Referrer | A cross-site subresource receives only the origin as `Referer`. |
| Preconnect, DNS prefetch, speculation rules | A page links `rel=preconnect` and `rel=dns-prefetch` to a second test server and asks speculation rules to prefetch and prerender a page on it: that server accepts no connection and receives no request. |

**Mutation checks**, each must fail:
- M1: GPC not forced → the GPC test fails;
- M2: WebRTC back to `default` → the WebRTC test fails;
- M3: Related Website Sets on → its test fails;
- M4: `kNetworkPredictionOptions` at upstream's default → the preconnect test fails.

**The egress audit** stays at no unexpected host.

## Documentation

[privacy-model.md](../../privacy-model.md) (each promise of the two sections with the test that enforces it; Related Website Sets off; strict HTTPS-First in Incognito; WebRTC's exact policy), [testing.md](../../testing.md) (the suite), [roadmap.md](../../roadmap.md) (3C done), progress notes.

## Done when

- [ ] The suite passes and the four mutation checks fail as required.
- [ ] The egress audit finds no unexpected host.
- [ ] The documentation is updated; everything is pushed with tooling CI green.

## Out of scope

- Switches for these settings: all but GPC are in Chromium's settings already; GPC's switch comes with 3D's panel (meanwhile `--disable-features=GlobalPrivacyControlForce`).
- WebRTC limited to proxied connections while a route is active (Phase 9).
- The Strict level's referrer (none cross-site): 3D.
- A DoH provider list of our own: upstream's stays.
