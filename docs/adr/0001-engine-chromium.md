# 0001. Build on Chromium

- Status: Accepted
- Date: 2026-09-28

## Context

We need an engine that a small team can keep secure for years. It must allow privacy controls below the JavaScript layer, run Chrome Web Store extensions, and ship on Windows first without ruling out macOS and Linux.

Realistic candidates:
- a Chromium fork;
- Chromium plus reusable patch sets;
- a fork of Brave's brave-core;
- CEF;
- a Firefox/Gecko fork.

Electron is not a browser security model and was not considered further.

Factors weighed:
- **Security maintenance.** Both Chromium and Gecko ship frequent security releases. Whatever we choose, we must re-base onto them quickly.
- **Privacy primitives.** Gecko already has several we need: container isolation via `originAttributes`, resist-fingerprinting (Tor and Mullvad Browser build on it), and total cookie protection. Chromium has comparable building blocks in different shapes:
  - `StoragePartition`, which gives each partition its own `NetworkContext`: cookies, HTTP cache, HSTS, auth cache, proxy configuration;
  - off-the-record profiles with unique IDs;
  - third-party storage partitioning.

  Brave shipped per-tab containers built on `StoragePartitionConfig` in Brave 1.92 (July 2026). The hardest identity feature is demonstrably buildable on Chromium.
- **Extensions.** Only Chromium runs Chrome Web Store extensions without porting.
- **Fingerprinting crowd.** Standardization protects users only if they look like many other users. A new browser's own population is too small to hide in. The largest population we can plausibly resemble is Chromium-based browsers on Windows. A standardized Gecko fork resembles the much smaller set of resist-fingerprinting users.
- **Windows security.** Chromium's sandbox and site isolation are the most battle-tested on Windows.
- **Build cost.** Chromium is far more expensive to build than Gecko: at least 100 GB, and hours for a full build on a desktop CPU. Gecko's artifact builds make front-end work cheap. This is Chromium's biggest cost to us.

## Decision

Build on Chromium, as a set of Ghost-owned sources (`//ghost`) plus a minimal patch series on top of pinned upstream release tags ([ADR 0004](0004-patch-strategy.md)).

Reuse components, not a whole downstream browser:
- **adblock-rust** as a dependency ([ADR 0006](0006-blocking-engine-adblock-rust.md));
- **ungoogled-chromium** patches (BSD-3) as references or cherry-picks for removing Google services;
- **brave-core** (MPL-2.0) as a reference implementation for integration points, copying individual files only where that is clearly better than writing our own, with notices kept.

## Consequences

- We inherit Chromium's security fixes by re-basing, not by porting. Staying current becomes a permanent staffing cost of roughly half to one engineer ([ADR 0003](0003-upstream-extended-stable.md) reduces it).
- Build infrastructure is a real budget item. Developers need at least 16 cores, 64 GB of RAM and fast NVMe storage for comfortable iteration. CI needs self-hosted builders.
- Fingerprinting standardization (resist-fingerprinting-style) must be built by us in Blink. There is no equivalent upstream mode to switch on.
- Identity isolation must be audited against every profile-scoped service in `//chrome`, because Chromium's profile is the unit most services assume.

## Alternatives considered

- **Firefox/Gecko fork.** The strongest alternative, rejected for four reasons:
  - no Chrome Web Store support, which matters for the mainstream users we target;
  - a less mature Windows sandbox track record;
  - a smaller web-compatibility share;
  - dependence on Mozilla's funding and priorities for the engine's future.
- **Fork of brave-core.** We would re-base twice (Chromium→Brave→us) and lag both. We would also inherit services we must remove (Rewards, Wallet, Leo, VPN, their endpoints) and design choices we don't share, such as randomization-first fingerprinting.
- **CEF.** An embedding framework. It lags Chromium releases, supports only part of the extension system, and hides the profile and network internals we need to modify.
- **Cromite/Bromite patches as a base.** GPL-3 licensed. They can't be mixed into MPL-2.0 files ([ADR 0002](0002-license-mpl2-dco.md)).
- **ungoogled-chromium as the base.** It removes Safe Browsing and the component updater outright. We need privacy-preserving replacements for both, not their absence. Individual patches remain useful.
