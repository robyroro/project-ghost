# WebSocket and WebTransport through the blocking engine: design

- Status: design approved 2026-10-10
- Phase 3, sub-project 3D-2 ([roadmap](../../roadmap.md#phase-3-network-protections)); follows [3D-1, the protection levels](2026-10-10-protection-levels-design.md)
- Closes the gap privacy-model.md names since 3A: "WebSocket and WebTransport connections are not yet checked"

## Goal

A page's WebSocket and WebTransport connections are judged like its other requests: the blocking engine decides, by the page's protection level, and a blocked connection never reaches the network.

## Decisions

- **The hooks are Chromium's, which only extensions use today**: `WillInterceptWebSocket` and `CreateWebSocket` (a WebSocket is routed through `CreateWebSocket` only when `WillInterceptWebSocket` says so; the connection can be held there and dropped before its handshake), and `WillCreateWebTransport` (its callback takes an error). These connections don't go through a `URLLoaderFactory`, so 3A's request filter never sees them. **Patch 0034** to `ChromeContentBrowserClient` puts Shade's check first in all three and then takes upstream's path, the extensions' webRequest proxy included, unchanged.
- **The rules are 3A's and 3D-1's**: the page's level decides (Off doesn't check, Standard checks third parties, Strict the page's own site too); the request types are `websocket` (EasyList has `$websocket` rules) and `other` for WebTransport.
- **The page**: a frame's outermost main frame. A WebSocket opened by a worker has no frame, and `CreateWebSocket` then has no profile either (upstream's own TODO, crbug.com/40195467); its page is the site in `site_for_cookies`, at Standard. WebTransport's hook has the process, hence the profile, and `initiator_origin` when there is no frame.
- **Blocked**: a WebSocket is never created, so the page's `WebSocket` fires `error` and `close`; WebTransport is refused with `ERR_BLOCKED_BY_CLIENT`. Nothing reaches the network.

## Components

| Unit | What it does |
|---|---|
| `//ghost/browser/blocking/connection_filter.{h,cc}` | `bool ShouldInterceptWebSocket(RenderFrameHost*)` (true for every frame, and for none); `void FilterWebSocket(frame, url, site_for_cookies, base::OnceClosure proceed, base::OnceClosure block)`; `void FilterWebTransport(process_id, frame_routing_id, url, initiator_origin, base::OnceClosure proceed, base::OnceCallback<void(int net_error)> block)`. Each resolves the page and its policy, asks the engine when the policy says to, and runs one continuation on the UI thread. |
| `NeedsVerdict` | Gains the page-and-URL form the connection filter shares with the request filter, so the same-site rule is one function. |
| Patch 0034 | `WillInterceptWebSocket` also returns `ShouldInterceptWebSocket(frame)`; `CreateWebSocket` and `WillCreateWebTransport` call the filter first and run their current bodies as the `proceed` continuation; `block` drops the WebSocket's handshake client, and runs WebTransport's callback with `ERR_BLOCKED_BY_CLIENT`. |

## For the spike

1. That `WillInterceptWebSocket` is asked for worker WebSockets (frame null) and what `CreateWebSocket` then receives; whether the profile is reachable another way.
2. How a dropped handshake client surfaces in the page (the `WebSocket`'s `error` and `close` events, and what close code).
3. Chromium's test servers: `net::SpawnedTestServer` (WebSocket, a Python server) and the WebTransport test server (`content/browser/webtransport/web_transport_simple_test_server`), reachable from `ghost_browsertests` with host-resolver rules.

## Testing

**Browser tests** (`ghost_browsertests`, `ConnectionFilterBrowserTest`), with a test list `||tracker.test^$third-party\n/own-socket\n`:
- a page's WebSocket to `tracker.test`: blocked (the page sees `error`; the server sees no handshake); to its own site: open;
- Strict: the page's own `/own-socket` blocked; Off: the tracker's open;
- a worker's WebSocket to `tracker.test`: blocked;
- WebTransport to `tracker.test`: refused; to its own site: open.

**Mutation checks**, each must fail: M1 `WillInterceptWebSocket` without Shade's term; M2 the WebSocket filter ignores the level; M3 WebTransport's check skipped.

**The egress audit** stays at no unexpected host.

## Documentation

privacy-model.md (WebSocket and WebTransport checked; the worker's level), architecture.md (patch 0034), testing.md, roadmap.md (3D-2 done), progress notes.

## Done when

- [ ] The tests pass and the mutation checks fail as required.
- [ ] The egress audit finds no unexpected host.
- [ ] The documentation is updated; everything is pushed with tooling CI green.

## Out of scope

- WebRTC data channels and other peer-to-peer transports (Phase 9's routing decides WebRTC).
- Messages over an open connection (the engine judges connections, not their traffic).
