# Network Defaults Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every promise of privacy-model.md's "Cookies and storage" and "Connections and DNS" holds by default and is enforced by a behavior test ([spec](../specs/2026-10-10-network-defaults-design.md)).

**Architecture:** Three defaults through Phase 1's hooks: `kGlobalPrivacyControlForce` in `browser/features/feature_overrides.cc` (patch 0003), the WebRTC IP-handling policy and Related Website Sets in `browser/prefs/pref_defaults.cc` (patch 0002). One browser-test suite, `test/network_defaults_browsertest.cc`, proves each promise, new or old. No new patch.

**Status:** done 2026-10-10. What differed (strict Incognito needed code; Related Website Sets needed none; the mutation checks) is in the [progress notes](../specs/2026-10-10-network-defaults-spike.md).

**Tech Stack:** C++ browser tests (`InProcessBrowserTest`, `EmbeddedTestServer` over HTTP and HTTPS with `CERT_TEST_NAMES`, a connection listener), Chromium 152.0.7977.158.

**Conventions (from 3A/3B):** edit in webops, copy with the scratchpad's `sync.py`, build in `out/vanilla` (`autoninja -C out\vanilla ghost_browsertests`), never edit `src` while a build runs; webops commits without trailers; mutation checks undone by `sync.py`.

**Settled while planning (from the sources at 152):**
- `kWebRTCIPHandlingPolicy` (`RegisterBrowserUserPrefs`) and `kPrivacySandboxRelatedWebsiteSetsEnabled` (`privacy_sandbox::RegisterProfilePrefs`) are registered inside chrome's `RegisterProfilePrefs()`, before `ghost::OverrideProfilePrefDefaults()`: their defaults can be changed there.
- The renderer reads WebRTC's policy from `blink::RendererPreferences::webrtc_ip_handling_policy`, a `blink::mojom::WebRtcIpHandlingPolicy`.
- `PrivacySandboxSettings` has `IsTopicsAllowed()`, `IsFledgeAllowed(top_frame, party, operation)`, `AreRelatedWebsiteSetsEnabled()`; ad measurement has no settings method at 152, so its decision is the pref `prefs::kPrivacySandboxM1AdMeasurementEnabled`. `document.browsingTopics()` would reject in a test page whatever the settings (no attestation), so it can't be a test.
- `DIPS` (`features::kBtm`) is enabled with `kBtmTriggeringAction` defaulting to `kBounce`; `content::BtmService::Get(context)`.
- Page hints (`<link rel=preconnect>`, `rel=dns-prefetch`) reach `PreconnectManagerImpl`, which checks `IsPreconnectEnabled()` (the preloading pref) before preconnecting **and** before resolving. A preconnect is observable (a socket); a DNS resolution isn't, so the test observes preconnect and the notes say the resolution shares its gate.
- HTTPS-First upgrades in tests go to `HttpsUpgradesInterceptor::SetHttpsPortForTesting`'s port; pointing it at a port without TLS makes a site HTTP-only.

---

### Task 1: The suite and Global Privacy Control

**Files:** Create `test/network_defaults_browsertest.cc`; modify root `BUILD.gn` (`ghost_browsertests` sources and deps), `browser/features/feature_overrides.cc`, `browser/features/BUILD.gn` (if `//third_party/blink/public/common` is missing).

- [x] **Step 1: The suite with its fixture and the GPC tests.** The fixture maps every host to the embedded servers, records each request's host, path and headers, and serves `/worker.js` (posts `navigator.globalPrivacyControl`), `/img` (a GIF) and pages.

```cpp
IN_PROC_BROWSER_TEST_F(NetworkDefaultsBrowserTest, GpcIsSentOnNavigationsAndSubresources) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/page")));
  EXPECT_EQ(Header("a.test", "/page", "Sec-GPC"), "1");
  ASSERT_TRUE(content::ExecJs(Tab(), content::JsReplace(
      "new Promise(done => { const i = new Image(); i.onload = i.onerror = done; i.src = $1; })",
      Url("b.test", "/img"))));
  EXPECT_EQ(Header("b.test", "/img", "Sec-GPC"), "1");
}

IN_PROC_BROWSER_TEST_F(NetworkDefaultsBrowserTest, GpcIsVisibleToPagesAndWorkers) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/page")));
  EXPECT_EQ(content::EvalJs(Tab(), "navigator.globalPrivacyControl"), true);
  EXPECT_EQ(content::EvalJs(Tab(),
                            "new Promise(done => { const w = new Worker('/worker.js');"
                            " w.onmessage = e => done(e.data); })"),
            true);
}
```

- [x] **Step 2:** Build, run `--gtest_filter=NetworkDefaults*`: both fail (no header; the property is false).
- [x] **Step 3:** `feature_overrides.cc` appends:

