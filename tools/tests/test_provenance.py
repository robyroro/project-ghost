# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import datetime
import hashlib
import json
import tempfile
import unittest
from pathlib import Path

import provenance

UTC = datetime.timezone.utc
FACTS = provenance.BuildFacts(
    tag="152.0.7977.149-1", webops_commit="a" * 40, chromium_commit="b" * 40,
    series_digest="c" * 64, depot_tools_commit="d" * 40,
    toolchain={"clang": "llvmorg-23-init-19482-g53d18800-1", "windows-sdk": "10.0.26100.0"},
    args_gn='import("//ghost/build/args/release.gn")\nghost_signing_identity = "test"\n',
    identity="test", tests={"ghost_unittests": "release", "ghost_browsertests": "release"},
    started=datetime.datetime(2026, 10, 5, 9, 0, tzinfo=UTC),
    finished=datetime.datetime(2026, 10, 6, 7, 30, tzinfo=UTC))


class SumsTest(unittest.TestCase):
    def test_round_trip_sorted_by_name(self):
        text = provenance.sums_text({"b.exe": "1" * 64, "a.crx3": "2" * 64})
        self.assertEqual(text, f"{'2' * 64}  a.crx3\n{'1' * 64}  b.exe\n")
        self.assertEqual(provenance.parse_sums(text), {"a.crx3": "2" * 64, "b.exe": "1" * 64})

    def test_a_malformed_line_is_refused(self):
        with self.assertRaises(ValueError):
            provenance.parse_sums("not a sum\n")


class StatementTest(unittest.TestCase):
    def setUp(self):
        self.doc = provenance.statement({"update.crx3": "e" * 64, "a.exe": "f" * 64}, FACTS)

    def test_is_an_in_toto_statement_with_an_slsa_predicate(self):
        self.assertEqual(self.doc["_type"], "https://in-toto.io/Statement/v1")
        self.assertEqual(self.doc["predicateType"], "https://slsa.dev/provenance/v1")
        self.assertEqual(self.doc["subject"], [
            {"name": "a.exe", "digest": {"sha256": "f" * 64}},
            {"name": "update.crx3", "digest": {"sha256": "e" * 64}}])

    def test_records_how_the_release_was_made(self):
        definition = self.doc["predicate"]["buildDefinition"]
        self.assertEqual(definition["buildType"], provenance.BUILD_TYPE)
        self.assertEqual(definition["externalParameters"],
                         {"repository": provenance.REPOSITORY, "tag": "152.0.7977.149-1"})
        self.assertEqual(definition["internalParameters"]["args.gn"], FACTS.args_gn)
        self.assertEqual(definition["internalParameters"]["identity"], "test")
        self.assertEqual(definition["internalParameters"]["tests"], FACTS.tests)
        digests = [d.get("digest") for d in definition["resolvedDependencies"]]
        self.assertIn({"gitCommit": "a" * 40}, digests)
        self.assertIn({"gitCommit": "b" * 40}, digests)
        self.assertIn({"sha256": "c" * 64}, digests)
        self.assertIn({"gitCommit": "d" * 40}, digests)
        uris = [d["uri"] for d in definition["resolvedDependencies"]]
        self.assertIn("pkg:generic/clang@llvmorg-23-init-19482-g53d18800-1", uris)
        run = self.doc["predicate"]["runDetails"]
        self.assertEqual(run["builder"], {"id": provenance.BUILDER_ID})
        self.assertEqual(run["metadata"], {"invocationId": "152.0.7977.149-1",
                                           "startedOn": "2026-10-05T09:00:00Z",
                                           "finishedOn": "2026-10-06T07:30:00Z"})


class CheckFilesTest(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.dir = Path(tmp.name)
        for name, data in (("ShadeSetup.exe", b"setup"), ("update.crx3", b"crx")):
            (self.dir / name).write_bytes(data)
        provenance.write_release_files(self.dir, ["ShadeSetup.exe", "update.crx3"],
                                       FACTS)

    def test_a_complete_release_passes(self):
        self.assertEqual(provenance.check_files(self.dir), [])
        sums = provenance.parse_sums((self.dir / provenance.SUMS_FILE).read_text())
        self.assertEqual(set(sums), {"ShadeSetup.exe", "update.crx3",
                                     provenance.PROVENANCE_FILE})
        self.assertEqual(sums["update.crx3"], hashlib.sha256(b"crx").hexdigest())

    def test_a_changed_file_fails(self):
        (self.dir / "update.crx3").write_bytes(b"other")
        self.assertEqual(provenance.check_files(self.dir),
                         [f"update.crx3: its SHA-256 differs from {provenance.SUMS_FILE}"])

    def test_a_missing_file_fails(self):
        (self.dir / "update.crx3").unlink()
        self.assertIn("update.crx3: missing", provenance.check_files(self.dir))

    def test_a_wrong_hash_in_the_provenance_fails(self):
        path = self.dir / provenance.PROVENANCE_FILE
        doc = json.loads(path.read_text(encoding="utf-8"))
        doc["subject"][0]["digest"]["sha256"] = "0" * 64
        path.write_text(json.dumps(doc), encoding="utf-8")
        failures = provenance.check_files(self.dir)
        self.assertIn(f"{doc['subject'][0]['name']}: the provenance names another SHA-256",
                      failures)
        self.assertIn(f"{provenance.PROVENANCE_FILE}: its SHA-256 differs from "
                      f"{provenance.SUMS_FILE}", failures)

    def test_no_sums_file(self):
        (self.dir / provenance.SUMS_FILE).unlink()
        self.assertEqual(provenance.check_files(self.dir), [f"no {provenance.SUMS_FILE}"])
