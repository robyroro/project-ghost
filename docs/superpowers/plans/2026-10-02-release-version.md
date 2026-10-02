# Release Version Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Release builds carry ADR 0007's release version (`152.0.7977.14901`) for the installer, the updater and Windows, while everything the web and Google see keeps Chromium's version (`152.0.7977.149`).

**Architecture:** One hook patch overrides `version_info`'s MAJOR/MINOR/BUILD/PATCH with `CHROMIUM_VERSION` through `version.py -e` arguments, so `version_info` always reports the Chromium release. `chrome/VERSION` carries the release version, written by a new release tool, and reaches the installer and the resources unpatched. Ghost code that needs the release version (the upgrade detector, the About page) reads it from a new `//ghost/version` target. A spike with a fake respin proves it end to end.

**Tech Stack:** Python 3 stdlib (`tools/`, `unittest`), GN, C++ (Chromium `base`, gtest, browser tests), git patch series (`tools/patches.py`), Windows Sandbox (`tools/installer_smoke.py`).

**Spec:** [docs/superpowers/specs/2026-10-02-release-version-design.md](../specs/2026-10-02-release-version-design.md)

**Deviation from the spec:** the spec has `//ghost/build/version.gni` write a VERSION-format file at `gn gen` time. `version.py` already overrides keys with `-e KEY=expr` arguments, applied after the files are read (`GenerateValues` in `build/util/version.py`). So the hook passes `extra_args` instead, and nothing is written at `gn gen`. Task 13 updates the spec to match.

---

## Conventions for every task

**Paths.**
- `WEBOPS=/c/Users/robyv/Desktop/DLU/webops` (this repository).
- `SRC=$WEBOPS/chromium/src` (Chromium, on branch `ghost/152.0.7977.149`).
- `$SRC/ghost` is a clone of `WEBOPS`, on `main`, tracking `origin` (a `file://` URL to `WEBOPS`).

**Shell:** Git Bash. Build environment, in each shell that builds:

```bash
export PATH="/c/src/depot_tools:$PATH" DEPOT_TOOLS_WIN_TOOLCHAIN=0
```

**Getting `//ghost` changes into the build:**
1. Commit in `WEBOPS`.
2. Pull into the checkout: `git -C $SRC/ghost pull --ff-only`.
3. Build.

Never copy files with PowerShell's `Copy-Item`. It keeps the old modification time, and the build skips the file.

**Changing Chromium (a patch):**
1. Edit in `$SRC`.
2. `git -C $SRC commit -am "<message>"`.
3. `cd $WEBOPS && python tools/patches.py export --src chromium/src`.
4. Commit `patches/` in `WEBOPS`.

One concern per patch. The commit message's first line names the upstream directory, as the existing series does (`installer: …`, `prefs: …`).

**Commits:** no `Co-Authored-By` or "Generated with" lines. The user is the sole author.

**Tooling checks before every commit in `WEBOPS`:**

```bash
cd $WEBOPS && git add -A && python tools/lint.py && python -m unittest discover -s tools/tests -t tools
```

`lint.py` checks only tracked files, so stage first. CI runs the tests with `-t tools`, and without it every import fails.

**Never:**
- rename or move an `out\` directory;
- edit Chromium sources while a build runs;
- re-run `patches.py apply` during this plan (it rewrites every patched file and rebuilds their dependents);
- run an installer on this machine. Installers run only in Windows Sandbox, through `tools/installer_smoke.py sandbox`.

---

## File map

| File | Responsibility |
|---|---|
| `tools/release_version.py` (new) | Parse release tags, compute the release version, write it into `chrome/VERSION` |
| `tools/tests/test_release_version.py` (new) | Tests for it |
| `tools/installer_smoke.py` (modify) | Expect the release version on disk and in the registry, and Chromium's from the running browser |
| `tools/tests/test_installer_smoke.py` (modify) | Tests for that split |
| `build/version.gni` (new) | Read `CHROMIUM_VERSION`, check it against `chrome/VERSION` at `gn gen`, export the `-e` overrides |
| `version/BUILD.gn` (new) | Target `//ghost/version`, and the generated header |
| `version/version_values.h.version` (new) | Template for the generated header |
| `version/version.h`, `version/version.cc` (new) | `ReleaseVersion()`, `ChromiumVersion()`, `DisplayVersion()`, `FormatDisplayVersion()` |
| `version/version_unittest.cc` (new) | Unit tests, including that `version_info` is the Chromium release |
| `version/installed_version_poller_unittest.cc` (new) | The installed release version is not an update |
| `test/web_version_browsertest.cc` (new) | Pages see the Chromium release; About and `chrome://version` show the display version |
| `BUILD.gn` (modify) | Add the new tests to `ghost_unittests` and `ghost_browsertests` |
| `patches/0012-…`, `0013-…`, `0014-…` (new, exported) | Hooks: `version_info`, the upgrade detector, `version_ui` |
| `docs/superpowers/specs/2026-10-02-release-version-spike.md` (new) | Spike results and the audit list |
| `docs/adr/0007-version-numbers.md`, `docs/roadmap.md`, the spec (modify) | Implementation note, the Phase 2 section, the `-e` deviation |

---

### Task 0: Preconditions

- [ ] **Step 1: Check the state.**

```bash
cd $WEBOPS && git status -sb | head -1
git -C $SRC status --porcelain --untracked-files=no | wc -l
git -C $SRC rev-parse --abbrev-ref HEAD
git -C $SRC/ghost status -sb | head -1
df -h /c | tail -1
```

Expected:
- `## main...origin/main` with nothing ahead, or ahead only by this plan's commit.
- `0` uncommitted changes in `$SRC`.
- Branch `ghost/152.0.7977.149`.
- `$SRC/ghost` on `main...origin/main`.
- At least 60 GB free. The spike and the mutation check each rebuild a large part of the browser.

If `$SRC/ghost` is detached (`builder.py` leaves it that way), put it back on `main` without touching files:

```bash
git -C $SRC/ghost fetch -q origin && git -C $SRC/ghost branch -f main origin/main && git -C $SRC/ghost checkout -q main
```

---

### Task 1: The release tool

**Files:**
- Create: `tools/release_version.py`
- Test: `tools/tests/test_release_version.py`

- [ ] **Step 1: Write the failing tests.** Create `tools/tests/test_release_version.py`:

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import contextlib
import io
import tempfile
import unittest
from pathlib import Path

import release_version as rv
import repo

CHROMIUM = "152.0.7977.149"


def as_tuple(version: str) -> tuple[int, ...]:
    return tuple(int(p) for p in version.split("."))


