# WebSocket and WebTransport Filtering Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A page's WebSocket and WebTransport connections are judged by the blocking engine at the page's protection level, and a blocked one never reaches the network ([spec](../specs/2026-10-10-websocket-webtransport-design.md)).

**Architecture:** `//ghost/browser/blocking/connection_filter` resolves the page and its policy and asks the engine, then runs one continuation. Patch 0034 routes every WebSocket through `ChromeContentBrowserClient::CreateWebSocket` and calls the filter first there and in `WillCreateWebTransport`; allowed connections take upstream's path (the extensions' proxy when one may want them, else the factory with the `User-Agent` header, as upstream's non-intercepted path does).

**Status:** done 2026-10-10; what differed (no request-type field) is in the [progress notes](../specs/2026-10-10-websocket-webtransport-spike.md).

**Tech Stack:** C++, Chromium 152.0.7977.158; `EmbeddedTestServer`'s WebSocket handlers, `content::WebTransportSimpleTestServer`.

**Settled while planning:**
- `WebSocketConnectorImpl` gives `CreateWebSocket` a frame for pages and dedicated workers (the creator document); shared and service workers come with a null frame, hence no profile: their page is `site_for_cookies`' site, at Standard.
- Upstream's non-intercepted path runs `factory(url, {User-Agent}, handshake_client, NullRemote(), options.header_client)`; DevTools' extra headers are added inside the factory callback.
- `network::mojom::RequestDestination` has no WebSocket value: `CheckRequest` gains `std::optional<std::string> adblock_type`, which `BlockingEngine` uses instead of mapping the destination.
- `WebTransportSimpleTestServer` forces QUIC for `localhost:<port>` only; the tests extend `--origin-to-force-quic-on` to `tracker.test:<port>`, so a tracker's connection would succeed if not blocked (otherwise the test couldn't fail).

---

### Task 1: The engine takes an explicit request type

- [x] `components/blocking/blocking_engine.h`: `CheckRequest` gains `std::optional<std::string> adblock_type;  // Overrides the destination's type (WebSocket has none).` `blocking_engine.cc`'s `Answer` passes `request.adblock_type ? *request.adblock_type : ToAdblockType(request.destination)`.
- [x] Unit test first in `blocking_engine_unittest.cc`: with the list `||sockets.test^$websocket\n`, a check with `adblock_type = "websocket"` is blocked and the same URL as a script is not. Commit: `blocking: a check can name its request type`.

### Task 2: The connection filter and patch 0034

- [x] `browser/blocking/connection_filter.{h,cc}`:

```cpp
// Every WebSocket goes through CreateWebSocket once blocking has started.
bool ShouldInterceptWebSocket();
// Runs |proceed| unless the engine blocks the connection at the page's level;
// a blocked WebSocket is dropped with |proceed|, which owns its handshake.
void FilterWebSocket(content::RenderFrameHost* frame, const GURL& url,
                     const net::SiteForCookies& site_for_cookies, base::OnceClosure proceed);
void FilterWebTransport(int process_id, int frame_routing_id, const GURL& url,
                        const url::Origin& initiator_origin, base::OnceClosure proceed,
                        base::OnceClosure block);
```

The page: the frame's outermost main frame's committed URL; else `site_for_cookies.RepresentativeUrl()` (WebSocket) or `initiator_origin` (WebTransport). The policy: `privacy_policy::GetPolicy` with the frame's or process's `BrowserContext`; without one, Standard. Checked when `policy.block_requests` and (`policy.check_same_site` or another site), via `BlockingEngine::Check` with `adblock_type` `"websocket"` or `"other"`.
- [x] Patch 0034 in `chrome/browser/chrome_content_browser_client.cc` (and `//ghost/browser/blocking` already in `core`'s deps): `WillInterceptWebSocket` returns true when `ghost::blocking::ShouldInterceptWebSocket()`; `CreateWebSocket` calls `FilterWebSocket` with a continuation that runs upstream's body when the extensions' proxy may want the frame, else the factory as upstream's non-intercepted path; `WillCreateWebTransport` calls `FilterWebTransport`, `block` runs the callback with `network::mojom::WebTransportError::New(net::ERR_BLOCKED_BY_CLIENT, ...)`.
- [x] Browser tests first (`browser/blocking/connection_filter_browsertest.cc`, list `||tracker.test^$third-party\n/echo-with-no-extension\n`): WebSocket to `tracker.test` from a page: `error`, and the server's monitor sees no handshake; to the page's own site: `open`; Strict page: its own blocked; Off page: the tracker's open; from a dedicated worker: blocked; WebTransport to `tracker.test`: rejected; from an Off page: connected. Commit: `blocking: WebSocket and WebTransport connections through the engine (patch 0034)`.

### Task 3: Mutation checks, audit, docs, push

- [x] M1 `WillInterceptWebSocket` without Shade's term; M2 the WebSocket filter ignores the level (always Standard); M3 WebTransport's check skipped. Each failing, undone with `sync.py`.
- [x] Full suites; egress audit.
- [x] Docs: privacy-model.md (connections checked; shared and service workers at Standard), architecture.md (patch 0034), testing.md, roadmap.md (3D-2 done), progress notes, spec and plan status. Push; both tooling jobs; memory.
