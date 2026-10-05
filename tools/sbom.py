# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""A release's SBOM, sbom.spdx.json (sub-project E).

Chromium's tools/licenses/licenses.py writes SPDX 2.2 JSON for one GN
target's shipped third-party code. This runs it for the browser's installer
and the updater's, merges the two, and describes the release as Ghost's own
package (MPL-2.0) containing them.

    python tools/sbom.py --src SRC --out out\\release --tag T --commit C --output FILE
"""

from __future__ import annotations

import argparse
import copy
import datetime
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

import release_version
import repo

DOCUMENT = "sbom.spdx.json"
BROWSER_TARGET = "//chrome/installer/mini_installer:mini_installer"
UPDATER_TARGET = "//chrome/updater/win/installer:installer"
GHOST_ID = "SPDXRef-Package-Project-Ghost"
REPOSITORY = "https://github.com/robyroro/project-ghost"


def licenses_command(python: str, src: Path, out_dir: Path, target: str,
                     output: Path) -> list[str]:
    return [python, str(src / "tools" / "licenses" / "licenses.py"), "license_file",
            "--format", "spdx", "--gn-out-dir", str(out_dir), "--gn-target", target,
            "--target-os", "win", str(output)]


def merge(primary: dict, secondary: dict) -> dict:
    """primary, plus secondary's packages it lacks (by name) and their licenses."""
    doc = copy.deepcopy(primary)
    root = doc["documentDescribes"][0]
    names = {p["name"] for p in doc["packages"]}
    ids = {p["SPDXID"] for p in doc["packages"]}
    texts = {l["licenseId"]: l["extractedText"] for l in doc["hasExtractedLicensingInfos"]}
    theirs = {l["licenseId"]: l for l in secondary["hasExtractedLicensingInfos"]}
    renamed: dict[str, str] = {}
    for package in secondary["packages"]:
        if package["name"] in names:
            continue
        license_id = package["licenseConcluded"]
        if license_id not in renamed and license_id in theirs:
            text = theirs[license_id]["extractedText"]
            new_id = license_id if texts.get(license_id, text) == text else f"{license_id}-updater"
            if new_id not in texts:
                doc["hasExtractedLicensingInfos"].append(
                    dict(copy.deepcopy(theirs[license_id]), licenseId=new_id))
                texts[new_id] = text
            renamed[license_id] = new_id
        package_id = package["SPDXID"] if package["SPDXID"] not in ids \
            else f"{package['SPDXID']}-updater"
        doc["packages"].append(dict(package, SPDXID=package_id,
                                    licenseConcluded=renamed.get(license_id, license_id)))
        doc["relationships"].append({"spdxElementId": root, "relationshipType": "CONTAINS",
                                     "relatedSpdxElement": package_id})
        names.add(package["name"])
        ids.add(package_id)
    return doc


def complete(doc: dict, version: str, tag: str, commit: str) -> dict:
    """Describes the release: Ghost's package, MPL-2.0, contains Chromium's root."""
    doc = copy.deepcopy(doc)
    chromium = doc["documentDescribes"][0]
    doc["name"] = f"Project Ghost {version}"
    doc["documentNamespace"] = f"{REPOSITORY}/releases/{tag}/sbom"
    doc["creationInfo"] = {
        "created": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "creators": ["Tool: ghost/tools/sbom.py", *doc["creationInfo"]["creators"]]}
    doc["packages"].insert(0, {
        "SPDXID": GHOST_ID, "name": "Project Ghost", "versionInfo": version,
        "downloadLocation": f"git+{REPOSITORY}@{commit}", "licenseConcluded": "MPL-2.0",
        "comment": "Ghost's code (//ghost) and its Chromium patch series (patches/)."})
    doc["documentDescribes"] = [GHOST_ID]
    doc["relationships"].insert(0, {"spdxElementId": GHOST_ID, "relationshipType": "CONTAINS",
                                    "relatedSpdxElement": chromium})
    return doc


def build(python: str, src: Path, out_dir: Path, tag: str, commit: str, output: Path,
          env: dict[str, str] | None = None) -> None:
    version = release_version.parse_tag(tag, repo.read_chromium_version()).version
    with tempfile.TemporaryDirectory() as tmp:
        docs = []
        for target in (BROWSER_TARGET, UPDATER_TARGET):
            path = Path(tmp) / f"{len(docs)}.json"
            subprocess.run(licenses_command(python, src, out_dir, target, path), cwd=src,
                           env=env, check=True)
            docs.append(json.loads(path.read_text(encoding="utf-8")))
    doc = complete(merge(*docs), version, tag, commit)
    output.write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8", newline="\n")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--src", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True, help="the output directory, in src")
    parser.add_argument("--tag", required=True)
    parser.add_argument("--commit", required=True, help="the tag's commit in this repository")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--python", default="vpython3.bat" if os.name == "nt" else "vpython3")
    args = parser.parse_args(argv)
    try:
        build(args.python, args.src.resolve(), (args.src / args.out).resolve(), args.tag,
              args.commit, args.output)
    except (subprocess.CalledProcessError, release_version.ReleaseVersionError) as e:
        print(f"sbom: {e}", file=sys.stderr)
        return 1
    print(f"wrote {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
