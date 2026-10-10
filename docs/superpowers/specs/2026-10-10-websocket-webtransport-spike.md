# WebSocket and WebTransport through the blocking engine: progress notes

- Phase 3, sub-project 3D-2 ([design](2026-10-10-websocket-webtransport-design.md), [plan](../plans/2026-10-10-websocket-webtransport.md))
- Machine: the reference machine (Ryzen 5 3600, 6 cores, 32 GB)
- Chromium 152.0.7977.158, the series with patch 0034 (34 patches); `out/vanilla`

## The spike's questions

| Question | Answer |
|---|---|
| Is `WillInterceptWebSocket` asked for workers? | Yes. `WebSocketConnectorImpl` passes the creator document's frame for a page and a **dedicated worker**, so those follow their page's level; a **shared or service worker** comes with a null frame and `CreateWebSocket` has no profile: judged at Standard against `site_for_cookies`' site. |
| How does a dropped WebSocket look to the page? | Dropping the handshake client fires the `WebSocket`'s `error` (then `close`); the test server's request monitor shows the handshake never arrived. |
| Test servers | `EmbeddedTestServer` has WebSocket handlers (`InstallDefaultWebSocketHandlers`), no Python server needed. `content::WebTransportSimpleTestServer` forces QUIC for `localhost:<port>` only: the test adds the tracker's name to `--origin-to-force-quic-on`, so an unblocked tracker connects (the control) and the refusal test can fail. |

## Found on the way

| Finding | What was done |
|---|---|
| `CheckRequest` was to gain a request-type field, since `RequestDestination` has no WebSocket value. The first test showed adblock-rust reads the type from a `ws:`/`wss:` scheme whatever type it is given (a `wss:` URL "as a script" was blocked by a `$websocket` rule). | The field was removed (YAGNI); a unit test documents the property the connection filter relies on. The field's commit had gone in with its test failing, chained after the build: the next commit removed it, and commits now follow a passing test only. |
| Routing every WebSocket through `CreateWebSocket` changes the path for frames no extension proxies. | Their continuation runs upstream's non-intercepted path: the `User-Agent` header and the factory (DevTools' headers are added inside the factory). |
| The worker test first crashed: the worker's code, built with the URL inside a template string, didn't parse. | The URL reaches the worker in a message. |
| Mutation check M3's first build failed (an unused variable), so its first run tested the previous binary. | Rerun with a mutation that compiles; recorded below from that run. |

## Mutation checks

Each made, built, seen failing, undone (Chromium's file with `git checkout`, `src/ghost` by copying webops back):

| Check | Fails |
|---|---|
| M1: `WillInterceptWebSocket` without Shade's term (patch 0034) | `ATrackersWebSocketIsBlocked`, `AWorkersTrackerWebSocketIsBlocked`, `StrictBlocksThePagesOwnWebSocket` |
| M2: the WebSocket filter ignores the level | `AnOffPagesTrackerWebSocketOpens`, `StrictBlocksThePagesOwnWebSocket` |
| M3: WebTransport's check skipped | `ATrackersWebTransportIsRefused` |

## End to end, 2026-10-10

| Check | Result |
|---|---|
| Tooling tests | pass (1 skipped: the TPM test) |
| `ghost_unittests` | 80 of 80 |
| `ghost_browsertests` | 65 of 65 (7 new), no retry |
| Egress audit, `out/vanilla` | 0 unexpected hosts (the route probe and `wpad`, not counted) |

## Not covered yet

- Shared and service workers' WebSockets at the page's real level (the hook has no profile).
- Messages over an open connection; WebRTC (Phase 9).
