# Privacy model

This document defines what Ghost does to protect privacy, the defaults in each mode, and, equally important, what it does not do. User-facing claims must be traceable to this document. Anything described here as a guarantee must be backed by a test.

Adversaries and security boundaries are in [threat-model.md](threat-model.md). Mechanisms are in [architecture.md](architecture.md).

## Principles

1. **Protected by default.** Protections work without configuration and without extensions.
2. **No fake features.** We don't ship a protection we can't test, and we don't describe a protection as stronger than it is.
3. **Explainable.** Every number we show a user can be traced to specific events and a published formula.
4. **Honest limits.** We say plainly that routing traffic through a proxy is not anonymity, that a small browser can't offer a large fingerprinting crowd, and that the OS can retain traces the browser can't erase.
5. **No data collection.** The browser sends the project nothing by default beyond what updates require, and that is documented byte for byte ([below](#data-the-browser-sends)).

## Modes

| | Normal | Private | Ghost |
|---|---|---|---|
| What it is | Everyday browsing; can be divided into identities | One shared off-the-record session (like Incognito) | Any number of disposable sessions, each isolated from all others |
| State persists | Yes | Until the last Private window closes | Until the session's last window closes |
| Isolated from Normal | — | Yes | Yes |
| Isolated from other sessions of the same mode | Identities from each other | No (one shared session) | Yes |
| Blocking level | Standard | Strict | Strict |
| Tracking parameters | Click identifiers | + campaign parameters | + campaign parameters |
| Fingerprinting | Standard tier | Strict tier | Strict tier, optional UTC/en-US |
| HTTPS | HTTPS-First (balanced) | HTTPS-First (strict) | HTTPS-First (strict) |
| Routing | Per identity, optional | Optional | Per session, optional |

**Ghost sessions do not hide your IP address** unless a route is enabled. Even with a route, they aren't an anonymity tool. The UI says so where sessions are started.

## Protections

### Blocking

- **Lists.** Network requests are matched against EasyList, EasyPrivacy and our own list. Our list covers first-party trackers, CNAME-cloaked trackers, and cases the community lists handle poorly.
- **Levels.**
  - *Standard* blocks third-party trackers and ads.
  - *Strict* also blocks first-party trackers, and applies cosmetic rules more aggressively.
  - *Off* disables blocking for the site. Per-site overrides are one click from the protections panel.
- **Breakage reports** are prepared on the user's machine and shown to the user before anything is sent. Nothing is reported automatically.

### Cookies and storage

- **Third-party cookies are blocked** in every mode.
  - Chromium's partitioned third-party storage and bounce-tracking mitigation stay on.
  - Sign-in flows that legitimately need cross-site access use the Storage Access API and FedCM, which Chromium supports.
- **Remaining Privacy Sandbox advertising APIs are disabled.**

### Tracking parameters

**Click identifiers** are per-visit or per-user values that link a click to a profile. They are removed from top-level navigations and redirects in every mode, before the request leaves the browser. Examples: `fbclid`, `gclid`, `dclid`, `msclkid`, `gbraid`, `wbraid`, `yclid`, `twclid`, `ttclid`, `igshid`, `mc_eid`, `_hsenc`. The list is maintained as a signed data component.

**Campaign parameters** (`utm_*`) describe a campaign, not a person. Removing them in Normal mode breaks publishers' attribution for little privacy gain, so:
- in Normal mode they're kept by default, with a setting to strip them;
- in Private and Ghost modes they're removed, since those modes minimize anything that could link visits.

The omnibox shows the cleaned URL.

### Connections and DNS

- **HTTPS-First with automatic upgrades.** Plain-HTTP navigations are upgraded, and the user is warned before loading a site that has no HTTPS.
- **DNS-over-HTTPS** uses upstream's "automatic" behavior: it upgrades to the configured resolver's DoH endpoint when one exists. A list of DoH providers is offered in settings. We don't silently send every user's DNS to one third-party provider.
- **Referrer** keeps Chromium's `strict-origin-when-cross-origin` default. The Strict level sends no referrer cross-site.
- **WebRTC** exposes only the default public interface. When a route is active it is limited to proxied connections, so it can't reveal the real IP.
- **Global Privacy Control** is sent: the `Sec-GPC` header and `navigator.globalPrivacyControl`. In some jurisdictions it is a legally meaningful opt-out.
- **Off by default:**
  - DNS prefetch;
  - preconnect and page preloading, which contact sites you haven't chosen to visit;
  - search suggestions, which send keystrokes to the search engine;
  - the password leak check, which sends Google hashes of each username and password the user signs in with.

### Permissions

- Powerful permissions (camera, microphone, location, notifications, clipboard, sensors and similar) default to one-time or session grants where Chromium supports it.
- Grants on sites you haven't used recently are revoked automatically, on a shorter schedule than upstream's.
- In Private and Ghost modes, grants never outlive the session.
- In Ghost mode, sensor and device APIs (USB, HID, serial, Bluetooth) are denied by default.

## Fingerprinting

### Randomization versus standardization

A fingerprint is the set of values a site can read that together distinguish one browser from another. There are two ways to defend against it.

- **Standardization** makes every browser report the same value, so the value carries no information. Tor Browser and Mullvad Browser use it. It works only if many users share the standardized values: *the crowd*.
- **Randomization** reports a different value each time, so the value can't be used to link visits. Done naively (new noise on every call), it fails:
  - repeated reads can be averaged to recover the real value;
  - the noise itself proves protection is active;
  - a value that changes between two reads in the same page is more distinctive, not less.

Our approach uses each where it works.

1. **Standardize wherever it doesn't break sites**, toward the values most common among Chromium-based browsers on the same OS. That is the largest crowd we can plausibly join.
2. **Use deterministic keyed noise only for hardware-derived readbacks**, which can't be standardized without breaking rendering: canvas pixels, WebGL pixels, audio output.
   - The noise is derived from `HMAC-SHA256(scope_key, eTLD+1 of the top-level site ‖ surface)` and the content being read.
   - The same site reading the same content gets the same answer every time, so averaging recovers nothing and nothing flickers.
   - Different sites get different answers, so the values can't link visits across sites.
   - `scope_key` is:
     - persistent per **identity** in Normal mode, so a site sees a stable device and fraud checks aren't triggered repeatedly;
     - per session in Private mode;
     - per session in Ghost mode.
3. **Enforce in the engine.** Values are changed in Blink's C++ code at the point they leave the engine, never by patching JavaScript objects.
4. **Consistency is a requirement.** A value must be identical across the main thread, dedicated, shared and service workers, iframes, and CSS media queries. Inconsistency is itself a fingerprint, and it's tested.
5. **No user-configurable "personas"** or arbitrary spoofed values. Every choice a user makes becomes a distinguishing bit.
6. **The User-Agent** keeps upstream's reduced string and the unbranded Chromium brand list. We don't add our product name: that would mark every request as coming from a tiny population.

### Surfaces

| Surface | Normal | Private | Ghost |
|---|---|---|---|
| Canvas 2D and OffscreenCanvas readback, WebGL `readPixels`, audio rendering output | keyed noise | keyed noise | keyed noise |
| System fonts visible to pages | Windows base-install allowlist | same | same |
| Local Font Access, WebGPU adapter details | upstream (permission-gated) | disabled / generic | disabled / generic |
| `hardwareConcurrency` / `deviceMemory` | bucketed to 4 or 8 / capped at 8 | same | same |
| WebGL unmasked renderer string | real | vendor-generic | vendor-generic |
| `screen.*`, available screen, matching media queries | real | nearest common resolution | nearest common resolution |
| Speech-synthesis voices, battery status, `navigator.languages` | real | standardized | standardized |
| Time zone and locale | real | real | real; optional UTC + en-US when routed |
| Sensors, WebUSB, WebHID, Web Serial, Web Bluetooth | upstream permission | denied by default | denied by default |

**Normal mode is deliberately conservative.** Its protections are the ones that break almost nothing. The stricter standardization in Private and Ghost modes can occasionally affect layout or media features, and per-site controls exist for that.

### Limitations

We state these wherever fingerprinting protection is described.

- **The crowd is small.** Standardizing toward common Chromium values removes most entropy, but other properties still separate our users from Chrome users:
  - the Extended Stable version number ([ADR 0003](adr/0003-upstream-extended-stable.md));
  - the unbranded brand list;
  - the presence of noise.
- **Noise is detectable.** A site can tell that protection is active. That reveals one bit ("uses a protecting browser"), not an identity.
- **Behavior and network signals are out of scope:** typing cadence, IP address, TLS characteristics.
- **New surfaces appear with each Chromium milestone.** Each milestone move includes a review of newly exposed APIs.

## Privacy report and score

The protections panel shows, for the current page load:
- trackers blocked;
- ads blocked;
- parameters removed;
- third-party cookies blocked;
- "fingerprinting scripts".

**Fingerprinting scripts** means the number of distinct third-party script origins that read three or more protected high-entropy surfaces during the page load, or that match the fingerprinting list. We don't claim to detect or block "attempts": reading a surface is not proof of intent.

**The score** (0–100) measures the *residual exposure on this page load under the current settings*. It isn't a verdict on the site.

Formula, version 1:

| Deduction | Points | Cap |
|---|---|---|
| Top-level page loaded over HTTP | −30 | — |
| Request to a known tracker that was allowed (by an exception or Off level) | −5 each | −25 |
| Third-party cookie that was allowed | −5 each | −15 |
| Read of a protected fingerprinting surface that current settings leave unprotected | −5 each | −20 |
| Mixed content or a form submitting over HTTP | −10 | — |

**Properties**
- **Deterministic:** the same events give the same score.
- **Explainable:** the panel lists each deduction that applied ("Why 72?").
- **Versioned:** changes to weights or categories get a new formula version and a changelog entry.
- **Not inflated by blocking:** blocked items are shown next to the score but don't raise it.
- **Scoped:** the panel states that the score can't assess what the site does with data on its servers.

**Harden this site** raises the site's protection level to Strict and enables the strict fingerprinting tier for it. The score shows the effect immediately.

## Identities and Ghost sessions

**Identities** separate first-party state: which accounts you are signed in to, and what sites have stored.
- Cross-site tracking is already addressed by the protections above in every identity.
- Identities are primarily a separation tool. That is how we describe them.
- What an identity isolates, and what is shared by design, is listed in [architecture.md](architecture.md#identities-phase-6). Both lists are enforced by tests.

**A Ghost session**
- is isolated from Normal browsing, from Private mode, and from every other Ghost session;
- keeps its cookies, storage, cache, permissions and history in memory;
- is discarded by the browser when it ends.

What a Ghost session does not do:
- hide your IP address without a route;
- remove files you downloaded;
- remove OS-level traces such as the page file, hibernation file, DNS client cache or crash dumps.

## Routing

- A route sends an identity's or session's traffic through a proxy that you choose.
- The proxy operator can see which sites you connect to. HTTPS still protects the content.
- Routes fail closed: if the proxy is down, pages don't load.
- While a route is active, WebRTC and DNS prefetching are constrained so they can't bypass it.

Routing through Tor is not planned before the public alpha. If added, it will be limited to Ghost sessions, and will state in the UI that it provides Tor's network anonymity **without** Tor Browser's protections against fingerprinting and correlation. People who need anonymity should use Tor Browser.

## Data the browser sends

By default the browser sends the project only the requests needed for updates:

- **Browser update checks.** Product, version, channel, OS version and architecture, and random request and session identifiers. The exact request is [below](#the-update-request).
  - No install ID, user ID or activity counters.
  - The "days since last check" style counters used by the Omaha protocol are not sent.
- **Component update checks** (filter lists, parameter lists, security data). Same properties.
- **Crash reports.** Never sent unless the user opts in. Before public alpha, uploads are disabled entirely.

The update server doesn't retain IP addresses.

### The update request

Ghost's updater speaks Omaha 4 (JSON). Before a request is sent, an allow-list is applied to the serialized JSON: a key that isn't listed is removed, including any key a later Chromium release adds.

| Object | Keys sent |
|---|---|
| `request` | `protocol`, `ismachine`, `acceptformat`, `sessionid`, `requestid`, `@updater`, `updaterversion`, `prodversion`, `updaterchannel`, `prodchannel`, `@os`, `arch`, `wow64`, `dlpref`, `os`, `apps` |
| `os` | `platform`, `arch`, `version` |
| each `app` | `appid`, `version`, `ap`, `brand`, `release_channel`, `enabled`, `disabled`, `cached_items`, `updatecheck`, `data` |
| `disabled` entries | `reason` |
| `cached_items` entries | `sha256` |
| `updatecheck` | `updatedisabled`, `rollback_allowed`, `sameversionupdate`, `targetversionprefix` |
| `data` entries | `name`, `index` |

**Removed:** the install ID (`iid`) and install date; the `ping` object with its activity and "days since" counters; hardware details (`hw`), language, domain membership and the OS service pack; the updater's own state (`updaters`); install sources, cohorts and installer attributes; and the text of `data` entries.

A real request, captured by the test update server when an installed browser checked for an update:

```json
{
  "request": {
    "@os": "win",
    "@updater": "ProjectGhostUpdater",
    "acceptformat": "crx3,download,puff,run,xz,zucc",
    "apps": [
      {
        "appid": "{c0ff4371-d9ab-461e-bffd-6b0dc2430b02}",
        "enabled": true,
        "updatecheck": {},
        "version": "152.0.7977.14901"
      }
    ],
    "arch": "x64",
    "ismachine": false,
    "os": {
      "arch": "x86_64",
      "platform": "Windows",
      "version": "10.0.26100.6690"
    },
    "prodversion": "152.0.7977.149",
    "protocol": "4.0",
    "requestid": "{51b17675-aa0e-430c-b6c8-22b84fa542a4}",
    "sessionid": "{a9bc7b01-6a3a-4186-9eee-fea79aed3f5b}",
    "updaterversion": "152.0.7977.149"
  }
}
```

The names and the app ID are the development identity; the release identity replaces them.

- **`requestid` and `sessionid` are random.** A new `requestid` is made for each request and a new `sessionid` for each update session (one check and the downloads it leads to). Neither is stored.
- **`version` is Ghost's release version; `prodversion` is the Chromium release** ([ADR 0007](adr/0007-version-numbers.md)).
- **No event requests are sent.** The Omaha protocol reports install and update results in separate "event" pings; Ghost's updater sends none.
- **Headers.** The request URL carries the response signing key's version and a hash of the request (CUP). The headers name the updater and the apps it checks: `User-Agent: ProjectGhostUpdater <version>`, `X-Goog-Update-Updater`, `X-Goog-Update-AppId` and `X-Goog-Update-Interactivity` (`fg` when a person asked for the check, `bg` otherwise). The `X-Goog-` names are the protocol's; the values are Ghost's.

**Third parties the browser may contact on its own:**
- the Chrome Web Store, when you install or update an extension from it (Google receives those requests);
- nothing else by default.

**The egress audit test enforces this.** It launches the packaged browser with a fresh profile, idles, visits a local page, then fills in an address and signs in on local pages. It fails if any host outside the reviewed allowlist is contacted. A browser test does the same for typing and searching in the omnibox.

## Claims policy

- User-facing text, marketing included, may claim only what this document states. A claim needs a test or a documented mechanism behind it.
- We don't use "anonymous", "untraceable", "invisible" or "military-grade".
- When a guarantee changes, this document, the relevant UI strings and the tests change in the same review.
