# CNAME uncloaking: progress notes

Phase 4C ([design](2026-10-12-cname-uncloaking-design.md), [plan](../plans/2026-10-12-cname-uncloaking.md)).

## Tasks 1–3: the engine, the aliases, the resolver (2026-10-12)

- **The engine:** `check_aliases` in the bridge, `BlockingEngine::CheckAliases`; the bridge's 5 and the engine's 4 new tests pass, with the 43 earlier ones.
- **`dns_aliases`:** 9 tests pass.
- **`AliasResolver`**, a finding against the plan: `ResolveHostParameters::secure_dns_policy` is `network::mojom::SecureDnsPolicy`, which has no typemap in a struct field, so the resolver takes the mojom enum, not `net::SecureDnsPolicy`.

## Task 4: the request filter (2026-10-13)

- **A test bug, not a filter bug:** the plan's `LoadScript` built the URL in a template literal inside `content::JsReplace`'s script. `JsReplace` uses `base::ReplaceStringPlaceholders` in its lenient mode, which drops a `$` that isn't followed by a digit, and the character after it. `` `http://${$1}:${location.port}${$2}` `` became an invalid URL, so every script failed, even those that should load. The URL is now built in C++ (`Url(host, path)`) and passed as `$1`, as `RequestFilterBrowserTest` does.
- **`GURL::host()` is a `std::string_view` at the pin:** binding it to the timing lambda's `std::string` parameter needs an explicit conversion.
- **The proxy test** counted every resolution. The page's own host is direct (bypassed), and its other same-site requests (the favicon's) are uncloaked too, which is right. The test now looks only at the proxied host: started, then `ERR_DNS_DIRECT_ONLY`.
- **Spike point 2:** a chain of two aliases (`AddIPLiteralRuleWithDnsAliases`) reaches the filter through the system resolver's path in a browser test. The `CHECK` on one alias in `host_resolver_system_task.cc` isn't on that path. `EveryAliasIsChecked` stays.
- **Results:**
  - `CnameUncloakingBrowserTest` and `CnameUncloakingProxyBrowserTest`: 11 of 11 pass.
  - The earlier suites (`RequestFilter*`, `ConnectionFilter*`, `Redirect*`, `BlockedCount*`, `ProtectionLevels*`, `CosmeticFilters*`, `Scriptlets*`, `Procedural*`, `QueryFilter*`) pass with no retry. No test depended on a same-site request starting at once.

## Task 5: spike (2026-10-13)

All runs: the dev build (`out\vanilla`), a fresh profile on the hidden desktop, a NetLog, 25 s on the page. The NetLogs stay in the session's scratchpad (they hold IP addresses). The tool counts `HOST_RESOLVER_MANAGER_JOB`s per host: one job is one real resolution, and a cache hit makes none.

- **One DNS transaction per host (spike point 1).**
  - `https://www.lemonde.fr/` with DNS-over-HTTPS off: the 5 hosts the page resolves have one job each, though the filter uncloaked 45 requests to them. Our resolution and the request share the cache entry, so the key matches.
  - With DNS-over-HTTPS secure (Cloudflare's template): the same, one job per page host. `cloudflare-dns.com` has 2 jobs: the DoH server's own bootstrap, as without uncloaking.
  - On `https://www.telus.com/`, `challenges.cloudflare.com` has 2 jobs, under a `same_site` and a `cross_site` key. The same 2 jobs appear with the site Off, which uncloaks nothing: two frames' requests, partitioned by Chromium.
  - With the site Off, lemonde.fr resolves 9 hosts, against 5 with protections on; telus.com resolves 22, against 12. The difference is the trackers blocked by their URL: Shade never resolves them.
- **WPAD (step 2).** This PC's proxy settings auto-detect (WPAD). `wpad` has 4 jobs in every run, with the site Off too: Chromium's own proxy auto-detection, not `direct_only`.
  - `direct_only` makes a proxy resolution per uncloaking (`ProxyCheckingHostResolverRequest`). It passes an empty `NetLogWithSource`, so the NetLog doesn't show it.
  - Without a PAC script, a proxy resolution is a lookup in the proxy service's state. With one, it is a script evaluation per uncloaked request. Task 7's timing includes it.
- **Storage partitions (spike point 3).** A partition other than the default comes, at the pin, only from:
  - an extension with isolated storage (`HasIsolatedStorage`: Chrome Apps, deprecated on Windows);
  - an Isolated Web App (`kIsolatedWebApps`, off by default on Windows; Shade doesn't enable it);
  - `<webview>` in those, or in WebUI.

  A web page's shared or service worker runs in the profile's default partition, so the fallback is right, and 0037 stays as it is. Even a worker in another partition would only cost one extra lookup, through the same resolver with the same settings; it can't leak anything the request itself doesn't send. architecture.md records the assumption.
- **Real cloaked trackers (spike point 4).**
  - From the 35,621 `||host^` rules of EasyPrivacy's `specific_cname` sections, the first 3,000 gave at least 40 hosts whose canonical name lies on a listed domain (the listing was cut at 40). All 40 were Adobe Analytics, via `*.sc.omtrdc.net` (`||omtrdc.net^`).
  - `https://www.telus.com/`: `b.telus.com` (alias `telus.com.ssl.sc.omtrdc.net`) blocked, 3 requests (`/id`, `/ee/v1/interact`, `/ee/v1/privacy/set-consent`), by `||omtrdc.net^`.
  - `https://www.jalan.net/`: `aa-metrics.jalan.net` (alias `jalan.net.ssl.sc.omtrdc.net`) blocked, 2 requests (`/id`, `/b/ss/…`).
  - At Standard both are same-site requests, which the URL check skips: only uncloaking catches them.

## Task 6: the fuzzer (2026-10-13)

- **The fifth field:** comma-separated DNS aliases, through `AliasesToCheck`, `WithAlias` and `check_aliases`, with `CHECK`s that an alias kept can be placed in the URL.
- **Three findings in the first smoke runs,** all in the harness or the contract, none in the filter's path:
  - `ht:'stp//apnews.com/id?x=1`: a URL with no host takes no alias, so `WithAlias` gives an invalid URL. Right, but its comment didn't say so. Now it does, with a test (`AUrlWithoutAHostTakesNoAlias`). The fuzzer, as the request filter does, gives aliases only to http(s) URLs.
  - An invalid URL can still have an `http` scheme (`GURL::SchemeIsHTTPOrHTTPS` doesn't check validity), and `spec()` on it is fatal. The fuzzer checks `is_valid()` first, and so does `Uncloak` (defense in depth: a request's URL is always valid).
  - GURL needs ICU for international host names; the harness now initializes it, as `url/gurl_fuzzer.cc` does.
- **Smoke run:** 134,703 inputs in 120 s, no crash. The 30-minute run is Task 8's.
