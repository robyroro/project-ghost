#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Vendors the Rust crates //ghost needs and Chromium lacks, with generated BUILD.gn files.

  pin --src SRC      move every crate in third_party/rust/Cargo.lock that
                     Chromium also has to Chromium's version
  vendor --src SRC   copy each crate Chromium lacks into
                     third_party/rust/<crate>/v<epoch>/crate/, and write its
                     BUILD.gn and README.chromium

Chromium's own generator, gnrt, works only on Chromium's crate set
(third_party/rust/chromium_crates_io). Crates both need are always Chromium's:
never a second copy in the binary. When one of them lacks a feature the graph
needs, `vendor` stops and names it; patch 0028 adds such features.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tomllib
from dataclasses import dataclass, field
from pathlib import Path

import repo

GHOST_RUST = repo.REPO_ROOT / "third_party" / "rust"
MANIFEST = GHOST_RUST / "Cargo.toml"
CONFIG = GHOST_RUST / "vendor_config.toml"
MARKER = ".ghost-vendor.json"
# Not compiled: GN lists every .rs file a crate may include, minus these.
_SKIPPED_DIRS = {"tests", "benches", "examples", "target"}


class VendorError(Exception):
    pass


@dataclass(frozen=True)
class Crate:
    name: str
    version: str
    license: str
    features: list[str]
    deps: list[tuple[str, str | None]]  # (crate name, None for normal or "build")
    proc_macro: bool
    build_script: bool
    edition: str
    lib_root: str  # relative to the crate's directory
    package_dir: str  # where cargo unpacked it
    authors: list[str] = field(default_factory=list)
    description: str = ""
    repository: str = ""


def epoch(version: str) -> tuple[str, str]:
    """Cargo's compatibility epoch, as Chromium writes it and names its directories."""
    major, minor, patch = version.split("+")[0].split("-")[0].split(".")[:3]
    if major != "0":
        value = major
    elif minor != "0":
        value = f"0.{minor}"
    else:
        value = f"0.0.{patch}"
    return value, "v" + value.replace(".", "_")


def directory(name: str) -> str:
    return name.replace("-", "_")


def resolve(metadata: dict) -> dict[str, Crate]:
    """The crates reachable from the root through normal and build dependencies."""
    packages = {p["id"]: p for p in metadata["packages"]}
    nodes = {n["id"]: n for n in metadata["resolve"]["nodes"]}
    crates: dict[str, Crate] = {}
    pending = [metadata["resolve"]["root"]]
    seen = set(pending)
    while pending:
        node = nodes[pending.pop()]
        deps = []
        for dep in node["deps"]:
            kinds = {k["kind"] for k in dep["dep_kinds"]}
            kind = None if None in kinds else "build" if "build" in kinds else "dev"
            if kind == "dev":
                continue
            deps.append((packages[dep["pkg"]]["name"], kind))
            if dep["pkg"] not in seen:
                seen.add(dep["pkg"])
                pending.append(dep["pkg"])
        if node["id"] == metadata["resolve"]["root"]:
            continue
        p = packages[node["id"]]
        if p["name"] in crates:
            raise VendorError(f"two versions of {p['name']} in the graph: "
                              f"{crates[p['name']].version} and {p['version']}")
        package_dir = Path(p["manifest_path"]).parent
        lib = next(t for t in p["targets"]
                   if set(t["kind"]) & {"lib", "rlib", "proc-macro"})
        crates[p["name"]] = Crate(
            name=p["name"], version=p["version"], license=p.get("license") or "",
            features=sorted(node["features"]), deps=sorted(deps),
            proc_macro="proc-macro" in lib["kind"],
            build_script=any(t["kind"] == ["custom-build"] for t in p["targets"]),
            edition=lib.get("edition") or p.get("edition") or "2021",
            lib_root=Path(lib["src_path"]).relative_to(package_dir).as_posix(),
            package_dir=package_dir.as_posix(), authors=p.get("authors") or [],
            description=" ".join((p.get("description") or "").split()),
            repository=p.get("repository") or "")
    return crates


def prune(crates: dict[str, Crate], config: dict, roots: list[str]) -> dict[str, Crate]:
    """Applies `build_script = false` from vendor_config.toml (a build script
    that would misbehave under Chromium's toolchain), then keeps only what the
    roots still reach: the script's build-dependencies go with it."""
    changed = {}
    for name, crate in crates.items():
        if config.get(name, {}).get("build_script") is False:
            changed[name] = Crate(**{**crate.__dict__, "build_script": False,
                                     "deps": [d for d in crate.deps if d[1] != "build"]})
        else:
            changed[name] = crate
    kept, pending = set(), list(roots)
    while pending:
        name = pending.pop()
        if name not in kept:
            kept.add(name)
            pending += [d for d, _ in changed[name].deps]
    return {n: c for n, c in changed.items() if n in kept}


