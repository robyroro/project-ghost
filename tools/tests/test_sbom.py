# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import unittest
from pathlib import Path

import sbom


def document(packages: list[tuple[str, str, str]]) -> dict:
    """An SPDX document as licenses.py writes it: (name, license ID, license text)."""
    root = "SPDXRef-Package-Chromium"
    doc = {"spdxVersion": "SPDX-2.2", "SPDXID": "SPDXRef-DOCUMENT", "name": "x",
           "documentNamespace": "https://example.invalid/x",
           "creationInfo": {"creators": ["Tool: spdx_writer.py"]}, "dataLicense": "CC0-1.0",
           "documentDescribes": [root], "packages": [], "hasExtractedLicensingInfos": [],
           "relationships": []}
    for name, license_id, text in [("Chromium", "LicenseRef-Chromium", "BSD")] + packages:
        package_id = f"SPDXRef-Package-{name}"
        doc["packages"].append({"SPDXID": package_id, "name": name,
                                "licenseConcluded": license_id})
        if name != "Chromium":
            doc["relationships"].append({"spdxElementId": root, "relationshipType": "CONTAINS",
                                         "relatedSpdxElement": package_id})
        if license_id not in [l["licenseId"] for l in doc["hasExtractedLicensingInfos"]]:
            doc["hasExtractedLicensingInfos"].append(
                {"name": name, "licenseId": license_id, "extractedText": text,
                 "crossRefs": [{"url": "https://example.invalid"}]})
    return doc


BROWSER = document([("zlib", "LicenseRef-zlib", "zlib text"),
                    ("libpng", "LicenseRef-libpng", "png text")])
UPDATER = document([("zlib", "LicenseRef-zlib", "zlib text"),
                    ("lzma", "LicenseRef-lzma", "lzma text"),
                    ("other", "LicenseRef-libpng", "a different text")])


class MergeTest(unittest.TestCase):
    def setUp(self):
        self.doc = sbom.merge(BROWSER, UPDATER)

    def test_adds_the_updaters_packages_once(self):
        self.assertEqual([p["name"] for p in self.doc["packages"]],
                         ["Chromium", "zlib", "libpng", "lzma", "other"])

    def test_a_license_id_with_another_text_is_renamed(self):
        other = next(p for p in self.doc["packages"] if p["name"] == "other")
        self.assertEqual(other["licenseConcluded"], "LicenseRef-libpng-updater")
        texts = {l["licenseId"]: l["extractedText"]
                 for l in self.doc["hasExtractedLicensingInfos"]}
        self.assertEqual(texts["LicenseRef-libpng"], "png text")
        self.assertEqual(texts["LicenseRef-libpng-updater"], "a different text")
        self.assertEqual(texts["LicenseRef-lzma"], "lzma text")

    def test_every_added_package_is_contained_by_the_root(self):
        contained = {r["relatedSpdxElement"] for r in self.doc["relationships"]}
        self.assertTrue({"SPDXRef-Package-lzma", "SPDXRef-Package-other"} <= contained)

    def test_the_inputs_are_unchanged(self):
        self.assertEqual(len(BROWSER["packages"]), 3)


class GhostTest(unittest.TestCase):
    def test_describes_the_release_with_ghosts_code(self):
        doc = sbom.complete(sbom.merge(BROWSER, UPDATER), version="152.0.7977.14901",
                            tag="152.0.7977.149-1", commit="a" * 40)
        self.assertEqual(doc["name"], "Project Ghost 152.0.7977.14901")
        self.assertEqual(doc["documentNamespace"],
                         "https://github.com/robyroro/project-ghost/releases/152.0.7977.149-1/sbom")
        ghost = next(p for p in doc["packages"] if p["SPDXID"] == sbom.GHOST_ID)
        self.assertEqual(ghost["licenseConcluded"], "MPL-2.0")
        self.assertEqual(ghost["versionInfo"], "152.0.7977.14901")
        self.assertEqual(ghost["downloadLocation"],
                         "git+https://github.com/robyroro/project-ghost@" + "a" * 40)
        self.assertEqual(doc["documentDescribes"], [sbom.GHOST_ID])
        self.assertIn({"spdxElementId": sbom.GHOST_ID, "relationshipType": "CONTAINS",
                       "relatedSpdxElement": "SPDXRef-Package-Chromium"}, doc["relationships"])
        self.assertIn("Tool: ghost/tools/sbom.py", doc["creationInfo"]["creators"])


class CommandTest(unittest.TestCase):
    def test_runs_chromiums_generator_for_one_target(self):
        src, out = Path(r"C:\src"), Path(r"C:\src\out\release")
        self.assertEqual(
            sbom.licenses_command("vpython3.bat", src, out, sbom.BROWSER_TARGET, Path("b.json")),
            ["vpython3.bat", str(src / "tools" / "licenses" / "licenses.py"), "license_file",
             "--format", "spdx", "--gn-out-dir", str(out), "--gn-target",
             "//chrome/installer/mini_installer:mini_installer", "--target-os", "win", "b.json"])