```cpp
  // Global Privacy Control: Sec-GPC on every request and
  // navigator.globalPrivacyControl in pages and workers, an opt-out of sale
  // and sharing that some jurisdictions make binding (California, Colorado).
  // Chromium implements it and leaves it off; this feature forces it on.
  overrides->emplace_back(std::cref(blink::features::kGlobalPrivacyControlForce),
                          base::FeatureList::OVERRIDE_ENABLE_FEATURE);
```

with `#include "third_party/blink/public/common/features.h"`.
- [x] **Step 4:** Both pass. Commit: `features: Global Privacy Control on by default`.

### Task 2: WebRTC and Related Website Sets

**Files:** Modify `test/network_defaults_browsertest.cc`, `browser/prefs/pref_defaults.cc`, `browser/prefs/BUILD.gn`.

- [x] **Step 1: Tests first.**

```cpp
IN_PROC_BROWSER_TEST_F(NetworkDefaultsBrowserTest, WebRtcUsesOnlyTheDefaultPublicInterface) {
  for (Browser* b : {browser(), CreateIncognitoBrowser()}) {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(b, Url("a.test", "/page")));
    EXPECT_EQ(Tab(b)->GetMutableRendererPrefs()->webrtc_ip_handling_policy,
              blink::mojom::WebRtcIpHandlingPolicy::kDefaultPublicInterfaceOnly);
  }
}

IN_PROC_BROWSER_TEST_F(NetworkDefaultsBrowserTest, RelatedWebsiteSetsAreOff) {
  EXPECT_FALSE(PrivacySandboxSettingsFactory::GetForProfile(GetProfile())
                   ->AreRelatedWebsiteSetsEnabled());
}
```

- [x] **Step 2:** Both fail.
- [x] **Step 3:** `pref_defaults.cc` appends:

```cpp
  // WebRTC uses only the interface the system routes through by default, so
  // it reveals no other address (a VPN's physical interface, other networks).
  registry->SetDefaultPrefValue(
      prefs::kWebRTCIPHandlingPolicy,
      base::Value(blink::kWebRTCIPHandlingDefaultPublicInterfaceOnly));

  // Related Website Sets give Google-listed groups of sites cross-site cookie
  // access, against blocking third-party cookies in every mode. Sign-in flows
  // that need it use the Storage Access API, which asks.
  registry->SetDefaultPrefValue(prefs::kPrivacySandboxRelatedWebsiteSetsEnabled,
                                base::Value(false));
```

with `#include "components/privacy_sandbox/privacy_sandbox_prefs.h"` and `#include "third_party/blink/public/common/peerconnection/webrtc_ip_handling_policy.h"`; BUILD deps `//components/privacy_sandbox:privacy_sandbox_prefs` and `//third_party/blink/public/common`.
- [x] **Step 4:** Pass. Commit: `prefs: WebRTC on the default public interface; Related Website Sets off`.

### Task 3: Proving what was already there

**Files:** Modify `test/network_defaults_browsertest.cc`.

