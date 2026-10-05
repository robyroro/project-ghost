# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""A release's provenance (SLSA Build Level 1) and checksums (sub-project E).

provenance.intoto.json is an in-toto Statement v1 whose predicate is SLSA
Provenance v1: the artifacts and their SHA-256, and how they were made (the
tag, the commits, the patch series, the toolchain, args.gn, where the tests
ran). It is not signed: the release is built on a maintainer's machine, not
on a build platform that would sign it (Level 2). The artifacts' integrity
comes from their Authenticode signatures and the CRX3 publisher proof.
SHA256SUMS lists every published file, the provenance included.
"""

from __future__ import annotations

import datetime
import hashlib
import json
import re
import urllib.parse
from dataclasses import dataclass
from pathlib import Path

REPOSITORY = "https://github.com/robyroro/project-ghost"
BUILD_TYPE = f"{REPOSITORY}/blob/main/docs/build/release.md#provenance"
BUILDER_ID = f"{REPOSITORY}/blob/main/docs/build/release.md#the-release-machine"
CHROMIUM = "https://chromium.googlesource.com/chromium/src"
DEPOT_TOOLS = "https://chromium.googlesource.com/chromium/tools/depot_tools"
SUMS_FILE = "SHA256SUMS"
PROVENANCE_FILE = "provenance.intoto.json"
_SUM_LINE = re.compile(r"^([0-9a-f]{64})  ([^/\\]+)$")


@dataclass(frozen=True)
class BuildFacts:
    tag: str
    webops_commit: str
    chromium_commit: str
    series_digest: str
    depot_tools_commit: str
    toolchain: dict[str, str]  # name -> version
    args_gn: str
    identity: str
    tests: dict[str, str]  # suite -> the output directory it ran from
    started: datetime.datetime
    finished: datetime.datetime


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sums_text(sums: dict[str, str]) -> str:
    return "".join(f"{digest}  {name}\n" for name, digest in sorted(sums.items()))


def parse_sums(text: str) -> dict[str, str]:
    sums = {}
    for line in text.splitlines():
        m = _SUM_LINE.match(line)
        if not m:
            raise ValueError(f"not a {SUMS_FILE} line: {line!r}")
        sums[m.group(2)] = m.group(1)
    return sums


def _utc(moment: datetime.datetime) -> str:
    return moment.astimezone(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def statement(subjects: dict[str, str], facts: BuildFacts) -> dict:
    tools = [{"name": name, "uri": f"pkg:generic/{name}@{urllib.parse.quote(version, safe='')}"}
             for name, version in sorted(facts.toolchain.items())]
    return {
        "_type": "https://in-toto.io/Statement/v1",
        "subject": [{"name": name, "digest": {"sha256": digest}}
                    for name, digest in sorted(subjects.items())],
        "predicateType": "https://slsa.dev/provenance/v1",
        "predicate": {
            "buildDefinition": {
                "buildType": BUILD_TYPE,
                "externalParameters": {"repository": REPOSITORY, "tag": facts.tag},
                "internalParameters": {"args.gn": facts.args_gn, "identity": facts.identity,
                                       "tests": facts.tests},
                "resolvedDependencies": [
                    {"uri": f"git+{REPOSITORY}@refs/tags/{facts.tag}",
                     "digest": {"gitCommit": facts.webops_commit}},
                    {"uri": f"git+{CHROMIUM}", "digest": {"gitCommit": facts.chromium_commit}},
                    {"uri": f"git+{REPOSITORY}@refs/tags/{facts.tag}#patches",
                     "digest": {"sha256": facts.series_digest}},
                    {"uri": f"git+{DEPOT_TOOLS}",
                     "digest": {"gitCommit": facts.depot_tools_commit}},
                    *tools,
                ],
            },
            "runDetails": {
                "builder": {"id": BUILDER_ID},
                "metadata": {"invocationId": facts.tag, "startedOn": _utc(facts.started),
                             "finishedOn": _utc(facts.finished)},
            },
        },
    }


def write_release_files(directory: Path, names: list[str], facts: BuildFacts) -> None:
    """Writes the provenance for `names` (files in directory), then SHA256SUMS."""
    subjects = {name: sha256_file(directory / name) for name in names}
    (directory / PROVENANCE_FILE).write_text(
        json.dumps(statement(subjects, facts), indent=2) + "\n", encoding="utf-8", newline="\n")
    sums = dict(subjects, **{PROVENANCE_FILE: sha256_file(directory / PROVENANCE_FILE)})
    (directory / SUMS_FILE).write_text(sums_text(sums), encoding="utf-8", newline="\n")


def check_files(directory: Path) -> list[str]:
    """Every file in SHA256SUMS is present with its hash, and the provenance agrees."""
    if not (directory / SUMS_FILE).exists():
        return [f"no {SUMS_FILE}"]
    sums = parse_sums((directory / SUMS_FILE).read_text(encoding="utf-8"))
    failures = []
    for name, digest in sorted(sums.items()):
        path = directory / name
        if not path.is_file():
            failures.append(f"{name}: missing")
        elif sha256_file(path) != digest:
            failures.append(f"{name}: its SHA-256 differs from {SUMS_FILE}")
    if PROVENANCE_FILE not in sums or not (directory / PROVENANCE_FILE).is_file():
        return failures + [f"{PROVENANCE_FILE}: not listed or missing"]
    doc = json.loads((directory / PROVENANCE_FILE).read_text(encoding="utf-8"))
    for subject in doc.get("subject", []):
        if sums.get(subject["name"]) != subject["digest"]["sha256"]:
            failures.append(f"{subject['name']}: the provenance names another SHA-256")
    return failures
