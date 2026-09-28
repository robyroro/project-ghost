# Security Policy

## Supported versions

There are no releases yet. Until the public alpha, no build is supported for security purposes. After it, security fixes ship for the latest release only, and the auto-updater is the supported way to get them.

## Reporting a vulnerability

Report vulnerabilities privately through GitHub's private vulnerability reporting for this repository (**Security → Report a vulnerability**). Don't open public issues, discussions or pull requests for security problems.

Please include:

- the build and Chromium version (from the About page, once builds exist);
- reproduction steps or a proof of concept;
- the impact you believe it has, e.g. sandbox escape, identity isolation bypass, ability to deanonymize a routed session, update-channel compromise;
- whether the issue also reproduces in upstream Chromium at the same version.

**Bugs that also affect upstream Chromium** should also be reported to Chromium through its [security bug process](https://www.chromium.org/Home/chromium-security/reporting-security-bugs/). We coordinate disclosure with upstream and don't publish details before Chromium's fix ships.

## What counts as a vulnerability here

Besides the usual memory-safety and sandbox issues, we treat these as security bugs:

- state leaking between identities, between Ghost sessions, or from a Ghost session to disk, contrary to the guarantees in [docs/privacy-model.md](docs/privacy-model.md);
- traffic bypassing a configured route (DNS, WebRTC, QUIC, or any other path);
- the browser contacting a host that isn't on the egress allowlist;
- fingerprinting protections that expose different values in different contexts (e.g. main thread versus workers);
- anything that lets a web page, extension or network attacker influence updates, filter lists or other signed data.

## Response targets

These apply once the project has maintainers on call, which is required before the public alpha:

- We acknowledge reports within 3 business days.
- Upstream Chromium security releases ship within 72 hours of Google's release, and within 24–48 hours when an exploit is known to be in the wild.
- Fixes for vulnerabilities in our own code are released together with a public advisory, once users have had time to update.