- [x] **Step 1:** Tests (each must pass now; Task 4's mutations and the notes show which can fail):

```cpp
IN_PROC_BROWSER_TEST_F(NetworkDefaultsBrowserTest, PrivacySandboxAdApisAreOff) {
  PrivacySandboxSettings* settings = PrivacySandboxSettingsFactory::GetForProfile(GetProfile());
  EXPECT_FALSE(settings->IsTopicsAllowed());
  EXPECT_FALSE(settings->IsFledgeAllowed(url::Origin::Create(GURL("https://a.test")),
                                         url::Origin::Create(GURL("https://b.test")),
                                         content::InterestGroupApiOperation::kJoin));
  EXPECT_FALSE(GetProfile()->GetPrefs()->GetBoolean(prefs::kPrivacySandboxM1AdMeasurementEnabled));
}

IN_PROC_BROWSER_TEST_F(NetworkDefaultsBrowserTest, ThirdPartyCookiesAreNotSent) {
  // Set first-party, then asked for from another site's frame.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), HttpsUrl("b.test", "/set-cookie?tp=1;SameSite=None;Secure")));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), HttpsUrl("b.test", "/echoheader?Cookie")));
  ASSERT_EQ(BodyText(Tab()->GetPrimaryMainFrame()), "tp=1");  // The control.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), HttpsUrl("a.test", "/title1.html")));
  EXPECT_EQ(BodyText(LoadIframe(HttpsUrl("b.test", "/echoheader?Cookie"))), "None");
}

IN_PROC_BROWSER_TEST_F(NetworkDefaultsBrowserTest, BounceTrackingMitigationIsOn) {
  EXPECT_TRUE(base::FeatureList::IsEnabled(features::kBtm));
  EXPECT_NE(features::kBtmTriggeringAction.Get(), content::BtmTriggeringAction::kNone);
  EXPECT_TRUE(content::BtmService::Get(GetProfile()));
}

IN_PROC_BROWSER_TEST_F(NetworkDefaultsBrowserTest, IncognitoWarnsBeforeAnHttpOnlySite) {
  // The upgrade goes to a port without TLS, so the site is HTTP-only.
  HttpsUpgradesInterceptor::SetHttpPortForTesting(embedded_test_server()->port());
  HttpsUpgradesInterceptor::SetHttpsPortForTesting(embedded_test_server()->port());
  Browser* incognito = CreateIncognitoBrowser();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(incognito, Url("http-only.test", "/page")));
  EXPECT_TRUE(chrome_browser_interstitials::IsShowingHttpsFirstModeInterstitial(Tab(incognito)));
}

IN_PROC_BROWSER_TEST_F(NetworkDefaultsBrowserTest, DnsOverHttpsIsAutomatic) {
  EXPECT_EQ(SystemNetworkContextManager::GetStubResolverConfigReader()
                ->GetSecureDnsConfiguration(/*force_check_parental_controls_for_automatic_mode=*/false)
                .mode(),
            net::SecureDnsMode::kAutomatic);
}

IN_PROC_BROWSER_TEST_F(NetworkDefaultsBrowserTest, ACrossSiteSubresourceGetsOnlyTheOrigin) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/page?secret=1")));
  ASSERT_TRUE(content::ExecJs(Tab(), content::JsReplace(
      "new Promise(done => { const i = new Image(); i.onload = i.onerror = done; i.src = $1; })",
      Url("b.test", "/img"))));
  EXPECT_EQ(Header("b.test", "/img", "Referer"), Url("a.test", "/").spec());
}

IN_PROC_BROWSER_TEST_F(NetworkDefaultsBrowserTest, PageHintsAndSpeculationRulesContactNobody) {
  // A second server stands for a site the user hasn't chosen to visit; its
  // listener counts every socket it accepts.
  const GURL target = hints_server_.GetURL("hints.test", "/target");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/page")));
  ASSERT_TRUE(content::ExecJs(Tab(), content::JsReplace(R"(
      for (const rel of ['preconnect', 'dns-prefetch']) {
        const l = document.createElement('link'); l.rel = rel; l.href = $1;
        document.head.appendChild(l);
      }
      const s = document.createElement('script'); s.type = 'speculationrules';
      s.textContent = JSON.stringify({prefetch: [{source: 'list', urls: [$1]}]});
      document.head.appendChild(s);)", target)));
  WaitFor(base::Seconds(3));
  EXPECT_EQ(hints_listener_.accepted(), 0) << "a socket was opened to a site not visited";
}
```

Helpers in the fixture: `HttpsUrl()` (the HTTPS server with `CERT_TEST_NAMES` and `AddDefaultHandlers`), `BodyText(rfh)` (`EvalJs(rfh, "document.body.innerText")`), `LoadIframe(url)` (appends an iframe, waits with `TestNavigationObserver`, returns `ChildFrameAt(main, 0)`), `WaitFor(delta)` (a `RunLoop` quit by a delayed task), `hints_server_` with `hints_listener_` (an `EmbeddedTestServerConnectionListener` counting `AcceptedSocket`), `HttpsUpgradesInterceptor` ports reset in `TearDownOnMainThread`.
- [x] **Step 2:** Build and run: every test passes. A failure is a finding, recorded before anything changes.
- [x] **Step 3:** Commit: `test: the network defaults the privacy model promises, by behavior`.

### Task 4: Mutation checks and the egress audit

- [x] **Step 1:** M1 GPC not forced; M2 WebRTC `default`; M3 Related Website Sets on (both in `src/ghost` copies of Tasks 1–2's lines); M4 `kNetworkPredictionOptions` back to upstream's default (remove its `SetDefaultPrefValue`). Each built, run, seen failing, undone with `sync.py`; `src/ghost` compared with webops.
- [x] **Step 2:** Under M4, record whether the listener saw the preconnect, the speculation-rules prefetch, or both.
- [x] **Step 3:** Egress audit on `out/vanilla`: no unexpected host. GPC adds a header, not a host.

### Task 5: Docs, push

- [x] **Step 1:** privacy-model.md: each promise of "Cookies and storage" and "Connections and DNS" names its test; Related Website Sets off; strict HTTPS-First in Incognito; WebRTC's policy; DNS prefetch shares preconnect's gate. testing.md: the suite. roadmap.md: 3C done. Progress notes `docs/superpowers/specs/2026-10-10-network-defaults-spike.md`; spec and plan status.
- [x] **Step 2:** Full suites, tooling tests, lint, `patches.py check`; push; wait for both tooling jobs; memory.
