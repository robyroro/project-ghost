# Architecture

This document describes how Ghost is put together: where its code lives, how it attaches to Chromium, and which process each feature runs in.
- The reasoning behind the larger choices is in the [ADRs](adr/README.md).
- What the features promise users is in [privacy-model.md](privacy-model.md).
- What they defend against is in [threat-model.md](threat-model.md).

Status: Phase 0. Sections describing later phases are the design we build against, and each names its phase. Designs are revised by ADR when implementation teaches us something.

## Goals and non-goals

**Goals**
- Privacy protections are built into the browser, on by default, and independent of extensions.
- Identity isolation strong enough to hold several accounts on one site side by side, with a documented list of what is and isn't isolated.
- Disposable sessions whose state the browser discards when they end.
- Staying close enough to upstream Chromium that security releases ship within days.
- Windows 11 x64 first. Nothing in the design prevents macOS and Linux.

**Non-goals**
- Anonymity against network-level adversaries. That is Tor Browser's job ([threat-model.md](threat-model.md#non-goals)).
- A custom rendering or JavaScript engine, or changes to web-platform behavior beyond privacy protections.
- Features that need a project-operated server for the browser's core to work.

## Source layout

### The checkout

```
<root>/                   no spaces in the path; ideally a Dev Drive
  .gclient                written by tools/bootstrap.py: two solutions, src and src/ghost
  depot_tools/
  src/                    Chromium at CHROMIUM_COMMIT, the commit of tag CHROMIUM_VERSION
    ghost/                this repository  (= //ghost in GN labels)
```

Our repository is a gclient solution nested inside Chromium's tree. GN sees it as `//ghost`, so our targets depend on Chromium targets like any other directory. `src/ghost` is excluded from Chromium's `git status`.

### This repository

```
CHROMIUM_VERSION          the upstream tag; single source of truth
branding/                 product name, icons, app IDs, install modes, update URLs (Phase 1)
app/                      startup hooks, resources, pref/feature default overrides (Phase 1)
browser/                  glue to //chrome: ContentBrowserClient subclass, service factories,
                          identity & session lifecycle, UI (views/, webui/)
components/<feature>/     feature logic, layered as Chromium components:
  core/                   no //content dependency; pure logic; most unit tests live here
  common/                 mojom interfaces and types shared across processes
  browser/                browser-process side
  renderer/               renderer-process side
third_party/blink/renderer/ghost/   Blink-side helpers (fingerprinting); compiled into Blink
third_party/rust/         vendored crates with generated BUILD.gn
resources/                WebUI sources (TypeScript, Lit)
patches/                  the upstream patch series (ADR 0004)
test/                     browser tests, isolation contract suite, egress audit, fixtures
tools/                    bootstrap, environment check, patch and lint tooling
build/                    GN args, toolchain requirements, CI helpers
docs/                     this documentation
```

Planned components:

| Component | Responsibility | Phase |
|---|---|---|
| `components/blocking` | adblock-rust wrapper, list management, request and cosmetic filtering (the engine and the lists built in 3A) | 3–4 |
| `components/query_filter` | tracking-parameter rules (built in 3B) | 3 |
| `components/site` | what a site is (registrable domains), for every protection | 3 |
| `components/privacy_policy` | resolves mode × identity × site overrides into an effective policy (built in 3D-1: the levels and `EffectivePolicy`; identities in Phase 6) | 3 |
| `components/identity` | identity model, registry, domain rules | 6 |
| `components/fingerprinting` | policy types, per-site key derivation, mojom | 7 |
| `components/privacy_report` | per-page event accounting, score computation | 8 |
| `components/routing` | route model, proxy configuration, WebRTC and DNS coupling | 9 |

`DEPS` include rules enforce the layering: `core/` may not include `//content`, and components may not include `//chrome`. That keeps feature logic testable without a browser, and portable across platforms.

### Naming

