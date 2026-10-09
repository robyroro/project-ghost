# Shade

Shade is an open-source desktop browser built on Chromium. Its design goals are privacy by default and browsing identities that are properly isolated from each other.

The product is named Shade; "Ghost" is the codename that stays in the code (`//ghost`, the patches, this repository's name). Everything user-visible is confined to `branding/`, so a rename touches one directory. The name's trademark clearance is still open ([licensing: trademarks](docs/licensing.md#trademarks)).

## Status

**Phase 0: foundations.** There is no browser binary yet.

The repository contains:
- The architecture, privacy, threat-model and licensing documents that later work is held to.
- The tooling that turns a pinned Chromium release plus this repository into a build.

The first branded build is Phase 1 of the [roadmap](docs/roadmap.md).

## What we are building

- **Built-in blocking.** Tracker and ad blocking in the browser's network stack, using [adblock-rust](https://github.com/brave/adblock-rust) with EasyList and EasyPrivacy. No extension is required.
- **Identities.** Per-tab isolated cookie jars, site storage, permissions and network routes. Three accounts on the same site can be open side by side.
- **Ghost sessions.** Disposable sessions, each isolated from every other session, whose state is discarded when they close. They reduce what persists on your machine and what sites can link. They do not make you anonymous.
- **Fingerprinting protections.** We standardize the values that can be standardized and add deterministic, per-site noise where they can't. Details and limits are in [the privacy model](docs/privacy-model.md#fingerprinting).
- **A per-site privacy report.** The score is computed from a published formula, and every deduction is itemized.
- **No telemetry.** A test launches the browser on a fresh profile and fails if it contacts any host outside a reviewed allowlist.
- **Extensions.** Chrome Web Store extensions work. No privacy feature depends on one.

**What it is not.** Ghost is not Tor Browser. It does not provide anonymity against an adversary who can observe your network or correlate traffic. [The threat model](docs/threat-model.md) states what we defend against and what we don't.

## Getting started

Development targets Windows 11 x64 first. See [docs/build/windows.md](docs/build/windows.md) for the full procedure.

```
python tools/check_env.py --build-root D:\ghost
python tools/bootstrap.py --root D:\ghost --dry-run
```

`check_env.py` lists what this machine is missing for the pinned Chromium version ([CHROMIUM_VERSION](CHROMIUM_VERSION)). `bootstrap.py` prints the exact commands it would run to create the checkout.

## Documentation

| Document | Contents |
|---|---|
| [docs/architecture.md](docs/architecture.md) | How the browser is put together and where each feature lives |
| [docs/privacy-model.md](docs/privacy-model.md) | Modes, protections, fingerprinting strategy, the privacy score |
| [docs/threat-model.md](docs/threat-model.md) | Assets, adversaries, controls, non-goals |
| [docs/licensing.md](docs/licensing.md) | Project license, dependency policy, legal gates before release |
| [docs/roadmap.md](docs/roadmap.md) | Phases to public alpha and their exit criteria |
| [docs/patching.md](docs/patching.md) | How we carry changes to Chromium and move to new versions |
| [docs/adr/](docs/adr/README.md) | Architecture decision records |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Workflow, coding standards, review rules |

## License

Code in this repository is licensed under the [Mozilla Public License 2.0](LICENSE). Chromium and the third-party code it includes keep their own licenses. See [docs/licensing.md](docs/licensing.md).