class ParseTagTest(unittest.TestCase):
    def test_respin_zero_has_no_suffix(self):
        self.assertEqual(rv.parse_tag("152.0.7977.149", CHROMIUM).version, "152.0.7977.14900")

    def test_respin_fills_the_last_two_digits(self):
        self.assertEqual(rv.parse_tag("152.0.7977.149-1", CHROMIUM).version, "152.0.7977.14901")
        self.assertEqual(rv.parse_tag("152.0.7977.149-99", CHROMIUM).version, "152.0.7977.14999")

    def test_releases_sort_below_the_next_upstream_release(self):
        versions = [rv.parse_tag("152.0.7977.149", CHROMIUM).version,
                    rv.parse_tag("152.0.7977.149-1", CHROMIUM).version,
                    rv.parse_tag("152.0.7977.160", "152.0.7977.160").version]
        tuples = [as_tuple(v) for v in versions]
        self.assertEqual(tuples, sorted(tuples))
        self.assertEqual(len(set(tuples)), 3)

    def test_rejects_another_chromium_release(self):
        with self.assertRaisesRegex(rv.ReleaseVersionError, "CHROMIUM_VERSION is 152.0.7977.149"):
            rv.parse_tag("152.0.7977.140-1", CHROMIUM)

    def test_rejects_a_spelled_out_respin_zero(self):
        for tag in ("152.0.7977.149-0", "152.0.7977.149-01"):
            with self.assertRaisesRegex(rv.ReleaseVersionError, "respin 0 has no suffix"):
                rv.parse_tag(tag, CHROMIUM)

    def test_rejects_malformed_tags(self):
        for tag in ("v152.0.7977.149", "152.0.7977", "152.0.7977.149-", "152.0.7977.149-a"):
            with self.assertRaises(rv.ReleaseVersionError, msg=tag):
                rv.parse_tag(tag, CHROMIUM)

    def test_rejects_a_hundredth_respin(self):
        with self.assertRaisesRegex(rv.ReleaseVersionError, "two digits"):
            rv.parse_tag("152.0.7977.149-100", CHROMIUM)

    def test_rejects_a_fourth_part_over_16_bits(self):
        self.assertEqual(rv.parse_tag("152.0.7977.655-35", "152.0.7977.655").version,
                         "152.0.7977.65535")
        for tag, chromium in (("152.0.7977.655-36", "152.0.7977.655"),
                              ("152.0.7977.656", "152.0.7977.656")):
            with self.assertRaisesRegex(rv.ReleaseVersionError, "65535", msg=tag):
                rv.parse_tag(tag, chromium)


