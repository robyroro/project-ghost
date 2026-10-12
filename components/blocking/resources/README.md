# Scriptlets and redirect resources

Shade's own resources for filter lists' `+js(...)` scriptlets and
`$redirect=` rules, under the MPL-2.0 like the rest of `//ghost`. They are
written for Shade from the behavior uBlock Origin documents publicly
(its wiki), under the names and argument orders the lists use, and never
from uBlock Origin's or AdGuard's code, which is GPL-3.0
([licensing.md](../../../docs/licensing.md#filter-lists-and-scriptlets)).

- `scriptlets/`: each scriptlet is one function (adblock-rust calls it by
  name, with each argument a JSON string). `shade-safe.js` takes the natives
  the others use before the page's scripts run; `shade-chain.js` and
  `shade-trap.js` follow property chains. The browser runs a document's
  scriptlets in its own world at its start, all inside one function.
- `redirects/`: what a blocked script gets instead of an error. The
  non-JavaScript resources (`noop.txt`, `noop.json`, `1x1.gif`) are bytes in
  `../rust/resources.rs`.

`../rust/resources.rs` lists every resource with its aliases; the bridge
strips each file's license header before the engine sees it. A unit test
(`EngineBridgeTest.ShippedResourcesHaveNoProblem`) refuses a scriptlet that
isn't function-style and any `{{N}}` template placeholder.
