# Build host runbook

The build host is a self-hosted Windows machine that builds Chromium with Ghost for CI. GitHub's hosted runners can't: a full build takes hours and needs a persistent checkout and build directory.

What it runs, through `.github/workflows/build.yml` and `tools/builder.py`:

| When | Command | Steps |
|---|---|---|
| Every pull request and push to `main` | `builder.py pr` | Apply the patch series; build `chrome`, `ghost_unittests` and `ghost_browsertests`; run both test suites |
| Nightly, 01:00 UTC | `builder.py nightly` | The same, then build `mini_installer`, run the installer smoke test in Windows Sandbox, and run the egress audit |

The workflow does nothing until a maintainer finishes the setup below and sets `GHOST_BUILDER` to `enabled`.

## The machine

- **Use a dedicated machine or VM, not a workstation.** The builder runs code from pull requests.
- **Hardware:** follow [windows.md](windows.md#hardware). The reference machine (6 cores, 32 GB) needed 11 h 17 min for a full build. A full build happens on the first run and after every milestone move. Incremental builds take minutes.
- **Disk:**
  - The checkout takes about 30 GB, and a development build directory about 21 GB.
  - Keep 250 GB free on an NTFS or ReFS volume, ideally a Dev Drive.
- **Software:**
  - everything in [windows.md](windows.md#prerequisites);
  - Python 3.11 or newer, and Git, on `PATH`;
  - Windows Sandbox, for the nightly installer smoke test ([windows.md](windows.md#installer)).

## One-time setup

The maintainer performs these steps. Several need administrator rights or change security settings.

1. **Create a standard (non-administrator) local account** for the builder, for example `ghost-builder`. Everything below runs as that account, and the build root belongs to it.
2. **Add the Defender exclusion** for the build root only, for example `D:\ghost` ([windows.md](windows.md#prerequisites)).
3. **As the builder account, check the machine and create the checkout:**

   ```
   python tools\check_env.py --build-root D:\ghost
   python tools\bootstrap.py --root D:\ghost
   ```
4. **Register the runner.** Go to the repository's Settings → Actions → Runners → New self-hosted runner, and follow GitHub's steps for Windows x64.
   - Give it the extra label `ghost-builder`. GitHub adds `self-hosted` and `windows` itself.
   - **Run it with `run.cmd` in the builder account's logon session, not as a service.** Windows Sandbox and the egress audit's browser window need an interactive desktop, and a service has none.
   - Keep that session signed in, for example locked, or configure automatic sign-in for the account. Automatic sign-in stores the account's password in the registry; that is the maintainer's call.
5. **Set the repository variables** in Settings → Secrets and variables → Actions → Variables:

   | Variable | Value |
   |---|---|
   | `GHOST_BUILD_ROOT` | the build root, e.g. `D:\ghost` |
   | `GHOST_BUILD_JOBS` | optional: a parallel-job limit, e.g. `10` on 32 GB machines ([windows.md](windows.md#troubleshooting)) |
   | `GHOST_BUILDER` | `enabled`, last, once the steps above are done |
6. **Keep fork pull requests off the machine until reviewed.**
   - Create the label `safe to build`.
   - In Settings → Actions → General, require approval for workflow runs from all outside collaborators.
   - A fork's pull request builds only when a maintainer adds the label. Commits pushed after that don't build until the label is added again.

The first run does everything once:
- it runs bootstrap again to record the synced revision, which finds little to do;
- it applies the series;
- it builds everything from scratch, which takes hours.

## What a run does

`builder.py` prints its plan before it acts, and `--dry-run` prints only the plan.

1. It fetches the commit under test from the CI workspace into `src/ghost` and checks it out.
2. **Chromium sync.** If the commit pins another `CHROMIUM_COMMIT` than the one in `<root>\.ghost-synced`, it runs `bootstrap.py`, which moves Chromium to the new pin. After a milestone move, this and the rebuild that follows take hours.
3. **Patch series.** It re-applies the series only if the series or the pin changed, or if Chromium's tree has local changes. `<root>\.ghost-applied` records a digest of the series last applied.
   - Re-applying rewrites every patched file, and the build tool rebuilds by modification time.
   - `chrome/install_static/install_modes.h` alone reaches over a hundred files. Re-applying an unchanged series would rebuild all of them on every run.
4. It writes `src\out\ci\args.gn` with `import("//ghost/build/args/dev.gn")`, runs `gn gen`, and builds.
5. It runs the test suites, which write their summaries to `<root>\ci-results`. The nightly run then adds the installer smoke test and the egress audit.

## Operating the builder

- **Never rename or move `src\out\ci`.** The build tool keys its state by path, and a renamed directory rebuilds from scratch ([windows.md](windows.md#troubleshooting)).
- **The egress audit's NetLog holds the builder's IP addresses.** It stays in `<root>\ci-results` and is never uploaded.
- **Out-of-memory compiler failures** on busy machines: set `GHOST_BUILD_JOBS`, and don't run anything else beside a full build.
- **To force a clean state,** delete `<root>\.ghost-synced` and `<root>\.ghost-applied`. The next run then re-syncs and re-applies. Deleting `src\out\ci` forces a full rebuild; avoid that unless the build directory is corrupt.
- **Plan milestone moves.** The pull request that moves `CHROMIUM_VERSION` to a new milestone holds the builder for hours.

## Security

- **The builder holds no secrets.** The workflow's token is read-only, and the build needs no credentials. Signing keys never go on this machine: releases are built and signed on a separate builder from Phase 2 ([testing.md](../testing.md#ci)).
- **Code from pull requests runs as the builder account.** That is why the account is a standard user on a dedicated machine, and why fork pull requests need a maintainer's label each time.
- **Network access** is needed to `github.com` and `chromium.googlesource.com`, plus Chromium's dependency hosts during a sync.
