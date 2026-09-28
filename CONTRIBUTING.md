# Contributing

Thank you for helping. This project changes a browser that people trust with their accounts and browsing history. The rules below exist to keep that trust, and to keep a Chromium fork maintainable by a small team.

## Before you start

- **Bugs and small fixes:** open an issue or a pull request directly.
- **Features and behavior changes:** open an issue describing the problem first. Anything that changes the architecture, adds a dependency, adds a network request, or changes a privacy claim needs an [architecture decision record](docs/adr/README.md) or an update to an existing one.
- **Security vulnerabilities:** do not open a public issue. See [SECURITY.md](SECURITY.md).

## Developer Certificate of Origin

We use the [Developer Certificate of Origin](https://developercertificate.org/) instead of a CLA. Sign off every commit to certify that you wrote the change, or otherwise have the right to submit it under the project license:

```
git commit -s
```

This adds a `Signed-off-by: Your Name <you@example.com>` trailer. Commits without it can't be merged.

## Where code goes

- **New code lives in this repository** (`//ghost` in the Chromium checkout). It follows Chromium's layering: `components/<feature>/{core,common,browser,renderer}` for feature logic, and `browser/` for glue to `//chrome`. `DEPS` include rules enforce the boundaries.
- **Changes to Chromium's own files are patches** in `patches/`, managed with `tools/patches.py`. Use a patch only when no upstream extension point exists. Keep it to the minimum hook that calls into `//ghost`. Read [docs/patching.md](docs/patching.md) before writing one.
- **User-visible names, icons and URLs live in `branding/`** and string resources, never in code. The product name will change.

## Coding standards

**All languages**
- Comments explain *why*: the constraint, the upstream behavior being worked around, the attack being prevented. Don't restate what the code does.
- Prefer small files with one clear purpose. If a file needs a table of contents, split it.
- No placeholder implementations, `TODO`-shaped architecture, or code that can't be built and tested yet.

**C++**
- Follow the [Chromium C++ style guide](https://chromium.googlesource.com/chromium/src/+/main/styleguide/c++/c++.md) and format with the pinned tag's `.clang-format` (`git cl format`).
- Use `base::` facilities, not the standard library equivalents Chromium bans.
- Every new Mojo interface needs IPC security review (see below). The browser process never trusts data from a renderer for a security or privacy decision.

**Rust**
- Follow Chromium's Rust guidance.
- `unsafe` is allowed only at the FFI boundary, with a `// SAFETY:` comment on each block.
- Crates are vendored and pinned. Nothing is fetched at build time.

**TypeScript / WebUI**
- Chromium WebUI conventions: Lit, no remote resources, no inline scripts, strict CSP.

**Python (`tools/`)**
- Standard library only, Python 3.11+, type hints, `unittest`.
- Keep side effects (subprocess, registry, disk) separate from logic, so the logic can be tested with synthetic inputs.

**Documentation**
- Write for engineers. State limitations next to capabilities.
- Never describe a feature as making users "anonymous", "untraceable" or "invisible".

## Tests

- Every behavior change comes with tests that fail without the change. A test that only exercises a mock of the code under test doesn't count.
- Integration points (network interception, storage partitioning, profile lifecycle) need browser tests. Unit tests alone can't show that Chromium actually called our hook.
- Changes to isolation (identities, Ghost sessions) must extend the isolation contract suite described in [docs/architecture.md](docs/architecture.md#testing-isolation).
- Run the tooling tests before sending tooling changes:

  ```
  python -m unittest discover -s tools/tests -t tools
  python tools/lint.py
  ```

## Commits and pull requests

- One logical change per commit. Subject line `area: imperative summary` (e.g. `blocking: defer requests until the engine has loaded`), then a body explaining why.
- Patch commits in the Chromium checkout also need the `Why:` and `Upstream:` trailers ([docs/patching.md](docs/patching.md#patch-format)).
- Keep pull requests reviewable: one feature or fix, split into commits that each build.

## Review requirements

Every change needs one approving review from a maintainer. **Two** approvals, at least one from a maintainer of the affected area, are required for:

- Patches touching `net/`, `services/network/`, `sandbox/`, `mojo/`, `crypto/`, `content/browser/renderer_host/`, `chrome/updater/`, `components/update_client/` or `third_party/boringssl/`, and any patch over 50 changed lines. `python tools/patches.py stats` flags these.
- New or changed `.mojom` interfaces (IPC security review).
- Request interception, update, signing, or release-pipeline code.
- New third-party dependencies (licensing review against [docs/licensing.md](docs/licensing.md#dependency-policy)).
- Any new network request the browser makes on its own. It must be added to the egress allowlist with a written justification.
- Changes to documented privacy guarantees or to the privacy score formula.

## Privacy commitments that bind contributors

- No analytics, telemetry, tracking pixels, or third-party SDKs in the browser or its build.
- No stable identifiers in any request the browser sends to project servers.
- Paid services stay outside the browser core. Privacy and security features aren't restricted to push people toward a subscription.

## Code of Conduct

Participation is governed by the [Code of Conduct](CODE_OF_CONDUCT.md).
