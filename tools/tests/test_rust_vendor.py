# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import tempfile
import unittest
from pathlib import Path

import rust_vendor


def package(name, version, *, deps=(), kinds=("lib",), build=False, license="MIT",
            edition="2021"):
    root = f"/registry/{name}-{version}"
    targets = [{"name": name.replace("-", "_"), "kind": list(kinds),
                "src_path": f"{root}/src/lib.rs", "edition": edition}]
    if build:
        targets.append({"name": "build-script-build", "kind": ["custom-build"],
                        "src_path": f"{root}/build.rs", "edition": edition})
    return {"name": name, "version": version, "id": f"{name} {version}", "license": license,
            "license_file": None, "authors": ["A <a@example.invalid>"],
            "description": f"The {name} crate", "repository": f"https://example.invalid/{name}",
            "manifest_path": f"{root}/Cargo.toml", "edition": edition, "targets": targets,
            "dependencies": [{"name": d, "kind": None} for d in deps]}


def node(pkg_id, features=(), deps=()):
    """deps: (package id, kind) with kind None (normal) or "build"."""
    return {"id": pkg_id, "features": list(features),
            "deps": [{"pkg": d, "name": d.split()[0].replace("-", "_"),
                      "dep_kinds": [{"kind": k, "target": None}]} for d, k in deps]}


# root -> adblock -> {regex (Chromium's), seahash (ours), thiserror (ours, build script)}
# thiserror -> thiserror-impl (proc macro, ours); a dev-dependency is ignored.
METADATA = {
    "packages": [
        package("ghost_rust_deps", "0.0.0", deps=["adblock"]),
        package("adblock", "0.13.3", license="MPL-2.0"),
        package("regex", "1.12.4", license="MIT OR Apache-2.0"),
        package("seahash", "4.1.0"),
        package("thiserror", "1.0.69", build=True),
        package("thiserror-impl", "1.0.69", kinds=("proc-macro",)),
        package("criterion", "0.5.1"),
    ],
    "resolve": {"root": "ghost_rust_deps 0.0.0", "nodes": [
        node("ghost_rust_deps 0.0.0", deps=[("adblock 0.13.3", None)]),
        node("adblock 0.13.3", ["full-regex-handling"],
             [("regex 1.12.4", None), ("seahash 4.1.0", None), ("thiserror 1.0.69", None),
              ("criterion 0.5.1", "dev")]),
        node("regex 1.12.4", ["std", "unicode"]),
        node("seahash 4.1.0", ["default"]),
        node("thiserror 1.0.69", [], [("thiserror-impl 1.0.69", None)]),
        node("thiserror-impl 1.0.69"),
        node("criterion 0.5.1"),
    ]},
}


def chromium_tree(root: Path, crates: dict) -> Path:
    """A fake //third_party/rust: {(dir, epoch): (version, features)}."""
    for (directory, epoch), (version, features) in crates.items():
        path = root / directory / epoch
        path.mkdir(parents=True)
        listed = "".join(f'    "{f}",\n' for f in features)
        (path / "BUILD.gn").write_text(
            f'cargo_crate("lib") {{\n  cargo_pkg_version = "{version}"\n'
            f"  features = [\n{listed}  ]\n}}\n", encoding="utf-8")
    return root


class EpochTest(unittest.TestCase):
    def test_epochs(self):
        self.assertEqual(rust_vendor.epoch("1.2.3"), ("1", "v1"))
        self.assertEqual(rust_vendor.epoch("0.22.1"), ("0.22", "v0_22"))
        self.assertEqual(rust_vendor.epoch("0.0.4"), ("0.0.4", "v0_0_4"))

    def test_directory_names_use_underscores(self):
        self.assertEqual(rust_vendor.directory("thiserror-impl"), "thiserror_impl")


class ResolveTest(unittest.TestCase):
    def test_reachable_crates_without_dev_dependencies(self):
        crates = rust_vendor.resolve(METADATA)
        self.assertEqual(sorted(crates), ["adblock", "regex", "seahash", "thiserror",
                                          "thiserror-impl"])
        self.assertEqual(crates["adblock"].features, ["full-regex-handling"])
        self.assertEqual([d for d, _ in crates["adblock"].deps], ["regex", "seahash", "thiserror"])
        self.assertTrue(crates["thiserror"].build_script)
        self.assertTrue(crates["thiserror-impl"].proc_macro)
        self.assertEqual(crates["adblock"].license, "MPL-2.0")


