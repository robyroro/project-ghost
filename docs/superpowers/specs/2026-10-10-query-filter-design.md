# Tracking-parameter stripping: design

- Status: design approved 2026-10-10
- Phase 3, sub-project 3B ([roadmap](../../roadmap.md#phase-3-network-protections)); what it enforces is in [privacy-model.md](../../privacy-model.md#tracking-parameters)
- Follows 3A, [the blocking engine](2026-10-10-blocking-engine-design.md); 3D adds the setting's UI and per-site exceptions, 3E delivers the list as a component

## Goal

A link's tracking parameters don't leave the browser: when a top-level navigation arrives from another site or from the user, Shade removes the click identifiers (and, where the mode asks, the campaign parameters) from its URL before the request is sent, and the page is committed at the clean URL.

## Decisions

Settled in discussion on 2026-10-10:

- **What is stripped now.** Click identifiers in every profile; campaign parameters (`utm_*`) in Incognito, as [privacy-model.md](../../privacy-model.md#tracking-parameters) asks of Private mode, and in Normal only when the profile pref `ghost.query_filter.strip_campaign_parameters` is on (default off; its switch comes with 3D's panel). Private and Ghost modes don't exist until Phase 5; Incognito becomes Private, so nothing is built twice.
- **When.** When the URL comes from elsewhere: a navigation the user started (no initiator), one started by another site (another registrable domain), and a redirect that crosses sites. A site navigating within its own site keeps its parameters: it put them there and already has them, and its own flows (sign-in, checkout) may need them. Brave behaves alike.
- **The list is ours,** a text file committed under MPL-2.0: click identifiers valid on every site, some tied to a site (`si` on YouTube and Spotify), and the campaign group. It starts from privacy-model.md's list and brave-core's query filter (MPL-2.0, taken with attribution), each entry checked. Rejected: `$removeparam` rules in adblock-rust from a list such as AdGuard's URL Tracking filter: wider, but less predictable, harder to test, and it would put the engine on top-level navigations, which 3A deliberately leaves alone.
- **The mechanism is a `blink::URLLoaderThrottle` on navigations,** added by `ChromeContentBrowserClient::CreateURLLoaderThrottles` (approach A of three). Changing `request->url` in `WillStartRequest` makes an internal redirect, so the original URL is never sent; changing `redirect_info->new_url` in `WillRedirectRequest` is allowed when the origin stays, which removing query parameters guarantees. Chromium rewrites URLs the same way in `components/url_rewrite`. Rejected: extending 3A's request filter to navigations with a synthesized redirect (Mojo-level redirects by hand, two responsibilities in one class), and a `NavigationThrottle`, which can't change a URL, only cancel and restart the navigation (losing POST bodies and the back button).

## Components

### `//ghost/components/query_filter/`

No `//content` dependency; unit-tested without a browser.

| Unit | What it does |
|---|---|
| `data/parameters.txt` | The list. `#` comments; a `[click]` and a `[campaign]` group; one entry a line: a name (`fbclid`), or a name and the registrable domains it applies to (`si youtube.com youtu.be spotify.com`); a final `*` makes a prefix (`utm_*`). Each entry's comment names its source: the privacy model, brave-core, or a documented observation. |
| `parameter_list.{h,cc}` | Parses the format into a `ParameterList`. A malformed line is skipped and logged; 3E's component uses the same parser. |
| `query_filter.{h,cc}` | `std::optional<GURL> Strip(const GURL& url, const ParameterList& list, Scope scope)`: the URL without the matching parameters, or `std::nullopt` when nothing matches. The remaining query is kept byte for byte (order, percent-encoding); the fragment is kept; an emptied query loses its `?`. Names are compared after percent-decoding, case-sensitively. A site-tied entry matches when the URL's registrable domain is one of its domains (subdomains included). `Scope` is `kClick` or `kClickAndCampaign`. |
| `ShippedParameterList()` | The list compiled into the binary: a GN action turns `data/parameters.txt` into a C++ string literal at build time, parsed once on first use. No file to read and no loading state, so the first navigation after startup is covered. |

### `//ghost/browser/query_filter/`

| Unit | What it does |
|---|---|
| `query_filter_throttle.{h,cc}` | The throttle. `WillStartRequest` and `WillRedirectRequest` apply the rules below and replace the URL when `Strip` returns one. |
| `MaybeCreateQueryFilterThrottle(request, browser_context)` | Called by the patch. Creates a throttle only for a navigation of the outermost main frame (`request.is_outermost_main_frame`), which includes a prerendered page that becomes the tab's page, and never for iframes or fenced frames. Picks the scope: an off-the-record profile gets `kClickAndCampaign`; a regular one `kClick`, or `kClickAndCampaign` with the pref on. |
| `prefs.{h,cc}` | Registers `ghost.query_filter.strip_campaign_parameters`. |

### Patches

- **0032**, `chrome/browser/chrome_content_browser_client.cc` and `chrome/browser/BUILD.gn`: `CreateURLLoaderThrottles` appends `ghost::query_filter::MaybeCreateQueryFilterThrottle(...)` for navigation requests. `Why:` Chromium has no other embedder hook that can change a navigation's URL before it is sent.
- **0002** grows by one line: `RegisterProfilePrefs` also calls `ghost::RegisterProfilePrefs(registry)`, which registers Ghost's own profile prefs (this one first).

## The rules

- **Applies to** navigations of the outermost main frame, `http` and `https`, with method `GET`. A POST is never changed: an internal redirect may drop its body, and form targets rarely carry tracking parameters. A redirect whose new method isn't `GET` is not changed either.
- **At the start,** the URL is stripped when the navigation has **no initiator** (the address bar, a pasted link, a bookmark, a link opened from another application, session restore, the home page), when the initiator's registrable domain differs from the URL's, or when the initiator is **opaque** (sandboxed frames, `data:` URLs), which counts as another site.
- **At each redirect,** the new URL is stripped when its registrable domain differs from the URL being redirected from. Each step is judged on its own: a URL cleaned at the start that gains a parameter from a cross-site redirect is cleaned again.
- **Same site** means the same registrable domain (`net::registry_controlled_domains`, private registries included); an IP address is its own site, as in 3A.
- **What is removed:** `[click]` names valid on every site; `[click]` names tied to the URL's site; with `kClickAndCampaign`, `[campaign]` too.
- **What the user sees:** the page is committed at the clean URL, so the address bar, history, the `Referer` the next page receives, reload and back all have it. No UI notice now; 3D's panel can count. DevTools' Network panel shows the internal redirect.

## For the spike

Before the rest:

1. That `request_initiator` is empty for browser-initiated navigations (address bar, bookmarks, session restore, external links) and set for renderer-initiated ones, in `CreateURLLoaderThrottles` at 152.
2. That an internal redirect from `WillStartRequest` on a navigation sends nothing for the original URL (the embedded server never sees it) and commits the clean URL as the history entry, with no extra entry.
3. That changing `new_url` in `WillRedirectRequest` works for a navigation's cross-site redirect.
4. That `is_outermost_main_frame` is set for prerendered main frames and not for fenced frames, and what reaches the throttle for back/forward and reload from the cache.
5. Whether `CreateURLLoaderThrottles` runs again for a navigation that a service worker falls back to the network for (the header comment says throttles may run more than once); `Strip` is idempotent, so a second run must change nothing.

## Error handling

- Nothing fails at run time: the list is compiled in and its parse is tested; a URL `Strip` can't handle is left as it is.
- A malformed list line is skipped, logged once; the test of the shipped list fails on any.

## Testing

**Unit tests** (`ghost_unittests`):
- `Strip`: a parameter first, in the middle and last; the rest of the query byte for byte; the fragment kept; an emptied query without `?`; a percent-encoded name (`f%62clid`) matched; look-alikes (`fbclid2`, `xfbclid`) kept; a repeated parameter removed every time; the `utm_*` prefix; a site-tied entry only on its site and subdomains; each scope.
- The parser: groups, comments, prefixes, site-tied entries, malformed lines; **the shipped list parses without a skipped line**.
- The decision: no initiator, another site, an opaque initiator, the same site; POST; a scheme other than http(s).

**Browser tests** (`ghost_browsertests`), hosts mapped to the embedded test server, which records every URL it receives:
- a link on `a.test` to `b.test/?fbclid=1&x=2`: the server receives only `/?x=2`, and the committed URL and the history entry are clean;
- a link within `a.test`: kept;
- a cross-site redirect `r.test` → `b.test/?gclid=1`: stripped; a same-site redirect: kept;
- a navigation from the address bar: stripped;
- `utm_source`: kept in Normal, stripped in Incognito and in Normal with the pref on;
- an iframe's `?fbclid=`: kept; a form POST: kept.

**Fuzzing:** `ghost_query_filter_fuzzer`, libFuzzer over the parser and over `Strip` with arbitrary URLs against the shipped list, in 3A's `out/fuzz`; 30 minutes.

**Mutation checks**, each must fail:
- M1: no stripping at redirects → the redirect test fails;
- M2: same-site navigations stripped too → the same-site test fails;
- M3: the initiator ignored (only navigations without one are stripped) → the cross-site link test fails;
- M4: Incognito with `kClick` only → the `utm` test fails.

**The egress audit** stays at no unexpected host.

## Documentation

[privacy-model.md](../../privacy-model.md#tracking-parameters) (what is stripped and when: from elsewhere, not within a site, not POST, not iframes; the list as shipped; `utm_*` per profile), [architecture.md](../../architecture.md) (the units, patch 0032; the open question on the interception point for redirects, closed), [testing.md](../../testing.md) (the tests above, the fuzzer), [licensing.md](../../licensing.md) (brave-core entries with attribution), [roadmap.md](../../roadmap.md) (3B done), progress notes.

## Done when

- [ ] The tests above pass and the four mutation checks fail as required.
- [ ] The fuzzer ran 30 minutes without a crash.
- [ ] The egress audit finds no unexpected host.
- [ ] The documentation is updated; everything is pushed with tooling CI green.

## Out of scope

- The campaign setting's UI and per-site exceptions (3D).
- List updates between releases (3E).
- "Copy clean link" in the context menu.
- Bounce-tracking redirectors that wrap the real URL (`l.facebook.com/l.php?u=…`, `google.com/url?q=…`): skipping them goes beyond removing parameters; a later sub-project (Brave's "debouncing").
- Parameters on subresources: 3A blocks the trackers themselves.
- The fragment: it never leaves the browser.