def _roots(metadata: dict) -> list[str]:
    packages = {p["id"]: p for p in metadata["packages"]}
    root = next(n for n in metadata["resolve"]["nodes"]
                if n["id"] == metadata["resolve"]["root"])
    return [packages[d["pkg"]]["name"] for d in root["deps"]]


def _chromium_build_gn(tree: Path, crate: Crate) -> Path:
    return tree / directory(crate.name) / epoch(crate.version)[1] / "BUILD.gn"


def chromium_version(tree: Path, crate: Crate) -> str | None:
    """Chromium's version of the crate, if Chromium ships it. A crate Chromium
    marks testonly (regex, for one) isn't shipped: the browser can't depend on
    it, so we vendor our own, which is then the binary's only copy."""
    path = _chromium_build_gn(tree, crate)
    if not path.exists():
        return None
    text = path.read_text(encoding="utf-8")
    if re.search(r"^  testonly = true", text, re.MULTILINE):
        return None
    m = re.search(r'cargo_pkg_version = "([^"]+)"', text)
    return m.group(1) if m else None


def chromium_features(tree: Path, crate: Crate) -> set[str]:
    text = _chromium_build_gn(tree, crate).read_text(encoding="utf-8")
    m = re.search(r"^  features = \[(.*?)\]", text, re.MULTILINE | re.DOTALL)
    return set(re.findall(r'"([^"]+)"', m.group(1))) if m else set()


def missing_features(tree: Path, crates: dict[str, Crate]) -> dict[str, list[str]]:
    """Features the graph needs from Chromium's crates that Chromium doesn't build."""
    missing = {}
    for crate in crates.values():
        if chromium_version(tree, crate):
            lacking = sorted(set(crate.features) - chromium_features(tree, crate))
            if lacking:
                missing[crate.name] = lacking
    return missing


def pins(tree: Path, crates: dict[str, Crate]) -> list[tuple[str, str, str]]:
    """(crate, locked version, Chromium's version) where they differ."""
    found = []
    for crate in sorted(crates.values(), key=lambda c: c.name):
        theirs = chromium_version(tree, crate)
        if theirs and theirs != crate.version:
            found.append((crate.name, crate.version, theirs))
    return found


def _reachable(metadata: dict) -> list[tuple[str, str]]:
    """(name, version) of every crate reachable through normal and build
    dependencies, duplicates included."""
    packages = {p["id"]: p for p in metadata["packages"]}
    nodes = {n["id"]: n for n in metadata["resolve"]["nodes"]}
    root = metadata["resolve"]["root"]
    seen, pending = {root}, [root]
    while pending:
        for dep in nodes[pending.pop()]["deps"]:
            kinds = {k["kind"] for k in dep["dep_kinds"]}
            if kinds & {None, "build"} and dep["pkg"] not in seen:
                seen.add(dep["pkg"])
                pending.append(dep["pkg"])
    seen.discard(root)
    return sorted((packages[i]["name"], packages[i]["version"]) for i in seen)


def pin_targets(tree: Path, metadata: dict) -> list[tuple[str, str, str]]:
    """(crate, locked version, Chromium's version) for every reachable crate
    Chromium has, at the same epoch or, when Chromium has only one, at that
    one: a crate cargo resolved to another epoch (syn 3 where Chromium has
    syn 2) is pinned back, and its dependents follow."""
    found = []
    for name, version in _reachable(metadata):
        versions = {}
        for build_gn in sorted((tree / directory(name)).glob("v*/BUILD.gn")):
            m = re.search(r'cargo_pkg_version = "([^"]+)"', build_gn.read_text(encoding="utf-8"))
            if m:
                versions[build_gn.parent.name] = m.group(1)
        target = versions.get(epoch(version)[1]) or (
            next(iter(versions.values())) if len(versions) == 1 else None)
        if target and target != version:
            found.append((name, version, target))
    return found


def same_epoch(left: list[tuple[str, str, str]]) -> list[tuple[str, str, str]]:
    """The pins that must land: a crate at Chromium's epoch but another version
    would be a second copy of the same crate. Another epoch (thiserror 1
    where Chromium has 2) is a different crate, vendored."""
    return [p for p in left if epoch(p[1]) == epoch(p[2])]


def label(crate: Crate, tree: Path) -> str:
    root = "//third_party/rust" if chromium_version(tree, crate) else "//ghost/third_party/rust"
    return f"{root}/{directory(crate.name)}/{epoch(crate.version)[1]}:lib"


