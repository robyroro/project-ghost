# Tracking-parameter stripping: progress notes

- Phase 3, sub-project 3B ([design](2026-10-10-query-filter-design.md), [plan](../plans/2026-10-10-query-filter.md))
- Machine: the reference machine (Ryzen 5 3600, 6 cores, 32 GB)
- Chromium 152.0.7977.158, the series with patch 0032 (32 patches); `out/vanilla` (development), `out/fuzz` (libFuzzer, ASan)

## The spike's questions

Answered by the browser tests (all on an embedded server that records every request) and by reading the sources:

| Question | Answer |
|---|---|
| Is `request_initiator` empty for navigations the user starts? | Yes: `TheAddressBarArrivesClean` and `ACrossSiteRedirectArrivesClean` start with `ui_test_utils::NavigateToURL` (no initiator) and are stripped; a page's `location.href` carries the page's origin. |
| Does an internal redirect from `WillStartRequest` send the original? | No: the server never receives it. One history entry is added, at the clean URL. |
| Can `WillRedirectRequest` change a navigation's cross-site redirect? | Yes (`ACrossSiteRedirectArrivesClean`). |
| Prerender, fenced frames | `is_outermost_main_frame` comes from `FrameTreeNode::IsOutermostMainFrame()`, "no parent and no outer document": a prerendered page's main frame is the root of its own frame tree and gets the throttle; a fenced frame has an outer document and doesn't. Shade turns preloading off by default anyway (Phase 1). |
| Back, forward, reload | They load the committed, clean URL (`BackForwardAndReloadUseTheCleanURL`). |
| A throttle run twice (service-worker fallback) | `Strip` is idempotent (`StrippingTwiceChangesNothing`, and the fuzzer checks it on every input). |

## Found on the way

| Finding | What was done |
|---|---|
| **`net`'s `SameDomainOrHost()` ignores unknown registries:** `shop.b.test` and `www.b.test`, and intranet names, are two sites to it. 3A's `RegistrableDomain()` counts them. | The function moved from `components/blocking` to `//ghost/components/site`, so blocking and stripping share one definition of a site. Found while planning. |
| **History keeps the original URL as the entry's "original request URL"**, saved with the session. Chromium records the first URL of a navigation there, also one a throttle rewrote. | It stays on this machine: loading it again (`NavigationController::LoadOriginalRequestURL`, a navigation without an initiator) is stripped again, and `ReloadingTheOriginalRequestURLSendsItClean` shows the original never reaches the server. Rewriting the entry would need another hook in `//content`; recorded in [privacy-model.md](../../privacy-model.md#tracking-parameters). History itself holds only the clean URL (`HistoryHoldsOnlyTheCleanURL`). |
| The fuzzer's first two runs stopped within seconds, on its own mistakes: `url::Origin` is new for every opaque URL (`data:`, unknown schemes), so its "same origin" check failed; and GURL needs ICU's data for internationalized hosts. | It compares scheme, host and port, and loads ICU as `url/gurl_fuzzer.cc` does. |
| Patch 0002 was to grow by the pref's registration. Amending it means rebasing the 29 patches after it, which rewrites their files, and Siso rebuilds by mtime. | The registration is in patch 0032, beside the throttle: one patch for 3B. |
| `Browser::profile()` is `GetProfile()` at 152; `//third_party/blink/public/common:headers` isn't visible to `//ghost` (`//third_party/blink/public/common` is). | Fixed while building. |

## The list

`components/query_filter/data/parameters.txt`: 73 entries. 67 click identifiers on every site (the privacy model's, and brave-core's default rule set, MPL-2.0, with attribution), 5 tied to a site (`igsh` on Instagram, `ref_src` and `ref_url` on X, `si` on YouTube from brave-core, `si` on Spotify observed), and `utm_*`. Brave's conditional entries (`mkt_tok`, `h_sid`, `h_slt`, `ck_subscriber_id`, kept on unsubscribe links) are left out: the format has no conditions. `tools/embed_text.py` compiles the list into the binary at build time.

## Fuzzing

`ghost_query_filter_fuzzer` in `out/fuzz` (built in under a minute beside 3A's fuzzers): the first line of the input is a URL, the rest a list; each input is stripped against it and against the shipped list, in both scopes, checking that the result is a valid URL with the same scheme, host and port and that a second pass changes nothing. Seeds: 1,500 URLs from 3A's request corpus with two listed parameters appended, half of them followed by the shipped list.

| 30 minutes | Runs | Per second | Edges covered | New inputs | Peak memory | Crashes |
|---|---|---|---|---|---|---|
| | 2,617,691 | 1,452 | 1,317 → 2,804 | 20,576 | 784 MB | none |

## Mutation checks

Each made in `src/ghost`, built, seen failing, then undone (`src/ghost` compared with webops byte for byte afterwards):

| Check | Fails |
|---|---|
| M1: no stripping at redirects (the redirect judged against itself) | `ACrossSiteRedirectArrivesClean` |
| M2: same-site navigations stripped too | `ALinkWithinASiteKeepsItsParameters` |
| M3: the initiator ignored (only navigations without one stripped) | `ALinkFromAnotherSiteArrivesClean`, `BackForwardAndReloadUseTheCleanURL`, `HistoryHoldsOnlyTheCleanURL`, `ReloadingTheOriginalRequestURLSendsItClean` |
| M4: Incognito without the campaign scope | `CampaignParametersAreStrippedInIncognito` |

## End to end, 2026-10-10

| Check | Result |
|---|---|
| Tooling tests | pass (1 skipped: the TPM test) |
| `ghost_unittests` | 67 of 67 (15 new: the list, `Strip`, the decisions, the pref; `RegistrableDomain`'s tests moved) |
| `ghost_browsertests` | 35 of 35 (13 new). 14 pass on retry after `EXCESSIVE_OUTPUT` (log lines from GCM, USB and ANGLE in each batch), against 2 to 6 before 3B: the known category, never a failure, but now frequent enough to quieten those logs in a later change |
| Egress audit, `out/vanilla` | 0 unexpected hosts (the route probe and `wpad`, not counted) |

## Not covered yet

- The campaign switch's UI and per-site exceptions (3D); list updates between releases (3E).
- Redirectors that wrap the real URL (`l.facebook.com/l.php?u=…`): a later sub-project.
- The fuzzer runs by hand.