- The C++ namespace is `ghost`. It's the engineering codename and stays even if the product is renamed.
- Features are named by function (`Identity`, `DisposableSession`, `Protections`), not by marketing names.
- User-visible strings, including "Ghost session", live in string resources under `branding/` and `app/`.

## How Ghost code reaches Chromium

The rule is in [ADR 0004](adr/0004-patch-strategy.md): use upstream extension points first. Where none exists, a minimal hook patch calls into `//ghost`.

**Extension points that need no patch beyond their one-time registration hook:**

| Mechanism | Used for |
|---|---|
| `ChromeBrowserMainExtraParts` | startup and shutdown of Ghost services |
| `BrowserContextKeyedServiceFactory` | per-profile services (identity registry) |
| `ContentBrowserClient::WillCreateURLLoaderFactory` | request interception for blocking and parameter stripping |
| `ContentBrowserClient::ConfigureNetworkContextParams` | per-partition proxy config for identities and sessions |
| `NavigationThrottle` | domain → identity rules on top-level navigations |
| `PrefRegistry::SetDefaultPrefValue` | privacy-preserving defaults for upstream prefs |
| variations extra feature overrides | disabling Google-service and ad-API features by default |
| `ui::ColorProvider` mixers | theming, identity colors, Ghost window treatment |

**Hook patches expected in Phase 1:**
- adding `//ghost` targets to `chrome` and the test targets to `gn_all`;
- selecting our branding directory, and compiling the installer strings it names the product in;
- adding our `ChromeBrowserMainExtraParts`;
- instantiating our `ContentBrowserClient` subclass;
- calling our pref and feature default registration;
- turning off Google services that no switch, pref or feature controls: GCM, the web-account list fetched from Google, Google as the fallback search engine, and search engines' remote New Tab pages.

Each hook is a few lines, and the series stays small enough to review in one sitting.

## Processes

```
┌──────────────── Browser process ─────────────────────────────────┐
│ PolicyResolver(mode, identity, site overrides)                   │
│   ├─ RequestFilter  (proxying URLLoaderFactory) ── adblock engine │
│   │                                               (own sequence) │
│   ├─ QueryFilter    (top-level navigations and redirects)        │
│   ├─ IdentityService / DisposableSessionManager                  │
│   ├─ FingerprintPolicy + per-site keys ─────────┐                │
│   └─ PageReport     (per-tab event accounting) ◄┼──────────┐     │
└──────────────┬──────────────────────────────────┼──────────┼─────┘
               │ one NetworkContext per           │ frame-   │ events
               │ StoragePartition                 │ associated  (untrusted,
┌──────────────▼───────────────┐  ┌───────────────▼─mojo─────┴─────┐
│ Network service              │  │ Renderer (sandboxed)            │
│ cookies, cache, HSTS, proxy  │  │ Blink supplements apply policy; │
│ per partition                │  │ cosmetic CSS, scriptlets        │
└──────────────────────────────┘  └─────────────────────────────────┘
```

- **The browser process decides all policy.** Renderers receive the effective policy for their frame and apply it. They never compute it. Anything a renderer reports, such as fingerprinting-API reads, is untrusted and used only for display.
- **Blocking runs in the browser process**, on a dedicated sequence, so one compiled engine serves all tabs. adblock-rust is memory-safe, which satisfies Chromium's Rule of 2 for parsing downloaded lists there.
- **Fingerprinting protections run in Blink C++.** They are delivered to frames and workers from the browser. We never patch JavaScript prototypes: prototype patching is detectable, leaks through iframes and workers, and is visible to `Function.prototype.toString`.

## Modes

| Mode | Chromium primitive | Lifetime | Defined in |
|---|---|---|---|
| Normal | regular profile; each identity a storage partition | persistent | [ADR 0005](adr/0005-isolation-primitives.md) |
| Private | primary off-the-record profile (Incognito) | until the last Private window closes | [ADR 0005](adr/0005-isolation-primitives.md) |
| Ghost | a **unique** off-the-record profile per session | until that session's last window closes | [ADR 0005](adr/0005-isolation-primitives.md) |