class ChromiumTest(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.tree = chromium_tree(Path(tmp.name), {("regex", "v1"): ("1.12.4", ["std"])})
        self.crates = rust_vendor.resolve(METADATA)

    def test_finds_chromiums_crates(self):
        self.assertEqual(rust_vendor.chromium_version(self.tree, self.crates["regex"]), "1.12.4")
        self.assertIsNone(rust_vendor.chromium_version(self.tree, self.crates["seahash"]))

    def test_a_missing_feature_is_reported(self):
        self.assertEqual(rust_vendor.missing_features(self.tree, self.crates),
                         {"regex": ["unicode"]})

    def test_a_version_other_than_chromiums_is_a_pin(self):
        crates = dict(self.crates)
        crates["regex"] = crates["regex"].__class__(**{**crates["regex"].__dict__,
                                                       "version": "1.13.1"})
        self.assertEqual(rust_vendor.pins(self.tree, crates), [("regex", "1.13.1", "1.12.4")])


class PinTargetsTest(unittest.TestCase):
    def test_duplicates_and_other_epochs_are_pinned_to_chromiums(self):
        meta = {"packages": [package("ghost_rust_deps", "0.0.0"), package("syn", "2.0.119"),
                             package("syn", "3.0.7"), package("synstructure", "0.14.0"),
                             package("seahash", "4.1.0")],
                "resolve": {"root": "ghost_rust_deps 0.0.0", "nodes": [
                    node("ghost_rust_deps 0.0.0", deps=[("syn 2.0.119", None),
                                                        ("synstructure 0.14.0", None),
                                                        ("seahash 4.1.0", None)]),
                    node("synstructure 0.14.0", deps=[("syn 3.0.7", None)]),
                    node("syn 2.0.119"), node("syn 3.0.7"), node("seahash 4.1.0")]}}
        with tempfile.TemporaryDirectory() as d:
            tree = chromium_tree(Path(d), {("syn", "v2"): ("2.0.117", []),
                                           ("synstructure", "v0_13"): ("0.13.2", [])})
            self.assertEqual(rust_vendor.pin_targets(tree, meta), [
                ("syn", "2.0.119", "2.0.117"), ("syn", "3.0.7", "2.0.117"),
                ("synstructure", "0.14.0", "0.13.2")])


class LeftoverPinsTest(unittest.TestCase):
    def test_only_a_same_epoch_difference_is_an_error(self):
        # thiserror 1 is what the graph needs; Chromium's 2 can't replace it,
        # so it's vendored. regex 1.13 against Chromium's 1.12 would be a
        # second copy of the same crate.
        self.assertEqual(rust_vendor.same_epoch([("thiserror", "1.0.69", "2.0.18"),
                                                 ("regex", "1.13.1", "1.12.4")]),
                         [("regex", "1.13.1", "1.12.4")])


class BuildGnTest(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.tree = chromium_tree(Path(tmp.name), {("regex", "v1"): ("1.12.4", ["std",
                                                                               "unicode"])})
        self.crates = rust_vendor.resolve(METADATA)

    def render(self, name, **kwargs):
        return rust_vendor.render_build_gn(self.crates[name], self.crates, self.tree,
                                           ["src/lib.rs"], **kwargs)

    def test_a_library_depends_on_chromiums_and_our_crates(self):
        gn = self.render("adblock")
        self.assertIn('crate_name = "adblock"', gn)
        self.assertIn('epoch = "0.13"', gn)
        self.assertIn('crate_type = "rlib"', gn)
        self.assertIn('crate_root = "crate/src/lib.rs"', gn)
        self.assertIn('"//third_party/rust/regex/v1:lib"', gn)
        self.assertIn('"//ghost/third_party/rust/seahash/v4:lib"', gn)
        self.assertIn('"//ghost/third_party/rust/thiserror/v1:lib"', gn)
        self.assertIn('features = [ "full-regex-handling" ]', gn)
        self.assertIn('visibility = [ "//ghost/*" ]', gn)
        self.assertIn('cargo_pkg_version = "0.13.3"', gn)

    def test_a_proc_macro(self):
        self.assertIn('crate_type = "proc-macro"', self.render("thiserror-impl"))

    def test_a_build_script(self):
        gn = self.render("thiserror", build_script_outputs=["private.rs"])
        self.assertIn('build_root = "crate/build.rs"', gn)
        self.assertIn('build_script_outputs = [ "private.rs" ]', gn)


if __name__ == "__main__":
    unittest.main()
