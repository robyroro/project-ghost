# CNAME uncloaking: design

- Status: design approved 2026-10-12.
- Phase 4, sub-project 4C ([roadmap](../../roadmap.md#phase-4-content-protections)); builds on [3A, the blocking engine](2026-10-10-blocking-engine-design.md), [3D-1, the protection levels](2026-10-10-protection-levels-design.md) and [4B's redirects](2026-10-12-scriptlets-procedural-design.md).

## Goal

A tracker hidden behind a first-party subdomain (`metrics.site.com` with a CNAME to `site.tracker.net`) is blocked before the request leaves: the request filter learns the host's DNS alias chain from the host resolver and checks each alias with the lists, as it checks a URL. The tracker never receives the request, nor the first-party cookies it would carry.

## Decisions

- **Before the request leaves** (the user's choice among three, 2026-10-12). The request filter already holds each request until the engine's verdict; it now also resolves the host through the network service (`NetworkContext::ResolveHost`) and checks the aliases before passing the request on. Rejected:
  - reading `URLResponseHead::dns_aliases` when the response arrives, as upstream's subresource filter does: the request, with its cookies, has already reached the tracker;
  - a hook in `//net` between resolution and connection: a large patch in the network stack to carry on every rebase, and the engine in the network service.
- **No patch for the aliases.** `ResolveHostClient::OnComplete` gives a `network.mojom.AddressList`, which carries `dns_aliases` (the chain, canonical name first). The built-in resolver (`kAsyncDns`, on by default on Windows) fills it, DNS-over-HTTPS included.
- **The levels** (the user's choice, 2026-10-12): *Off* resolves nothing. *Standard* checks the aliases of every request the filter holds, same-site ones included, although it doesn't check a same-site request's URL: an alias on another site is a third party, whatever the URL says. *Strict* checks the same aliases.
- **Never resolve what is blocked.** A request the lists block by its URL is blocked without a DNS query: Shade's lookups never reach a blocked tracker's name server.
- **An exception ends it.** A request whose URL matched an exception (`@@`) is passed on without uncloaking: the list's authors allowed that host. The engine's verdict says so (`Decision::exception`).
- **The same cache entry.** The resolution uses the request's network anonymization key and secure DNS policy, so the request finds the answer in the host cache and the host is queried once. The spike confirms it in a NetLog.
- **No DNS beside a proxy.** The resolution asks for `direct_only`: when a proxy resolves the host, the answer is `ERR_DNS_DIRECT_ONLY` and the request passes without uncloaking. A proxy's user expects no local DNS query.
- **Which aliases.** From the chain: not the queried name, not an IP literal or an invalid name, not an alias on the original host's registrable domain (`a.site.com` to `b.site.com` isn't cloaking), each once, in the chain's order.
- **How an alias is checked.** As the original request with the alias as its host: same scheme, port, path and query, the same page, destination and method. Party is judged against the page, so `$third-party` rules apply to an alias on another site. The first alias that blocks decides; a redirect rule on it answers with Shade's resource, as in 4B.
- **Fail open on DNS.** An error, a timeout or a network service that went away: the request passes, and its own resolution fails or succeeds as it would have. The page dropping the request cancels the resolution.
- **Redirects too.** A redirect's new URL goes through the same steps before the page sees the redirect.
- **The shield** counts a request blocked by an alias like any other; the log names the alias and the rule.
- **Not in 4C:** WebSocket and WebTransport (`ConnectionFilter`): cloaked ones are rare, and their hooks can take the same step later. A list of CDN names to ignore (uBlock Origin's `cnameIgnoreList`): none of the shipped lists' rules needs one; 4D's corpus will tell.

## Components

| Unit | What it does |
|---|---|
| `components/blocking/rust/lib.rs` | `Verdict` gains `exception: bool`: an exception rule matched (`BlockerResult::exception`), so the request isn't blocked. |
| `BlockingEngine` | `Decision` gains `exception`; passed through. |
| `components/blocking/dns_aliases.{h,cc}` (new) | Pure functions, no network. `AliasesToCheck(const GURL& url, const std::vector<std::string>& dns_aliases)`: the aliases worth checking, by the rules above (`registrable_domain.h` for the domain). `WithAlias(const GURL& url, std::string_view alias)`: the URL with the alias as its host, or an invalid `GURL` if the alias isn't a valid host. |
| `browser/blocking/alias_resolver.{h,cc}` (new) | One resolution: calls `NetworkContext::ResolveHost` with the host, the key, the secure DNS policy and `direct_only`, and answers with `dns_aliases`, or nothing on any error. Destroying it cancels the resolution. Takes the `NetworkContext` from the storage partition at each resolution, never keeps it: the network service may restart. |
| `RequestFilter`, `InFlight` | The factory keeps its isolation info's network anonymization key; a request that carries its own (`trusted_params->isolation_info`, a frame's navigation) uses that one. `InFlight` gains the uncloaking step between the verdict and sending, and the same before passing a redirect on. The storage partition is the frame's, or without a frame (a worker) the profile's default. |
| Patches | None expected. If the spike finds workers in a partition other than the default, 0037 also passes the process id, whose host gives the partition. |

## Data flow

1. **The URL.** As today, a request that needs a verdict goes to `BlockingEngine::Check`. Blocked: blocked, no DNS. An exception: passed on, no uncloaking. A same-site request at Standard skips the check and goes to step 2.
2. **The aliases.** `AliasResolver` resolves the host; `AliasesToCheck` keeps the aliases worth checking; the engine checks `WithAlias(url, alias)` for each in turn, until one blocks.
3. **The verdict.** An alias blocks: the request is blocked (or answered with a redirect resource), and counted. None does, no aliases, or no answer: the request is passed on, and finds its host in the cache.
4. **Redirects.** Steps 1 to 3 for the new URL, before `OnReceiveRedirect` reaches the page; a block closes the network side, as today.

Everything stays on the UI thread, where the engine already answers; `ResolveHostClient` answers there too.

## Security and privacy

- **No query for a blocked host**, and none beside a proxy.
- **The DNS answer is as trusted as the user's resolver**, which already decides where every request goes. A hostile resolver could make a site's own resources look cloaked and block them; it can already break the site without Shade.
- **A site can evade uncloaking** with a CNAME to a host the lists don't name, or by proxying the tracker through its own servers. Uncloaking catches the trackers the lists name, as URL blocking does.
- **A cache miss no longer stays private to the request.** A request served from the HTTP cache, which would make no DNS query, may now make one to resolve its host. The query goes to the same resolver, over the same transport, as the request's own would.

## For the spike

1. A NetLog with DNS-over-HTTPS on and off: our resolution and the request make one DNS transaction per host.
2. `AddIPLiteralRuleWithDnsAliases` gives a chain of more than one alias to `ResolveHost` in a browser test.
3. Whether any worker in Shade uses a storage partition other than the default (decides 0037).
4. On a real site that EasyPrivacy's `specific_cname` sections name, the aliases reach the filter on Windows, and the tracker is blocked.

## Errors

- The resolution fails, times out or answers `ERR_DNS_DIRECT_ONLY`: the request passes without uncloaking.
- The network service restarts during a resolution: the client's pipe closes, which counts as no answer.
- The engine failed: it allows every request and every alias, as today.
- An alias that isn't a valid host: skipped.

## Performance budgets

Measured in the official build and reported; changed only with the user's agreement.

| What | Budget |
|---|---|
| The time uncloaking adds to a request whose host is in the host cache: from the URL's verdict to sending, on the perftest corpus' 20 front pages loaded a second time (a trace event per request, read by a tool over remote debugging) | p99 under 1 ms |
| The same with the host not in the cache | reported; no budget (the request would have waited for the same answer) |
| An alias's check | a request's check: p99 under ADR 0006's 50 µs (`ghost_blocking_perftests`) |

## Testing

**Unit tests** (`ghost_unittests`):
- `AliasesToCheck`: no aliases, only the queried name, an alias on the same registrable domain, an IP literal, an invalid name, duplicates, the order kept;
- `WithAlias`: port, path and query kept; an invalid alias gives an invalid URL;
- the bridge and `BlockingEngine`: `exception` set for a request an exception rule matched, while loading and after a failed load.

**Browser tests** (`ghost_browsertests`, `cname_uncloaking_browsertest.cc`) with a test list and the resolver's aliases (`AddIPLiteralRuleWithDnsAliases`):
1. Standard, a same-site request whose host has a listed alias: blocked, counted on the shield, and **the test server never receives it**;
2. a cross-site cloaked request: blocked;
3. an exception on the original host: loads, and the host is resolved once (the request's own);
4. an alias on the same registrable domain: loads;
5. Off: loads, no resolution;
6. a redirect to a cloaked host: blocked before the second server receives it;
7. a request blocked by its URL: **its host is never resolved** (a resolver that counts queries);
8. a proxy configured: no uncloaking, the request passes;
9. a `$redirect` rule on the alias: answered with Shade's resource;
10. the resolution fails: the request passes and fails as it would have.

**A fuzzer:** `ghost_blocking_request_fuzzer`'s input also gives aliases, through `AliasesToCheck` and `WithAlias`; run 30 minutes.

**Mutation checks**, each must fail:
- M1 the aliases aren't checked (test 1);
- M2 an exception doesn't stop uncloaking (test 3);
- M3 aliases on the same registrable domain are checked (test 4);
- M4 a blocked host is resolved (test 7);
- M5 a redirect isn't uncloaked (test 6);
- M6 no `direct_only` (test 8).

**The egress audit** stays at no unexpected host.

## Documentation

privacy-model.md (uncloaking at each level, no query for a blocked host, the limits: a proxy, the system resolver, WebSocket and WebTransport), architecture.md (the alias resolver, `InFlight`'s new step), threat-model.md (the resolver's trust, evasion, the HTTP cache's lookups), testing.md, roadmap.md (4C done), progress notes.

## Done when

- [ ] The tests pass with no retry and the mutation checks fail as required.
- [ ] The budgets hold in the official build.
- [ ] The egress audit finds no unexpected host.
- [ ] The documentation is updated; everything is pushed with tooling CI green.

## Out of scope

- WebSocket and WebTransport uncloaking; a CDN ignore list.
- Lists beyond EasyList and EasyPrivacy (AdGuard's CNAME tracker list included); list updates between releases (3E).
- Uncloaking through the system resolver, which gives at most the canonical name and only when asked, which forces the system source and so bypasses DNS-over-HTTPS.
- Breakage controls and the compatibility corpus (4D).