class WriteTest(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.src = Path(tmp.name)
        (self.src / "chrome").mkdir()
        self.version_file = self.src / "chrome" / "VERSION"
        self.version_file.write_text("MAJOR=152\nMINOR=0\nBUILD=7977\nPATCH=149\n",
                                     encoding="utf-8", newline="\n")

    def test_writes_only_the_patch_line(self):
        rv.write_version_file(self.src, rv.parse_tag("152.0.7977.149-1", CHROMIUM))
        self.assertEqual(self.version_file.read_bytes(),
                         b"MAJOR=152\nMINOR=0\nBUILD=7977\nPATCH=14901\n")

    def test_refuses_to_write_twice(self):
        release = rv.parse_tag("152.0.7977.149-1", CHROMIUM)
        rv.write_version_file(self.src, release)
        with self.assertRaisesRegex(rv.ReleaseVersionError, "already written"):
            rv.write_version_file(self.src, release)

    def test_refuses_a_checkout_at_another_pin(self):
        self.version_file.write_text("MAJOR=152\nMINOR=0\nBUILD=7977\nPATCH=140\n",
                                     encoding="utf-8", newline="\n")
        with self.assertRaisesRegex(rv.ReleaseVersionError, "another pin"):
            rv.write_version_file(self.src, rv.parse_tag("152.0.7977.149-1", CHROMIUM))
        self.assertIn("PATCH=140", self.version_file.read_text(encoding="utf-8"))


class MainTest(unittest.TestCase):
    def test_compute_prints_the_release_version(self):
        pinned = repo.read_chromium_version()
        major_minor_build, patch = pinned.rsplit(".", 1)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = rv.main(["compute", "--tag", pinned + "-1"])
        self.assertEqual(code, 0)
        self.assertEqual(out.getvalue(), f"{major_minor_build}.{int(patch) * 100 + 1}\n")

    def test_errors_exit_1(self):
        with contextlib.redirect_stderr(io.StringIO()) as err:
            code = rv.main(["compute", "--tag", "1.2.3.4"])
        self.assertEqual(code, 1)
        self.assertIn("CHROMIUM_VERSION", err.getvalue())


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run them; they fail.**

Run: `cd $WEBOPS && python -m unittest discover -s tools/tests -t tools -p test_release_version.py`
Expected: an error, `ModuleNotFoundError: No module named 'release_version'`.

- [ ] **Step 3: Write the tool.** Create `tools/release_version.py`:

```python
#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Release versions (ADR 0007): from a release tag to chrome/VERSION.

Release tags are CHROMIUM_VERSION, optionally followed by -<respin>:
152.0.7977.149 is respin 0, 152.0.7977.149-1 is respin 1. The release version
keeps Chromium's MAJOR.MINOR.BUILD and sets the fourth part to
PATCH * 100 + respin, so each release sorts above the one before it.

  compute --tag T          print T's release version
  write --src S --tag T    write it into S/chrome/VERSION (release builds only)

Only the release pipeline writes. Development builds keep upstream's
chrome/VERSION, which then equals CHROMIUM_VERSION.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

import repo

FIELD_MAX = 65535  # Windows version resources hold 16 bits per part.
RESPIN_LIMIT = 100

_TAG_RE = re.compile(r"^(\d+\.\d+\.\d+\.\d+)(?:-([1-9]\d*))?$")
_KEYS = ("MAJOR", "MINOR", "BUILD", "PATCH")


class ReleaseVersionError(Exception):
    pass


@dataclass(frozen=True)
class Release:
    chromium: tuple[int, int, int, int]
    respin: int

    @property
    def patch(self) -> int:
        return self.chromium[3] * 100 + self.respin

    @property
    def version(self) -> str:
        major, minor, build, _ = self.chromium
        return f"{major}.{minor}.{build}.{self.patch}"


def parse_tag(tag: str, chromium_version: str) -> Release:
    m = _TAG_RE.match(tag)
    if not m:
        raise ReleaseVersionError(f"release tag {tag!r} is not <chromium version>[-<respin>]; "
                                  "respin 0 has no suffix")
    if m.group(1) != chromium_version:
        raise ReleaseVersionError(f"release tag {tag!r} is for Chromium {m.group(1)}, "
                                  f"but CHROMIUM_VERSION is {chromium_version}")
    major, minor, build, patch = (int(p) for p in chromium_version.split("."))
    respin = int(m.group(2) or 0)
    if respin >= RESPIN_LIMIT:
        raise ReleaseVersionError(f"respin {respin} does not fit in the two digits "
                                  "ADR 0007 gives it")
    if patch * 100 + respin > FIELD_MAX:
        raise ReleaseVersionError(f"{patch} * 100 + {respin} exceeds {FIELD_MAX}, the largest "
                                  "Windows version field; ADR 0007's scheme has to change")
    return Release((major, minor, build, patch), respin)


def write_version_file(src: Path, release: Release) -> None:
    path = src / "chrome" / "VERSION"
    text = path.read_text(encoding="utf-8")
    values = dict(line.split("=", 1) for line in text.splitlines() if "=" in line)
    current = ".".join(values.get(k, "?") for k in _KEYS)
    chromium = ".".join(str(p) for p in release.chromium)
    if current != chromium:
        raise ReleaseVersionError(f"{path} is {current}, not Chromium {chromium}: it was "
                                  "already written, or the checkout is at another pin")
    path.write_text(re.sub(r"(?m)^PATCH=\d+$", f"PATCH={release.patch}", text),
                    encoding="utf-8", newline="\n")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    compute = sub.add_parser("compute", help="print the release version")
    compute.add_argument("--tag", required=True)
    write = sub.add_parser("write", help="write the release version into chrome/VERSION")
    write.add_argument("--src", type=Path, required=True, help="Chromium checkout (the src dir)")
    write.add_argument("--tag", required=True)
    args = parser.parse_args(argv)
    try:
        release = parse_tag(args.tag, repo.read_chromium_version())
        if args.command == "write":
            write_version_file(args.src, release)
    except (ReleaseVersionError, repo.RepoError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    print(release.version)
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

The "already written" and "another pin" cases share one message, which names both causes. The tests match on either phrase.

- [ ] **Step 4: Run them; they pass.**

Run: `cd $WEBOPS && python -m unittest discover -s tools/tests -t tools -p test_release_version.py`
Expected: `Ran 13 tests` … `OK`.

- [ ] **Step 5: Mutation check.** Each mutation must make at least one test fail. Restore the file after each.

```bash
cd $WEBOPS && cp tools/release_version.py /tmp/rv.bak
for pair in 'if respin >= RESPIN_LIMIT:|if False:' \
            'if patch * 100 + respin > FIELD_MAX:|if False:' \
            'if current != chromium:|if False:' \
            '(?:-([1-9]\\d*))?|(?:-(\\d+))?'; do
  python - "$pair" <<'EOF'
import sys; old, new = sys.argv[1].split("|", 1); p = "tools/release_version.py"
s = open(p, encoding="utf-8").read(); assert old in s, old
open(p, "w", encoding="utf-8", newline="\n").write(s.replace(old, new, 1))
EOF
  python -m unittest discover -s tools/tests -t tools -p test_release_version.py 2>&1 | tail -1
  cp /tmp/rv.bak tools/release_version.py
done
```

Expected: four lines, each starting with `FAILED`.

- [ ] **Step 6: Commit.**

```bash
cd $WEBOPS && git add tools/release_version.py tools/tests/test_release_version.py \
  && python tools/lint.py && python -m unittest discover -s tools/tests -t tools 2>&1 | tail -1
git commit -q -m "release: compute and write the release version

ADR 0007's release version comes from the release tag: CHROMIUM_VERSION,
then -<respin> unless the respin is 0. tools/release_version.py prints it,
and writes it into chrome/VERSION for release builds. It refuses a tag
for another Chromium release, a spelled-out respin 0, a 100th respin, a
fourth part beyond 16 bits, and a chrome/VERSION it has already written."
```

---

### Task 2: The smoke test expects two versions

The installed files and the registry carry the release version. The running browser reports Chromium's (`Browser.getVersion`, from `version_info`). Until now the smoke test used `CHROMIUM_VERSION` for both.

**Files:**
- Modify: `tools/installer_smoke.py` (docstring lines 15–23, `Expectations` lines 61–70, `expectations()` lines 73–98, lines 142, 149, 190, 266, 347, `run_in_sandbox`, `main`)
- Test: `tools/tests/test_installer_smoke.py`

- [ ] **Step 1: Update the tests first.**

In `tools/tests/test_installer_smoke.py`, change `EXP` to two distinct versions:

```python
EXP = installer_smoke.Expectations(
    product_path="Project Ghost", app_name="Project Ghost", prog_id_prefix="GhostHTM",
    pdf_prog_id_prefix="GhostPDF", url_scheme="projectghost", product_name="Project Ghost",
    company_name="Project Ghost", release_version="152.0.7977.14001",
    web_version="152.0.7977.140")
```

In `INSTALLED`, set the Apps & features entry to the release version:

```python
    "uninstall": {"DisplayName": "Project Ghost", "Publisher": "Project Ghost",
                  "DisplayVersion": "152.0.7977.14001"},
```

Replace `ExpectationsTest.test_come_from_the_branding_directory_and_the_pin` with:

```python
    def test_come_from_the_branding_directory_the_pin_and_the_installer(self):
        exp = installer_smoke.expectations(repo.REPO_ROOT, release_version="152.0.7977.14901")
        self.assertEqual(exp, installer_smoke.Expectations(
            product_path="Project Ghost", app_name="Project Ghost", prog_id_prefix="GhostHTM",
            pdf_prog_id_prefix="GhostPDF", url_scheme="projectghost",
            product_name="Project Ghost", company_name="Project Ghost",
            release_version="152.0.7977.14901", web_version=repo.read_chromium_version()))
```

Add to the installed-state tests, the class holding `test_apps_and_features_entry_names_the_product_and_version`:

```python
    def test_apps_and_features_shows_the_release_version_not_chromiums(self):
        entry = dict(INSTALLED["uninstall"], DisplayVersion=EXP.web_version)
        self.assertTrue(any("DisplayVersion" in f for f in self.failures(uninstall=entry)))
```

Replace `LaunchTest.test_the_browser_reports_the_pinned_version` with:

```python
    def test_the_browser_reports_chromiums_version(self):
        self.assertEqual(installer_smoke.evaluate_launch(
            {"product": "Chrome/152.0.7977.140"}, EXP), [])
        self.assertTrue(installer_smoke.evaluate_launch({"product": "Chrome/151.0.1.2"}, EXP))

    def test_the_browser_never_reports_the_release_version(self):
        self.assertTrue(installer_smoke.evaluate_launch(
            {"product": "Chrome/152.0.7977.14001"}, EXP))
```

- [ ] **Step 2: Run them; they fail.**

Run: `cd $WEBOPS && python -m unittest discover -s tools/tests -t tools -p test_installer_smoke.py`
Expected: errors, `TypeError: Expectations.__init__() got an unexpected keyword argument 'release_version'`.

- [ ] **Step 3: Change `tools/installer_smoke.py`.**

Docstring: replace lines 15–23 with:

```python
What is checked comes from branding/, CHROMIUM_VERSION and the installer's
file version, which is the release version (ADR 0007):
- after install: the browser, and setup.exe in the release version's
  directory; the Apps & features entry with the product name, publisher and
  release version; registration as a browser (StartMenuInternet and the HTML
  ProgID); Start menu and Desktop shortcuts named for the product and opening
  it; the product name in chrome.exe's file properties; and nothing named
  Chromium, so Ghost can be installed next to Chromium;
- launch: the installed browser starts and reports CHROMIUM_VERSION, the
  version websites see;
- after uninstall: none of the above is left.
```

In `Expectations`, replace `version: str          # CHROMIUM_VERSION` with:

```python
    release_version: str  # the installer's file version: install directory, Apps & features
    web_version: str      # CHROMIUM_VERSION: what the running browser reports
```

Change the signature of `expectations` to `def expectations(root: Path, release_version: str) -> Expectations:`, and replace its last argument line `version=repo.read_chromium_version(root))` with:

```python
        release_version=release_version,
        web_version=repo.read_chromium_version(root))
```

Add after `_ps_quote`:

```python
def installer_version(installer: Path) -> str:
    """The installer's file version: the chrome/VERSION of the build that made it."""
    return _powershell_json(f"(Get-Item -LiteralPath {_ps_quote(str(installer))})"
                            ".VersionInfo.FileVersion")
```

Replace the uses:
- line 142: `failures.append(f"setup.exe is not in {exp.release_version}\\Installer")`
- line 149: `("DisplayVersion", exp.release_version)):`
- line 190: `want = f"Chrome/{exp.web_version}"`
- line 266: `"setup.exe": (app_dir / exp.release_version / "Installer" / "setup.exe").exists()},`
- line 347: `setup = Path(installed["chrome_exe"]).parent / exp.release_version / "Installer" / "setup.exe"`

In `run_in_sandbox`, replace `json.dumps(expectations(repo.REPO_ROOT).__dict__)` with:

```python
        json.dumps(expectations(repo.REPO_ROOT, installer_version(installer)).__dict__)
```

In `main`, replace `else expectations(repo.REPO_ROOT))` with:

```python
           else expectations(repo.REPO_ROOT, installer_version(args.installer)))
```

Confirm nothing else reads the old field:

```bash
cd $WEBOPS && grep -n "exp\.version\|\.version\b" tools/installer_smoke.py tools/tests/test_installer_smoke.py
```

Expected: no output.

- [ ] **Step 4: Run all tooling tests; they pass.**

Run: `cd $WEBOPS && python -m unittest discover -s tools/tests -t tools 2>&1 | tail -1`
Expected: `OK`.

- [ ] **Step 5: Check it against the current installer.** No install happens here; this only reads the file version:

```bash
cd $WEBOPS/tools && python -c "
from pathlib import Path
import installer_smoke
print(installer_smoke.installer_version(Path('../chromium/src/out/vanilla/mini_installer.exe')))"
```

Expected: `152.0.7977.149`.

- [ ] **Step 6: Commit.**

```bash
cd $WEBOPS && git add -A && python tools/lint.py && python -m unittest discover -s tools/tests -t tools 2>&1 | tail -1
git commit -q -m "installer: expect the release version on disk, Chromium's from the browser

ADR 0007 gives the installed files and the Apps & features entry the
release version, while the running browser reports CHROMIUM_VERSION. The
smoke test took CHROMIUM_VERSION for both. It now reads the release
version from the installer's file version, and checks that the browser
never reports it."
```

---

### Task 3: `//ghost/version`

**Files:**
- Create: `build/version.gni`, `version/BUILD.gn`, `version/version_values.h.version`, `version/version.h`, `version/version.cc`, `version/version_unittest.cc`
- Modify: `BUILD.gn` (`ghost_unittests`)

- [ ] **Step 1: Create `build/version.gni`.**

```gn
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# The two versions a build carries (ADR 0007):
# - chrome/VERSION is the release version. Release builds set its PATCH to
#   CHROMIUM_VERSION's PATCH * 100 + respin (tools/release_version.py).
#   Development builds keep upstream's.
# - CHROMIUM_VERSION is the Chromium release. version_info reports it, and so
#   does everything that tells a website or Google the browser's version
#   (patches/0012 passes ghost_chromium_version_args to version.py).
#
# Imported by //base/version_info, so the checks below run on every gn gen.

_chromium_parts =
    string_split(read_file("//ghost/CHROMIUM_VERSION", "trim string"), ".")
_release = read_file("//chrome/VERSION", "scope")

ghost_chromium_version = string_join(".", _chromium_parts)

# version.py applies -e values after reading its files, so these replace
# chrome/VERSION's.
ghost_chromium_version_args = [
  "-e",
  "MAJOR=" + _chromium_parts[0],
  "-e",
  "MINOR=" + _chromium_parts[1],
  "-e",
  "BUILD=" + _chromium_parts[2],
  "-e",
  "PATCH=" + _chromium_parts[3],
]

_release_version =
    "${_release.MAJOR}.${_release.MINOR}.${_release.BUILD}.${_release.PATCH}"
assert(
    "${_release.MAJOR}.${_release.MINOR}.${_release.BUILD}" ==
        "${_chromium_parts[0]}.${_chromium_parts[1]}.${_chromium_parts[2]}",
    "chrome/VERSION is $_release_version, but CHROMIUM_VERSION is " +
        "$ghost_chromium_version: the checkout is not at the pin")

_chromium_patch = _chromium_parts[3]
_release_patch = "${_release.PATCH}"
_patch_ok = _release_patch == _chromium_patch
_digits = [
  "0",
  "1",
  "2",
  "3",
  "4",
  "5",
  "6",
  "7",
  "8",
  "9",
]
foreach(_tens, _digits) {
  foreach(_ones, _digits) {
    if (_release_patch == "$_chromium_patch$_tens$_ones") {
      _patch_ok = true
    }
  }
}
assert(_patch_ok,
       "chrome/VERSION's PATCH is $_release_patch. For CHROMIUM_VERSION " +
           "$ghost_chromium_version it must be $_chromium_patch (a " +
           "development build) or ${_chromium_patch}00 to " +
           "${_chromium_patch}99 (a release; tools/release_version.py)")
```

- [ ] **Step 2: Create `version/version_values.h.version`.**

```c
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Generated from //chrome/VERSION and //ghost/CHROMIUM_VERSION by
// //ghost/version:generate_version_values.

#ifndef GHOST_VERSION_VERSION_VALUES_H_
#define GHOST_VERSION_VERSION_VALUES_H_

#define GHOST_RELEASE_VERSION "@MAJOR@.@MINOR@.@BUILD@.@PATCH@"
#define GHOST_CHROMIUM_VERSION "@CHROMIUM_VERSION@"

#endif  // GHOST_VERSION_VERSION_VALUES_H_
```

- [ ] **Step 3: Create `version/BUILD.gn`.**

```gn
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import("//build/util/process_version.gni")
import("//ghost/build/version.gni")

# MAJOR..PATCH come from chrome/VERSION: the release version.
process_version("generate_version_values") {
  template_file = "version_values.h.version"
  sources = [ "//chrome/VERSION" ]
  extra_args = [
    "-e",
    "CHROMIUM_VERSION='$ghost_chromium_version'",
  ]
  output = "$target_gen_dir/version_values.h"
}

# Linked into //chrome/browser targets (patches/0013, 0014), so it must not
# depend on //chrome/browser.
source_set("version") {
  sources = [
    "version.cc",
    "version.h",
  ]
  deps = [
    ":generate_version_values",
    "//base",
  ]
}
```

- [ ] **Step 4: Create `version/version.h`.**

```cpp
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_VERSION_VERSION_H_
#define GHOST_VERSION_VERSION_H_

#include <string>

namespace base {
class Version;
}

namespace ghost {

// The release version: chrome/VERSION, which the installer, the updater and
// Windows also use. In a release build its fourth part is the Chromium
// release's PATCH * 100 + respin (ADR 0007). In a development build it equals
// ChromiumVersion().
const base::Version& ReleaseVersion();

// The Chromium release this build is based on: CHROMIUM_VERSION. version_info
// reports it (patches/0012), so it is the only version websites and Google
// see.
const base::Version& ChromiumVersion();

// The version as people see it: the Chromium release, then "-<respin>" unless
// the respin is 0. For example "152.0.7977.149-1".
std::string DisplayVersion();

// DisplayVersion() for any pair. `release` must equal `chromium` (a
// development build) or be a release of it.
std::string FormatDisplayVersion(const base::Version& release,
                                 const base::Version& chromium);

}  // namespace ghost

#endif  // GHOST_VERSION_VERSION_H_
```

- [ ] **Step 5: Write the failing unit tests.** Create `version/version_unittest.cc`:

```cpp
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/version/version.h"

#include "base/version.h"
#include "components/version_info/version_info.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ghost {
namespace {

TEST(VersionTest, ADevelopmentBuildShowsTheChromiumRelease) {
  EXPECT_EQ(FormatDisplayVersion(base::Version("152.0.7977.149"),
                                 base::Version("152.0.7977.149")),
            "152.0.7977.149");
}

TEST(VersionTest, RespinZeroHasNoSuffix) {
  EXPECT_EQ(FormatDisplayVersion(base::Version("152.0.7977.14900"),
                                 base::Version("152.0.7977.149")),
            "152.0.7977.149");
}

TEST(VersionTest, ARespinIsAppended) {
  EXPECT_EQ(FormatDisplayVersion(base::Version("152.0.7977.14901"),
                                 base::Version("152.0.7977.149")),
            "152.0.7977.149-1");
  EXPECT_EQ(FormatDisplayVersion(base::Version("152.0.7977.14999"),
                                 base::Version("152.0.7977.149")),
            "152.0.7977.149-99");
}

TEST(VersionTest, TheReleaseVersionIsAReleaseOfTheChromiumRelease) {
  ASSERT_TRUE(ReleaseVersion().IsValid());
  ASSERT_TRUE(ChromiumVersion().IsValid());
  // FormatDisplayVersion() CHECKs that the pair belongs together.
  EXPECT_FALSE(DisplayVersion().empty());
}

// patches/0012: version_info reports CHROMIUM_VERSION, not chrome/VERSION.
// Only a build whose chrome/VERSION differs (a release, or the spike) can
// tell the two apart.
TEST(VersionTest, VersionInfoIsTheChromiumRelease) {
  EXPECT_EQ(version_info::GetVersion(), ChromiumVersion());
}

}  // namespace
}  // namespace ghost
```

In `BUILD.gn`, add to `ghost_unittests`:
- `"version/version_unittest.cc",` in `sources`, after `"components/signin/gaia_cookie_manager_service_unittest.cc",`;
- `"//components/version_info",` and `"//ghost/version",` in `deps`, in sorted position.

- [ ] **Step 6: Commit the header, the tests and the build files, and build; it fails to link.**

```bash
cd $WEBOPS && git add build/version.gni version/BUILD.gn version/version_values.h.version \
  version/version.h version/version_unittest.cc BUILD.gn && python tools/lint.py
git commit -q -m "version: tests and interface for the release and Chromium versions"
git -C $SRC/ghost pull --ff-only
export PATH="/c/src/depot_tools:$PATH" DEPOT_TOOLS_WIN_TOOLCHAIN=0
cd $SRC && cmd //c "autoninja.bat -C out\\vanilla -j 10 ghost_unittests" 2>&1 | tail -5
```

Expected: a link error for undefined symbols `ghost::FormatDisplayVersion`, `ghost::ReleaseVersion`, `ghost::ChromiumVersion` and `ghost::DisplayVersion`. `version.cc` doesn't exist yet. `gn gen` itself must succeed, which shows the asserts in `build/version.gni` pass on a development checkout.

- [ ] **Step 7: Implement.** Create `version/version.cc`:

```cpp
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/version/version.h"

#include <stdint.h>

#include <string>
#include <vector>

#include "base/check.h"
#include "base/check_op.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/version.h"
#include "ghost/version/version_values.h"

namespace ghost {

const base::Version& ReleaseVersion() {
  static const base::NoDestructor<base::Version> version(GHOST_RELEASE_VERSION);
  return *version;
}

const base::Version& ChromiumVersion() {
  static const base::NoDestructor<base::Version> version(
      GHOST_CHROMIUM_VERSION);
  return *version;
}

std::string FormatDisplayVersion(const base::Version& release,
                                 const base::Version& chromium) {
  const std::vector<uint32_t>& r = release.components();
  const std::vector<uint32_t>& c = chromium.components();
  CHECK_EQ(r.size(), 4u);
  CHECK_EQ(c.size(), 4u);
  CHECK(r[0] == c[0] && r[1] == c[1] && r[2] == c[2])
      << release << " is not a release of " << chromium;
  if (r[3] == c[3]) {
    return chromium.GetString();  // A development build.
  }
  CHECK_GE(r[3], c[3] * 100) << release << " is not a release of " << chromium;
  const uint32_t respin = r[3] - c[3] * 100;
  CHECK_LT(respin, 100u) << release << " is not a release of " << chromium;
  if (respin == 0) {
    return chromium.GetString();
  }
  return chromium.GetString() + "-" + base::NumberToString(respin);
}

std::string DisplayVersion() {
  return FormatDisplayVersion(ReleaseVersion(), ChromiumVersion());
}

}  // namespace ghost
```

- [ ] **Step 8: Build and run; they pass.**

```bash
cd $WEBOPS && git add version/version.cc && python tools/lint.py
git commit -q -m "version: the release version, the Chromium version, and how people see them

//ghost/version generates both from chrome/VERSION and CHROMIUM_VERSION.
DisplayVersion() writes ADR 0007's form, 152.0.7977.149-1, with no suffix
for respin 0. build/version.gni stops gn gen when chrome/VERSION is
neither the pinned Chromium release nor a release of it."
git -C $SRC/ghost pull --ff-only
cd $SRC && cmd //c "autoninja.bat -C out\\vanilla -j 10 ghost_unittests" 2>&1 | tail -2
out/vanilla/ghost_unittests.exe --gtest_filter='VersionTest.*' 2>&1 | grep -E "^\[|SUCCESS|FAIL"
```

Expected: `[5/5]` … `SUCCESS: all tests passed.`

- [ ] **Step 9: Check that the `gn gen` asserts work.** Change `chrome/VERSION` temporarily, run only `gn gen`, then restore. Nothing is built.

```bash
cd $SRC && sed -i 's/^PATCH=149$/PATCH=150/' chrome/VERSION && cmd //c "gn.bat gen out\\vanilla" 2>&1 | grep -E "ERROR|must be" | head -3
sed -i 's/^PATCH=150$/PATCH=14901/' chrome/VERSION && cmd //c "gn.bat gen out\\vanilla" 2>&1 | grep -c ERROR
git -C $SRC checkout -- chrome/VERSION && cmd //c "gn.bat gen out\\vanilla" 2>&1 | tail -1
```

Expected:
- First command: an `ERROR` naming PATCH `150`.
- Second command: `0`, because 14901 is a release of 149.
- Third command: `Done. Made … targets`.

`gn gen` doesn't touch sources, so the next build only reruns what `chrome/VERSION` feeds. Run `git -C $SRC status --porcelain --untracked-files=no` and expect no output.

---

### Task 4: Patch 0012, `version_info` reads `CHROMIUM_VERSION`

**Files:**
- Modify (patch): `$SRC/base/version_info/BUILD.gn`

- [ ] **Step 1: Edit.** In `$SRC/base/version_info/BUILD.gn`, add the import after `import("//build/util/process_version.gni")`:

```gn
import("//ghost/build/version.gni")
```

In `process_version("generate_version_info")`, add after `output = "$target_gen_dir/version_info_values.h"`:

```gn
  # Ghost: report the Chromium release, not chrome/VERSION's release version
  # (ADR 0007), to everything that reads version_info.
  extra_args = ghost_chromium_version_args
```

- [ ] **Step 2: Commit the patch and export it.**

```bash
git -C $SRC commit -q -am "base: report the Chromium release through version_info

Ghost's release builds put the release version in chrome/VERSION for the
installer, the updater and Windows (ADR 0007). version_info is what the
User-Agent, Client Hints and the Chrome Web Store report, so it keeps the
Chromium release: version.py's -e arguments from //ghost/build/version.gni
override chrome/VERSION's values."
cd $WEBOPS && python tools/patches.py export --src chromium/src && git status --short patches
```

Expected: `?? patches/0012-base-report-the-Chromium-release-through-version_info.patch`.

```bash
cd $WEBOPS && git add patches && python tools/lint.py && git commit -q -m "patches: version_info reports the Chromium release"
```

- [ ] **Step 3: Build and run the unit tests.** The generated `version_info_values.h` has the same content in a development build. The build may still recompile what includes it, if the build tool doesn't skip unchanged outputs.

```bash
cd $SRC && time cmd //c "autoninja.bat -C out\\vanilla -j 10 ghost_unittests" 2>&1 | tail -2
out/vanilla/ghost_unittests.exe 2>&1 | tail -2
```

Expected: `SUCCESS: all tests passed.` (16 tests). Note the build time; it goes into the spike notes.

---

### Task 5: Patch 0013, the upgrade detector compares with the release version

The installed version (registry `pv`, install directory) is the release version. The upgrade detector compared it with `version_info`, which is now the Chromium release. Without this patch every release build would see an update on each launch.

**Files:**
- Create: `version/installed_version_poller_unittest.cc`
- Modify: `BUILD.gn` (`ghost_unittests`)
- Modify (patch): `$SRC/chrome/browser/upgrade_detector/installed_version_poller.cc` (lines 24, 57, 189), `upgrade_detector_impl.cc` (lines 41, 551), `BUILD.gn` (`:impl`, the `!is_chromeos` deps)

- [ ] **Step 1: Write the test.** Create `version/installed_version_poller_unittest.cc`:

```cpp
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// patches/0013: the installed version is the release version, so finding it
// installed is not an update. Only a build whose chrome/VERSION differs from
// CHROMIUM_VERSION (a release, or the spike) tells this apart from upstream.

#include "chrome/browser/upgrade_detector/installed_version_poller.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/test/task_environment.h"
#include "chrome/browser/upgrade_detector/build_state.h"
#include "chrome/browser/upgrade_detector/get_installed_version.h"
#include "chrome/browser/upgrade_detector/installed_version_monitor.h"
#include "ghost/version/version.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ghost {
namespace {

class IdleMonitor final : public InstalledVersionMonitor {
 public:
  void Start(Callback callback) override {}
};

TEST(InstalledVersionPollerTest, TheInstalledReleaseIsNotAnUpdate) {
  base::test::TaskEnvironment task_environment{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  BuildState build_state;
  bool polled = false;
  InstalledVersionPoller poller(
      &build_state,
      base::BindRepeating(
          [](bool* polled, InstalledVersionCallback callback) {
            *polled = true;
            std::move(callback).Run(
                InstalledAndCriticalVersion(ReleaseVersion()));
          },
          &polled),
      std::make_unique<IdleMonitor>(), task_environment.GetMockTickClock());
  task_environment.RunUntilIdle();
  // kNone is also BuildState's initial value, so check the poll happened.
  ASSERT_TRUE(polled);
  EXPECT_EQ(build_state.update_type(), BuildState::UpdateType::kNone);
}

}  // namespace
}  // namespace ghost
```

In `BUILD.gn`, add `"version/installed_version_poller_unittest.cc",` to `ghost_unittests` sources, and `"//chrome/browser/upgrade_detector:impl",` to its deps.

- [ ] **Step 2: Build and run it.** In a development build it passes before the patch, because both versions are equal. Its failure without the patch is shown in the spike (Task 10).

```bash
cd $WEBOPS && git add -A && python tools/lint.py && git commit -q -m "version: test that the installed release is not an update"
git -C $SRC/ghost pull --ff-only
cd $SRC && cmd //c "autoninja.bat -C out\\vanilla -j 10 ghost_unittests" 2>&1 | tail -2
out/vanilla/ghost_unittests.exe --gtest_filter='InstalledVersionPollerTest.*' 2>&1 | grep -E "SUCCESS|FAIL"
```

Expected: `SUCCESS: all tests passed.`

- [ ] **Step 3: Patch.** In `$SRC/chrome/browser/upgrade_detector/installed_version_poller.cc`:
- line 24: replace `#include "components/version_info/version_info.h"` with `#include "ghost/version/version.h"`;
- line 57: `std::vector<uint32_t> components = ghost::ReleaseVersion().components();`
- line 189: `switch (versions.installed_version.CompareTo(ghost::ReleaseVersion())) {`

In `$SRC/chrome/browser/upgrade_detector/upgrade_detector_impl.cc`:
- line 41: replace `#include "components/version_info/version_info.h"` with `#include "ghost/version/version.h"`;
- line 551: `UpgradeDetected(build_state->critical_version() > ghost::ReleaseVersion()`

In `$SRC/chrome/browser/upgrade_detector/BUILD.gn`, in the `!is_chromeos` `deps +=` list of `source_set("impl")`, add `"//ghost/version",` after `"//content/public/browser",`.

Confirm no `version_info::` use is left in the two files:

```bash
grep -n "version_info::" $SRC/chrome/browser/upgrade_detector/installed_version_poller.cc $SRC/chrome/browser/upgrade_detector/upgrade_detector_impl.cc
```

Expected: no output.

- [ ] **Step 4: Commit, export, build, test.**

```bash
git -C $SRC commit -q -am "upgrade_detector: compare the installed version with Ghost's release version

The installer records the release version (chrome/VERSION), while
version_info reports the Chromium release (ADR 0007). Comparing the two
would find an update on every launch of a release build."
cd $WEBOPS && python tools/patches.py export --src chromium/src && git add patches \
  && python tools/lint.py && git commit -q -m "patches: the upgrade detector compares with the release version"
cd $SRC && cmd //c "autoninja.bat -C out\\vanilla -j 10 chrome ghost_unittests" 2>&1 | tail -2
out/vanilla/ghost_unittests.exe 2>&1 | tail -2
```

Expected: `SUCCESS: all tests passed.` (17 tests).

---

### Task 6: Patch 0014, About and `chrome://version` show the display version

**Files:**
- Create: `test/web_version_browsertest.cc` (the About and `chrome://version` tests; Task 7 adds the page test to the same file)
- Modify: `BUILD.gn` (`ghost_browsertests`)
- Modify (patch): `$SRC/chrome/browser/ui/webui/version/version_ui.cc` (lines 251, 343), `$SRC/chrome/browser/ui/webui/version/BUILD.gn` (`source_set("version")` deps)

- [ ] **Step 1: Write the failing tests.** Create `test/web_version_browsertest.cc`:

```cpp
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// ADR 0007: websites see the Chromium release; people see the release, as
// "<Chromium release>-<respin>".

#include <string>

#include "base/strings/utf_string_conversions.h"
#include "base/version.h"
#include "chrome/browser/ui/webui/version/version_ui.h"
#include "chrome/test/base/chrome_test_utils.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "ghost/version/version.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ghost {
namespace {

using VersionDisplayBrowserTest = InProcessBrowserTest;

IN_PROC_BROWSER_TEST_F(VersionDisplayBrowserTest, AboutShowsTheDisplayVersion) {
  const std::u16string about = VersionUI::GetAnnotatedVersionStringForUi();
  EXPECT_NE(about.find(base::UTF8ToUTF16(DisplayVersion() + " ")),
            std::u16string::npos)
      << about;
}

IN_PROC_BROWSER_TEST_F(VersionDisplayBrowserTest,
                       ChromeVersionShowsTheDisplayVersion) {
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://version")));
  const std::string shown =
      content::EvalJs(chrome_test_utils::GetActiveWebContents(this),
                      "document.getElementById('version').innerText")
          .ExtractString();
  EXPECT_EQ(shown.rfind(DisplayVersion() + " ", 0), 0u) << shown;
}

}  // namespace
}  // namespace ghost
```

`DisplayVersion() + " "` makes `152.0.7977.149` fail to match a page that shows `152.0.7977.149-1`, and the other way round. Both pages put a space after the version.

In `BUILD.gn`, add `"test/web_version_browsertest.cc",` to `ghost_browsertests` sources, and `"//chrome/browser/ui/webui/version",` and `"//ghost/version",` to its deps.

- [ ] **Step 2: Build and run; they pass in a development build.** `DisplayVersion()` equals `version_info`'s version there. The spike (Task 10) is where they must fail without the patch.

```bash
cd $WEBOPS && git add -A && python tools/lint.py && git commit -q -m "version: test the version About and chrome://version show"
git -C $SRC/ghost pull --ff-only
cd $SRC && cmd //c "autoninja.bat -C out\\vanilla -j 10 ghost_browsertests" 2>&1 | tail -2
out/vanilla/ghost_browsertests.exe --gtest_filter='VersionDisplayBrowserTest.*' 2>&1 | grep -E "SUCCESS|FAIL"
```

Expected: `SUCCESS: all tests passed.`

- [ ] **Step 3: Patch.** In `$SRC/chrome/browser/ui/webui/version/version_ui.cc`:
- add `#include "ghost/version/version.h"` after `#include "components/version_info/version_info.h"` (line 37);
- lines 250–251 become:

```cpp
  html_source->AddString(version_ui::kVersion, ghost::DisplayVersion());
```

- line 343, in `GetAnnotatedVersionStringForUi()`, becomes:

```cpp
      base::UTF8ToUTF16(ghost::DisplayVersion()),
```

In `$SRC/chrome/browser/ui/webui/version/BUILD.gn`, `source_set("version")`, add `"//ghost/version",` to `deps` after `"//content/public/common",`.

- [ ] **Step 4: Commit, export, build, test.**

```bash
git -C $SRC commit -q -am "version_ui: show Ghost's release as people read it

About and chrome://version show ADR 0007's display version,
152.0.7977.149-1, instead of version_info's Chromium release."
cd $WEBOPS && python tools/patches.py export --src chromium/src && git add patches \
  && python tools/lint.py && git commit -q -m "patches: About and chrome://version show the display version"
cd $SRC && cmd //c "autoninja.bat -C out\\vanilla -j 10 chrome ghost_browsertests" 2>&1 | tail -2
out/vanilla/ghost_browsertests.exe --gtest_filter='VersionDisplayBrowserTest.*' 2>&1 | grep -E "SUCCESS|FAIL"
```

Expected: `SUCCESS: all tests passed.`

---

### Task 7: Pages see the Chromium release

**Files:**
- Modify: `test/web_version_browsertest.cc`, `BUILD.gn` (`ghost_browsertests` deps)

- [ ] **Step 1: Add the test.** In `test/web_version_browsertest.cc`, add `#include "net/test/embedded_test_server/embedded_test_server.h"` to the includes, and before the closing `}  // namespace`:

```cpp
using WebVersionBrowserTest = InProcessBrowserTest;

// Everything a page can ask about the browser's version names the Chromium
// release. When this build's release version differs (a release, or the
// spike), the page never sees it.
IN_PROC_BROWSER_TEST_F(WebVersionBrowserTest, PagesSeeTheChromiumRelease) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  content::WebContents* contents =
      chrome_test_utils::GetActiveWebContents(this);
  const std::string seen =
      content::EvalJs(contents, R"(
        navigator.userAgentData
            .getHighEntropyValues(["fullVersionList", "uaFullVersion"])
            .then(v => navigator.userAgent + " " + JSON.stringify(v)))")
          .ExtractString();

  const std::string chromium = ChromiumVersion().GetString();
  EXPECT_NE(seen.find("\"uaFullVersion\":\"" + chromium + "\""),
            std::string::npos)
      << seen;
  EXPECT_NE(seen.find("\"version\":\"" + chromium + "\""), std::string::npos)
      << seen;

  const std::string release = ReleaseVersion().GetString();
  if (release != chromium) {
    EXPECT_EQ(seen.find(release), std::string::npos) << seen;
  }
}
```

The second `EXPECT_NE` checks `fullVersionList`, whose entries are `{"brand":…,"version":…}`. `http://127.0.0.1` is a secure context, so `navigator.userAgentData` is available.

In `BUILD.gn`, add `"//net:test_support",` to `ghost_browsertests` deps.

- [ ] **Step 2: Build and run.**

```bash
cd $WEBOPS && git add -A && python tools/lint.py && git commit -q -m "version: test that pages see the Chromium release"
git -C $SRC/ghost pull --ff-only
cd $SRC && cmd //c "autoninja.bat -C out\\vanilla -j 10 ghost_browsertests" 2>&1 | tail -2
out/vanilla/ghost_browsertests.exe --gtest_filter='WebVersionBrowserTest.*' 2>&1 | grep -E "SUCCESS|FAIL"
```

Expected: `SUCCESS: all tests passed.`

---

### Task 8: Full development run

- [ ] **Step 1: Build everything and run every suite.**

```bash
cd $SRC && cmd //c "autoninja.bat -C out\\vanilla -j 10 chrome ghost_unittests ghost_browsertests mini_installer" 2>&1 | tail -2
out/vanilla/ghost_unittests.exe 2>&1 | tail -1
out/vanilla/ghost_browsertests.exe 2>&1 | tail -1
cd $WEBOPS && python tools/installer_smoke.py sandbox --installer chromium/src/out/vanilla/mini_installer.exe
```

Expected: `SUCCESS: all tests passed.` twice: 17 unit tests and 15 browser tests. The smoke test prints `ok` for clean machine, install, launch and uninstall, then `PASSED`. Here both versions are `152.0.7977.149`.

- [ ] **Step 2: Push nothing yet.** The commits stay local until the spike passes.

---

### Task 9: The spike build, with respin 1

- [ ] **Step 1: Write the release version.**

```bash
cd $WEBOPS && python tools/release_version.py write --src chromium/src --tag 152.0.7977.149-1
cat chromium/src/chrome/VERSION
```

Expected: `152.0.7977.14901`, then the file with `PATCH=14901`.

- [ ] **Step 2: Build, timing it.** This is the respin rebuild the spec asks to measure.

```bash
cd $SRC && time cmd //c "autoninja.bat -C out\\vanilla -j 10 chrome ghost_unittests ghost_browsertests mini_installer" 2>&1 | tail -3
```

Note the wall time and the number of actions from the last line. If the build is still running at the end of a working session, stop it cleanly (Ctrl+C in an interactive shell, or stop the task). Resume with the same command; never re-run `patches.py apply`.

- [ ] **Step 3: Run the suites.** Now the release version (`152.0.7977.14901`) differs from the Chromium release (`152.0.7977.149`), so every version test checks something real.

```bash
cd $SRC && out/vanilla/ghost_unittests.exe 2>&1 | tail -1
out/vanilla/ghost_browsertests.exe 2>&1 | tail -1
```

Expected: `SUCCESS: all tests passed.` for both.

- [ ] **Step 4: Smoke test in Windows Sandbox.**

```bash
cd $WEBOPS && python tools/installer_smoke.py sandbox --installer chromium/src/out/vanilla/mini_installer.exe
```

Expected: `PASSED`. It now checks:
- `setup.exe` in `152.0.7977.14901\Installer`;
- Apps & features `DisplayVersion` `152.0.7977.14901`;
- the running browser reporting `Chrome/152.0.7977.149`.

---

### Task 10: Spike audit and mutation checks

- [ ] **Step 1: Audit the remaining `version_info` callers.** List the browser-side callers that might compare with an installed version or build paths:

```bash
cd $SRC && grep -rln "version_info::GetVersion\b\|version_info::GetVersion()\|version_info::GetVersionNumber()" \
  --include=*.cc chrome/browser chrome/installer/util chrome/app chrome/common components/component_updater \
  | grep -v -E "_unittest|_browsertest|test/" | sort > /tmp/version_info_callers.txt
wc -l < /tmp/version_info_callers.txt
grep -l -i -E "install|setup|upgrade|relaunch|new_chrome|InstallUtil|GetChromeVersion" $(cat /tmp/version_info_callers.txt) | sort
```

For each file in the second list, read each `version_info` call and classify it:
- **compares with an installed version or builds a path into the install directory:** it needs the release version. Change it to `ghost::ReleaseVersion()` in a hook patch, the same way as Task 5, with its own commit and test.
- **reports the version to a server, to a website, or to the user's profile data:** it keeps the Chromium release. Write down why that's right.

Record every file, the call, and the decision in the spike notes (Step 4).

- [ ] **Step 2: Mutation-check the patches.** Each step below reverts one patch in `$SRC` only (never in `patches/`), rebuilds, runs the named tests, and restores. Expect long rebuilds, the first one especially: it recompiles everything that includes `version_info`.

| Patch | Commit | Paths | Rebuild | Must fail |
|---|---|---|---|---|
| 0012 | `HEAD~2` | `base/version_info/BUILD.gn` | `ghost_unittests ghost_browsertests` | `ghost_unittests`: `VersionTest.VersionInfoIsTheChromiumRelease`; `ghost_browsertests`: `WebVersionBrowserTest.PagesSeeTheChromiumRelease` |
| 0013 | `HEAD~1` | `chrome/browser/upgrade_detector` | `ghost_unittests` | `ghost_unittests`: `InstalledVersionPollerTest.TheInstalledReleaseIsNotAnUpdate` |
| 0014 | `HEAD` | `chrome/browser/ui/webui/version` | `ghost_browsertests` | `ghost_browsertests`: `VersionDisplayBrowserTest.*` (both) |

Before using `HEAD~2`, `HEAD~1` and `HEAD`, confirm the last three commits are 0012, 0013 and 0014, newest last: `git -C $SRC log --oneline -3`. If Task 10 Step 1 added patches, adjust the offsets.

For each row, with `COMMIT`, `PATHS` and `TARGETS` from the table, and each `BINARY` and `FILTER` pair from its last column:

```bash
cd $SRC && git show COMMIT -- PATHS | git apply -R
cmd //c "autoninja.bat -C out\\vanilla -j 10 TARGETS" 2>&1 | tail -1
out/vanilla/BINARY.exe --gtest_filter='FILTER' 2>&1 | grep -E "SUCCESS|FAILED"
git -C $SRC checkout -- PATHS && git -C $SRC status --porcelain --untracked-files=no
```

Expected:
- The named tests print `FAILED` while the patch is reverted.
- The last command prints only ` M chrome/VERSION`. Restore only the row's `PATHS`, so the respin from Task 9 stays written.

- [ ] **Step 3: Restore the development version and rebuild.**

```bash
git -C $SRC checkout -- chrome/VERSION && git -C $SRC status --porcelain --untracked-files=no
cd $SRC && time cmd //c "autoninja.bat -C out\\vanilla -j 10 chrome ghost_unittests ghost_browsertests mini_installer" 2>&1 | tail -1
out/vanilla/ghost_unittests.exe 2>&1 | tail -1 && out/vanilla/ghost_browsertests.exe 2>&1 | tail -1
```

Expected: no uncommitted changes, then `SUCCESS: all tests passed.` twice.

- [ ] **Step 4: Write the spike notes.** Create `docs/superpowers/specs/2026-10-02-release-version-spike.md` with:
- the date, `CHROMIUM_VERSION`, and the respin used (`152.0.7977.149-1`);
- the build times: Task 4 Step 3, Task 9 Step 2 (the respin rebuild) and Task 10 Step 3;
- the smoke test result with the release version;
- the audit list from Step 1, each file with its call and decision;
- the mutation results from Step 2.

Commit:

```bash
cd $WEBOPS && git add docs/superpowers/specs/2026-10-02-release-version-spike.md && python tools/lint.py
git commit -q -m "spec: release version spike results"
```

---

### Task 11: Documentation

**Files:**
- Modify: `docs/adr/0007-version-numbers.md`, `docs/roadmap.md`, `docs/superpowers/specs/2026-10-02-release-version-design.md`

- [ ] **Step 1: ADR 0007 implementation note.** Append to `docs/adr/0007-version-numbers.md`:

```markdown
## Implementation

Added 2026-10-02 (Phase 2, sub-project A; [design](../superpowers/specs/2026-10-02-release-version-design.md)).

- **The cut is at `version_info`, not at each place that sends the version.**
  - Patch 0012 passes `CHROMIUM_VERSION` to `version.py` as `-e` overrides for `//base/version_info`.
  - Every `version_info` caller, including ones a future milestone adds, therefore reports the Chromium release.
  - The installer, the updater and Windows read `chrome/VERSION` unpatched.
- **Ghost code that needs the release version** reads `//ghost/version`.
  - The upgrade detector (patch 0013) compares the installed version with it.
  - About and `chrome://version` (patch 0014) show `DisplayVersion()`.
- **`tools/release_version.py`** computes the release version from the release tag, `<CHROMIUM_VERSION>[-<respin>]`, and writes it into `chrome/VERSION`.
  - It also refuses a fourth part above 65535. With PATCH at 655, that allows respins up to 35.
- **`//ghost/build/version.gni`** stops `gn gen` when `chrome/VERSION` is neither the pinned Chromium release nor a release of it.
```

- [ ] **Step 2: Spec deviation.** In the design spec, section "The cut: at `version_info`, not at the call sites", replace the bullets that begin "It adds a second file", "`version.py` reads the files in order" and "`//ghost/build/version.gni` writes that file" with:

```markdown
- It passes `-e MAJOR=… -e MINOR=… -e BUILD=… -e PATCH=…` to `generate_version_info`. `version.py` applies `-e` values after reading `//chrome/VERSION`, so `version_info` gets the Chromium release.
- `//ghost/build/version.gni` builds those arguments from `CHROMIUM_VERSION` at `gn gen` time. Nothing is written to disk.
```

In "Errors caught at `gn gen`", leave the text unchanged; it already describes the asserts.

- [ ] **Step 3: Roadmap Phase 2 section.** In `docs/roadmap.md`, after the Phase 1 section (its last exit-criteria bullet), add:

```markdown
## Phase 2: release engineering

Runs under a **test identity** until the final name is chosen: test app IDs, a test server domain and test keys, on test machines only ([licensing.md](licensing.md#trademarks)). Moving to the final name is one step, through `branding/` and the server configuration, before the first public build.

Sub-projects, each with its own design and plan in `docs/superpowers/`:

1. **A. Release version.** [ADR 0007](adr/0007-version-numbers.md) in builds.
   **Done 2026-10-02.** `tools/release_version.py`, `//ghost/version`, patches 0012–0014. Spike results are in [the spike notes](superpowers/specs/2026-10-02-release-version-spike.md).
2. **B. Branded `//chrome/updater`.** Our app IDs and server, installed with the browser.
3. **C. Update server.** Omaha 4, CUP-signed responses, no IP retention; components later.
4. **D. Signing.** Authenticode, the CUP key, the CRX3 key, and where the keys live.
5. **E. Release pipeline.** Official build from a tag, SBOM and provenance, staged rollout with a halt switch.
6. **F. Security release runbook,** and one measured milestone move (to 154).

**Exit criteria**
- [ ] An update shipped end to end to test machines.
- [ ] One milestone move measured.
```

Mark sub-project A done only after Task 10 has passed. If it hasn't, leave out the "Done" line.

- [ ] **Step 4: Commit.**

```bash
cd $WEBOPS && git add -A && python tools/lint.py && python -m unittest discover -s tools/tests -t tools 2>&1 | tail -1
git commit -q -m "docs: release version implemented; Phase 2 in the roadmap

ADR 0007 gets its implementation note: the cut is at version_info. The
design spec records -e overrides instead of a generated file. The roadmap
gains a Phase 2 section with sub-projects A to F and the test identity."
```

- [ ] **Step 5: Ask the user before pushing.** Report the commits (`git log --oneline origin/main..main`), the spike numbers and the audit decisions. Push only on their go.
