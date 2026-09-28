# 0006. adblock-rust as the content-blocking engine

- Status: Accepted
- Date: 2026-09-28

## Context

Blocking must be built into the browser, work without extensions, and understand the filter syntax that community lists are written in.

The syntax covers:
- network rules with options such as `$third-party`, `$domain=` and `$removeparam`;
- cosmetic (element hiding) rules;
- scriptlet injection;
- redirect resources.

The engine parses untrusted input (downloaded lists) in a privileged process, so it must satisfy Chromium's Rule of 2: untrusted input, unsafe language, or high privilege — pick at most two.

Candidates:
- **adblock-rust** (Brave; MPL-2.0; Rust). Used in production by Brave at scale. Supports the network, cosmetic, scriptlet and redirect syntax of EasyList and uBlock Origin-style lists, CNAME-aware matching, and a serializable compiled engine.
- **Chromium `components/url_pattern_index`** (BSD; C++). The indexed ruleset behind Chrome's subresource filter. Network rules only, with a subset of options.
- **Writing our own.** Years of work to match list syntax that already has a mature implementation.

## Decision

- Use adblock-rust as a vendored, pinned crate, built with Chromium's in-tree Rust toolchain and exposed to C++ through `cxx`.
- Matching runs in the browser process on a dedicated sequence, never on the UI thread.
- Requests are intercepted through a proxying `URLLoaderFactory` installed via `ContentBrowserClient::WillCreateURLLoaderFactory`. WebSockets and WebTransport use their own `ContentBrowserClient` hooks.
- Filter lists are data. They are delivered as signed components through Chromium's component updater, pointed at our update server, and compiled on the client.
- A snapshot of the default lists ships with the installer, so blocking works on first run offline.

## Consequences

- Rust code parses the untrusted list data, which satisfies the Rule of 2 in the browser process.
- We depend on Brave's maintenance of adblock-rust. Pinning plus our own tests limit surprise. The crate is MPL-2.0 like our code, so forking it is possible if that dependency fails us.
- Browser-side interception adds a hop for every request. The budget is under 50 µs p99 per match, measured by a performance test against vanilla Chromium at the same tag.
- Scriptlet resources must be our own. uBlock Origin's are GPL-3 ([docs/licensing.md](../licensing.md#filter-lists-and-scriptlets)).
- The fallback, if Rust integration in Chromium becomes untenable, is `url_pattern_index` for network rules only. Cosmetic filtering would then need a separate implementation.

## Alternatives considered

- **`url_pattern_index` as the primary engine.** No cosmetic filters, scriptlets or `$removeparam`. It can't use most of what current lists express.
- **Renderer-side matching**, as the subresource filter does. It avoids a browser-process hop. But either every renderer holds a copy of the engine (memory multiplied by process count), or rulesets must be shared through memory maps in a format adblock-rust doesn't provide.
- **Relying on an extension** (uBlock Origin Lite under Manifest V3). It contradicts the requirement that privacy features must not depend on extensions, and inherits MV3's rule limits.