def _gn_list(name: str, items: list[str], indent: str = "  ") -> str:
    """A GN list assignment, or nothing for an empty list: Chromium's templates
    treat a missing list as empty, and some pass a present one on as flags."""
    if not items:
        return ""
    if len(items) == 1:
        return f'{indent}{name} = [ "{items[0]}" ]\n'
    body = "".join(f'{indent}  "{i}",\n' for i in items)
    return f"{indent}{name} = [\n{body}{indent}]\n"


def _gn_string(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"').replace("$", "\\$")


def render_build_gn(crate: Crate, crates: dict[str, Crate], tree: Path, sources: list[str],
                    build_sources: list[str] | None = None,
                    build_script_outputs: list[str] = (), rustflags: list[str] = ()) -> str:
    deps = sorted(label(crates[n], tree) for n, k in crate.deps if k is None)
    build_deps = sorted(label(crates[n], tree) for n, k in crate.deps if k == "build")
    value, _ = epoch(crate.version)
    out = ["# This Source Code Form is subject to the terms of the Mozilla Public\n",
           "# License, v. 2.0. If a copy of the MPL was not distributed with this\n",
           "# file, You can obtain one at https://mozilla.org/MPL/2.0/.\n\n",
           "# Generated by tools/rust_vendor.py from third_party/rust/Cargo.lock.\n",
           "# Don't edit: change the lock or vendor_config.toml and run it again.\n\n",
           'import("//build/rust/cargo_crate.gni")\n\n',
           'cargo_crate("lib") {\n',
           f'  crate_name = "{directory(crate.name)}"\n',
           f'  epoch = "{value}"\n',
           f'  crate_type = "{"proc-macro" if crate.proc_macro else "rlib"}"\n',
           f'  crate_root = "crate/{crate.lib_root}"\n',
           _gn_list("sources", [f"crate/{s}" for s in sources]),
           "  inputs = []\n",
           "  build_native_rust_unit_tests = false\n",
           f'  edition = "{crate.edition}"\n',
           f'  cargo_pkg_authors = "{_gn_string(", ".join(crate.authors))}"\n',
           f'  cargo_pkg_name = "{crate.name}"\n',
           f'  cargo_pkg_description = "{_gn_string(crate.description)}"\n',
           f'  cargo_pkg_repository = "{_gn_string(crate.repository)}"\n',
           f'  cargo_pkg_version = "{crate.version}"\n',
           "  allow_unsafe = true\n",
           _gn_list("features", crate.features),
           _gn_list("deps", deps)]
    if crate.build_script:
        out += ['  build_root = "crate/build.rs"\n',
                _gn_list("build_sources", [f"crate/{s}" for s in
                                           (build_sources or ["build.rs"])]),
                _gn_list("build_deps", build_deps),
                _gn_list("build_script_outputs", list(build_script_outputs))]
    if rustflags:
        out.append(_gn_list("rustflags", list(rustflags)))
    out += ['  visibility = [ "//ghost/*" ]\n',
            "\n  # Third-party code: no chromium_code warnings, no coverage.\n",
            "  library_configs -= [\n",
            '    "//build/config/compiler:chromium_code",\n',
            '    "//build/config/coverage:default_coverage",\n',
            "  ]\n",
            "\n  # Fuzzers see inside the crate (//ghost/build/config/BUILD.gn).\n",
            '  library_configs += [ "//ghost/build/config:rust_fuzz_coverage" ]\n',
            "  proc_macro_configs -= [\n",
            '    "//build/config/compiler:chromium_code",\n',
            '    "//build/config/coverage:default_coverage",\n',
            "  ]\n",
            "}\n"]
    return "".join(out)


def rust_sources(crate_dir: Path) -> list[str]:
    found = []
    for path in sorted(crate_dir.rglob("*.rs")):
        rel = path.relative_to(crate_dir)
        if rel.parts[0] in _SKIPPED_DIRS or rel.as_posix() == "build.rs":
            continue
        found.append(rel.as_posix())
    return found


def readme(crate: Crate, crate_dir: Path) -> str:
    licenses = sorted(p.name for p in crate_dir.iterdir()
                      if p.is_file() and re.match(r"(LICEN[CS]E|COPYING|UNLICENSE)", p.name,
                                                  re.IGNORECASE))
    return (f"Name: {crate.name}\n"
            f"URL: https://crates.io/crates/{crate.name}\n"
            f"Version: {crate.version}\n"
            f"Revision: {crate.version}\n"
            f"License: {crate.license}\n"
            f"License File: {', '.join('crate/' + n for n in licenses)}\n"
            "Security Critical: yes\n"
            "Shipped: yes\n\n"
            f"Description:\n{crate.description}\n\n"
            "Local Modifications:\nNone. Vendored by tools/rust_vendor.py.\n")


# --- Running cargo ---------------------------------------------------------------

def cargo(src: Path) -> Path:
    return src / "third_party" / "rust-toolchain" / "bin" / (
        "cargo.exe" if os.name == "nt" else "cargo")


def metadata(src: Path) -> dict:
    out = subprocess.run([str(cargo(src)), "metadata", "--format-version", "1",
                          "--manifest-path", str(MANIFEST)],
                         capture_output=True, check=True).stdout
    return json.loads(out.decode("utf-8"))


def lock_checksums() -> dict[tuple[str, str], str]:
    lock = tomllib.loads((GHOST_RUST / "Cargo.lock").read_text(encoding="utf-8"))
    return {(p["name"], p["version"]): p.get("checksum", "") for p in lock.get("package", [])}


def pin(src: Path) -> int:
    """Pins until nothing moves. A pin can fail until another lands first
    (a dependent still requiring the newer epoch), so failures are retried
    in the next round; whatever is left at the end is reported."""
    tree = src / "third_party" / "rust"
    lock = GHOST_RUST / "Cargo.lock"
    for _ in range(20):
        found = pin_targets(tree, metadata(src))
        if not found:
            break
        before = lock.read_bytes()
        for name, ours, theirs in found:
            done = subprocess.run([str(cargo(src)), "update", "--manifest-path", str(MANIFEST),
                                   "-p", f"{name}@{ours}", "--precise", theirs],
                                  capture_output=True, text=True)
            print(f"{'pinned' if done.returncode == 0 else 'retry '} {name} {ours} -> {theirs}")
        if lock.read_bytes() == before:
            break
    left = pin_targets(tree, metadata(src))
    for name, ours, theirs in left:
        if (name, ours, theirs) not in same_epoch(left):
            print(f"{name} {ours} stays: Chromium has {theirs}, another epoch; it's vendored")
    if same_epoch(left):
        raise VendorError("these crates can't reach Chromium's versions: " + ", ".join(
            f"{n} {o} (Chromium {t})" for n, o, t in same_epoch(left)))
    print("every crate Chromium has at the same epoch is at Chromium's version")
    return 0


def vendor(src: Path) -> int:
    tree = src / "third_party" / "rust"
    config = tomllib.loads(CONFIG.read_text(encoding="utf-8")) if CONFIG.exists() else {}
    meta = metadata(src)
    crates = prune(resolve(meta), config, _roots(meta))
    missing = missing_features(tree, crates)
    if missing:
        raise VendorError("Chromium's crates lack features the graph needs (patch 0028):"
                          + "; ".join(f"{n}: {', '.join(f)}" for n, f in missing.items()))
    checksums = lock_checksums()
    ours = [c for c in crates.values() if not chromium_version(tree, c)]
    wanted = {(directory(c.name), epoch(c.version)[1]) for c in ours}
    for marker in GHOST_RUST.glob(f"*/*/{MARKER}"):
        if (marker.parent.parent.name, marker.parent.name) not in wanted:
            shutil.rmtree(marker.parent)
            if not any(marker.parent.parent.iterdir()):
                marker.parent.parent.rmdir()
            print(f"removed {marker.parent.relative_to(GHOST_RUST)}")
    for crate in sorted(ours, key=lambda c: c.name):
        dest = GHOST_RUST / directory(crate.name) / epoch(crate.version)[1]
        marker = dest / MARKER
        stamp = {"name": crate.name, "version": crate.version,
                 "checksum": checksums.get((crate.name, crate.version), "")}
        if not (marker.exists() and json.loads(marker.read_text(encoding="utf-8")) == stamp):
            if dest.exists():
                shutil.rmtree(dest)
            shutil.copytree(crate.package_dir, dest / "crate",
                            ignore=shutil.ignore_patterns(".cargo-ok", ".cargo_vcs_info.json"))
            marker.write_text(json.dumps(stamp, indent=1) + "\n", encoding="utf-8", newline="\n")
            print(f"vendored {crate.name} {crate.version}")
        settings = config.get(crate.name, {})
        (dest / "BUILD.gn").write_text(render_build_gn(
            crate, crates, tree, rust_sources(dest / "crate"),
            build_sources=settings.get("build_sources"),
            build_script_outputs=settings.get("build_script_outputs", []),
            rustflags=settings.get("rustflags", [])), encoding="utf-8", newline="\n")
        (dest / "README.chromium").write_text(readme(crate, dest / "crate"), encoding="utf-8",
                                              newline="\n")
    print(f"{len(ours)} crate(s) vendored; {len(crates) - len(ours)} are Chromium's")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("pin", "vendor"):
        p = sub.add_parser(name)
        p.add_argument("--src", type=Path, required=True, help="the Chromium checkout (src)")
    args = parser.parse_args(argv)
    try:
        return pin(args.src) if args.command == "pin" else vendor(args.src)
    except (VendorError, subprocess.CalledProcessError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