Protection defaults per mode are in [privacy-model.md](privacy-model.md#modes).

## Identities (Phase 6)

**Model** (`components/identity/core`)

`Identity { id: UUID, name, color, icon, history_policy: record | off, route, protection_level, fingerprint_key }`

- Identities are persisted in profile prefs by `IdentityRegistry`, a keyed service.
- The Default identity is the profile's default storage partition and can't be deleted.

**Isolation**
- A tab's identity is fixed when the tab is created. The tab's `SiteInstance` is bound to `StoragePartitionConfig("ghost-identity", <uuid>, in_memory=false)`.
- That partition's `NetworkContext` and storage backends make these per identity:
  - cookies;
  - localStorage and sessionStorage;
  - IndexedDB, Cache Storage and OPFS;
  - service workers;
  - HTTP cache, HSTS and the HTTP authentication cache;
  - socket pools and proxy configuration.
- **Permissions** for powerful capabilities are resolved per `(identity, origin)` from identity-scoped stores ([ADR 0005](adr/0005-isolation-primitives.md#decision)).
- **Switching identity** re-opens the URL in a new tab of the target identity. State never moves between partitions.
- **Session restore** stores each tab's identity through a session-command extension, which is a patch.

**Domain rules** (`Always open example.com in Work`)
- Evaluated by a pure function (exact host or eTLD+1, first match wins) and enforced by a `NavigationThrottle`.
- Applied only to user-initiated top-level navigations: typed URLs, bookmarks, links from other applications, new tabs.
- Not applied to redirects inside a flow. Re-routing an OAuth redirect into another identity would break sign-in.

**Shared by design**
- Bookmarks, passwords, autofill, extensions, the downloads list, favicons, and DNS-over-HTTPS settings.
- History when the identity's policy is `record`.

Each item is covered by a test asserting that it is shared, so a change in behavior is a visible test failure rather than a silent one.

**UI**
- A color marker on each tab.
- An identity chip in the omnibox: one click opens the switcher.
- "Reopen in identity" on the tab context menu.
- An identity menu on the new-tab button (two clicks to a new tab in any identity).

## Ghost sessions (Phase 5)

- `DisposableSessionManager` (`browser/disposable`) creates sessions as off-the-record profiles with unique `OTRProfileID`s. It tracks their windows, and ends a session when its last window closes.
- Chrome's existing off-the-record handling provides most of the guarantees: no history, in-memory storage and permissions, no autofill saving, extensions off unless allowed in private windows.
- **What we add:**
  - session-scoped fingerprinting keys and optional routes;
  - a distinct window treatment with an "End session" control;
  - an audit of Chrome UI that equates off-the-record with the primary Incognito profile.
- **Destruction is verified**, not assumed. A browser test ends a session, starts a new one, and asserts that no state is visible. It also diffs the user-data directory to show that the session wrote nothing outside an allowlist.

## Protections (Phases 3–4, 7)

- **Policy as built (3D-1).** `//ghost/components/privacy_policy` turns a protection level into an `EffectivePolicy` (pure); `//ghost/browser/privacy_policy` keeps per-site levels in a profile pref and answers `GetPolicy(context, page)`. The request filter asks per request through a callback bound to the profile's weak pointer (patch 0029 passes the `BrowserContext`); the query filter's throttle asks per navigation and per redirect. Per-site overrides are a pref, not content settings: no patch, and the panel is ours. [Design](superpowers/specs/2026-10-10-protection-levels-design.md), [progress notes](superpowers/specs/2026-10-10-protection-levels-spike.md).
- **Policy.** `PrivacyPolicyResolver` is a pure function of mode defaults, identity policy and per-site overrides. It produces an `EffectivePolicy` that every enforcement point reads. Per-site overrides are content settings, which requires new `ContentSettingsType` values (a patch).
- **Network blocking.** A proxying `URLLoaderFactory` sees navigations, subresources, and worker and service-worker fetches.
  - It asks the adblock engine for a verdict on a dedicated sequence and defers the request only until the verdict arrives.
  - WebSocket and WebTransport are covered by their own hooks (3D).
  - CNAME uncloaking matches on the DNS aliases the host resolver reports.
- **Network blocking as built (Phase 3A).** [Design](superpowers/specs/2026-10-10-blocking-engine-design.md), [progress notes](superpowers/specs/2026-10-10-blocking-engine-spike.md).
  - `//ghost/browser/blocking`: `RequestFilter`, appended first to every `URLLoaderFactory` through `WillCreateURLLoaderFactory` (patch 0029), so a blocked request reaches no other interceptor. Top-level documents and non-HTTP(S) URLs pass; a request to the page's own site (same registrable domain) starts at once but stays in the filter, which checks every redirect. A blocked request fails with `ERR_BLOCKED_BY_CLIENT`. `BlockingService`, one per browser, started in `PostCreateThreads` of Shade's `ChromeBrowserMainExtraParts`, reads the lists from `<version>\blocking\` (`base::DIR_ASSETS`).
  - `//ghost/components/blocking`: `BlockingEngine` owns the engine on a `USER_BLOCKING` sequence of its own. Checks made while it loads wait; if no list loads, it allows everything and logs it. Lists and strings that aren't UTF-8 never reach the bridge.
  - `components/blocking/rust/lib.rs`: the `cxx` bridge to adblock-rust 0.13.3, network rules only. adblock-rust asks C++ for registrable domains (`registry_controlled_domains`), so blocking and the browser agree on what a site is.
  - `//ghost/third_party/rust`: the crates Chromium lacks, vendored by `tools/rust_vendor.py`; Chromium's own crates for the rest (patch 0028). The lists ship in the installer (patch 0030); `about:credits` names the crates and the lists (patch 0031).
- **Parameter stripping.** The same interception point rewrites top-level navigations and redirects before they leave the browser, so the omnibox shows the cleaned URL. Which parameters are stripped in which mode is in [privacy-model.md](privacy-model.md#tracking-parameters).
- **Parameter stripping as built (Phase 3B).** Not the request filter's interception point after all, but a `blink::URLLoaderThrottle` on navigations, which Chromium lets change a navigation's URL: at the start (an internal redirect, so the original is never sent) and at each redirect, where the origin must stay, which removing query parameters guarantees. [Design](superpowers/specs/2026-10-10-query-filter-design.md), [progress notes](superpowers/specs/2026-10-10-query-filter-spike.md).
  - `//ghost/browser/query_filter`: `QueryFilterThrottle`, created for navigations of the outermost main frame by `ChromeContentBrowserClient::CreateURLLoaderThrottles` (patch 0032), first among the throttles, so Safe Browsing and the rest see the clean URL. The profile pref for campaign parameters, registered by the same patch.
  - `//ghost/components/query_filter`: the list, compiled in by `tools/embed_text.py`, its parser, `Strip()` and the decisions on where a URL comes from. No `//content`.
  - `//ghost/components/site`: `RegistrableDomain()`, the one definition of a site, shared with 3A's blocking.
- **Cosmetic filtering and scriptlets** (Phase 4).
  - At navigation commit, the browser computes each page's hiding rules and scriptlets.
  - It sends them over a frame-associated mojo interface.
  - The renderer inserts a user-level stylesheet and runs scriptlets at document start.
- **Lists and other data.** Filter lists, parameter lists and our own list are components delivered by Chromium's component updater from our update server ([Updates](#updates-and-signed-data)).
- **Fingerprinting** (Phase 7).
  - Blink supplements on `ExecutionContext` hold the effective fingerprint policy and the per-site noise key. Both are pushed from the browser for frames, dedicated workers, shared workers and service workers.
  - Surfaces call a single helper at the point where a value leaves the engine.

## Privacy report (Phase 8)

`PageReport` (per tab, reset on each navigation) counts events from trusted browser-side sources:
- blocked and allowed tracker requests;
- stripped parameters;
- blocked and allowed third-party cookies;
- HTTPS upgrades.

It also counts events from the renderer (untrusted): reads of protected fingerprinting surfaces, attributed to script origins.

`components/privacy_report/core` computes the score from these counts with a versioned, published formula ([privacy-model.md](privacy-model.md#privacy-report-and-score)). The panel shows the score, the counts, and each deduction that produced the score.

## Routing (Phase 9)

- A route is `direct`, `http(s)://host:port` or `socks5://host:port`, optionally with credentials.
- Routes are attached to an identity or a Ghost session and applied through `ConfigureNetworkContextParams`, as a fixed proxy configuration with **no direct fallback**. If the proxy is unreachable, requests fail.
- **While a route is active:**
  - WebRTC is limited to proxied connections for that identity's tabs;
  - DNS prefetch and preconnect are disabled for it, because they resolve names outside the proxy.
- Chromium doesn't support SOCKS5 username/password authentication. Supporting it is a `//net` patch in this phase.
- Tor and VPN integration are out of scope until after the public alpha. Tor, if added, is limited to Ghost sessions, and its UI says it isn't Tor Browser ([privacy-model.md](privacy-model.md#routing)).

## UI

**Chromium's two UI technologies**
- Views (C++) for the browser frame, tab strip, toolbar and omnibox.
- WebUI (TypeScript and Lit, served from inside the browser) for settings, the protections panel, the privacy report and internal pages.

We follow the same split. Bubbles that show rich content, like the protections panel, are WebUI hosted in a Views bubble.

**Visual system**
- Colors come from a `GhostColorMixer` in Chromium's color pipeline: semantic tokens, with light, dark and high-contrast variants. No colors are hard-coded in views.
- The design language is restrained: native-feeling controls, no decorative gradients or translucency.
- Identity is shown with one consistent color marker, not new chrome.
- Ghost windows have a distinct but quiet frame treatment, so they're recognizable at a glance without shouting.

**Density and scaling**
- Layouts are verified at 100%, 125%, 150% and 200% scaling, on 1080p and 1440p displays.

## Updates and signed data

The browser trusts two kinds of downloaded input:
- **browser updates**, via Chromium's `//chrome/updater`, branded, speaking the Omaha protocol to our update server;
- **data components**, via Chromium's component updater, from the same server: filter lists, tracking-parameter lists, our own lists, and mirrored security components such as CRLSet and certificate transparency data.

**Integrity**
- Installers are Authenticode-signed.
- Update responses are signed (CUP).
- Components are CRX3-signed.
- Clients reject anything else.

**Request contents**
- Update requests carry no stable identifiers. Details are in [privacy-model.md](privacy-model.md#the-update-request).

### The updater as built (Phase 2)

Ghost uses Chrome's install model: a small metainstaller installs the updater, and the updater installs and updates the browser. Installs are per-user. The identity is a test identity until the final name; every value is in `branding/updater.gni` and `branding/install_modes.h`.

**Layout.** The browser and the updater share a company directory, so neither one's uninstall removes the other's state:

| What | Where |
|---|---|
| Browser | `%LOCALAPPDATA%\Shade\Browser\Application` |
| Updater | `%LOCALAPPDATA%\Shade\ShadeUpdater\<version>\updater.exe`, with a scheduled task |
| Browser settings | `HKCU\Software\Shade\Browser` |
| Registration | `HKCU\Software\Shade\Update\Clients\{appid}` (`pv`, the installed release version) and `ClientState\{appid}` |

The company directory has no space: the updater's uninstall script, which is upstream's, can't handle one.

- **Registration.** Setup registers the browser under the updater's key when it installs or updates (`USE_GOOGLE_UPDATE_INTEGRATION`, with Ghost's key path). The browser registers its release version with the updater at runtime.
- **Uninstall.** Removing the browser removes its registration. At its next run the updater finds no app, uninstalls itself and deletes its key.

**Trust chain.**
- **Responses: CUP with Ghost's key.** Each update response is signed with ECDSA P-256 over the request hash and the response body. The client holds only Ghost's public key (`branding/cup_key.h`); upstream's keys stay compiled in but unused, so a key rotation stays a one-line change.
- **Packages: Ghost's publisher proof.** Updates are CRX3 files. The updater accepts only `CRX3_WITH_GHOST_PUBLISHER_PROOF`, a format Ghost adds: the package must carry a signature by a key pinned in `branding/crx_publisher_key.h`. The Chrome Web Store's format is unchanged for extensions.
- **Two publisher keys.** The updater accepts a proof by the identity's primary or its backup (`branding/keys/<identity>.h`, chosen by `ghost_signing_identity`). The primary is in a TPM, the backup offline ([signing](signing/README.md)).
- **Staged rollout.** The server holds a candidate release beside the active one and offers it to a fraction of update checks, drawn per check because requests carry no identifier; a fraction of 0 halts it ([release.md](build/release.md#rolling-out)).
- **The package's hash** comes from the signed response and is checked before the package is opened.

**The request scrubber** (`components/update_client/request_scrubber.cc`) applies the allow-list to the serialized JSON just before it's sent, so a key that upstream adds later is dropped too. `update_client` sends no event requests. Both sit in `update_client`, which the browser's component updater also uses.

**Testing.** `tools/update_server.py` is a test Omaha server that signs with the test CUP key and serves a CRX3 signed with the test publisher key; both private keys are committed in `test/updater/` and marked test-only. `tools/update_smoke.py` installs from the offline installer in Windows Sandbox, applies an update, checks every request against the allow-list, and uninstalls. Signed releases, the key custody and the recovery drill are described in [signing](signing/README.md).

The release process and SLA are in [roadmap.md](roadmap.md#security-release-sla).

## Services boundary

The browser core never requires a project-operated service to browse, block, isolate or protect. Optional services are separate products with separate repositories. The browser's clients for them are off unless the user enables them.

Services currently envisaged:
- encrypted sync;
- managed routes;
- team identity management.

No privacy or security feature is limited in the free browser to create a reason to subscribe.

## Portability

Windows 11 x64 is the only build target until Phase 9 ([roadmap.md](roadmap.md)). To keep macOS and Linux possible:
- Platform-specific code lives in `*_win.cc` files behind interfaces, as Chromium does.
- Component `core/` code uses only `//base` and `//url`.
- Branding and installer integration are isolated in `branding/` and `chrome/installer` hooks.

## Testing isolation

The isolation contract suite is a table:
- **Rows:** every storage mechanism — cookies, localStorage, sessionStorage, IndexedDB, Cache Storage, OPFS, service workers, HTTP cache (observed as server hit counts), HSTS, HTTP auth cache, permissions, `:visited`, BroadcastChannel, SharedWorker, Web Locks.
- **Columns:** every boundary — identity↔identity, identity↔default, Private↔Normal, Ghost↔Ghost, Ghost↔Normal.
- **Cells:** each is either *isolated* or *shared by design*, and a browser test proves it.

Adding a mode or a storage mechanism means adding its row or column. A milestone move that introduces a new storage API isn't finished until the API is classified.

The full testing strategy is in [testing.md](testing.md).

## Open design questions

Each is tied to the phase that resolves it:

| Question | Resolved by |
|---|---|
| Patch surface for per-identity basic content settings | Phase 6 spike; fallback defined in [ADR 0005](adr/0005-isolation-primitives.md) |
| Chrome UI call sites that equate off-the-record with Incognito | Phase 5 audit |
| Exact interception point for parameter stripping on redirects | Resolved in 3B: a navigation `URLLoaderThrottle` (`WillStartRequest`, `WillRedirectRequest`) |
| Serialization format for the cached compiled blocking engine | 3E (list updates as components); 3A compiles the lists at each start, in about 80 ms |
