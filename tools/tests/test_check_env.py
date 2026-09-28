# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import dataclasses
import sys
import unittest

import check_env
import repo
from check_env import Facts, PathEntry, Status, VolumeFacts, VsInstall

REQ = repo.load_requirements()
VS_OK = VsInstall("18.0.11111.1", r"C:\Program Files\Microsoft Visual Studio\18\Community",
                  "Microsoft.VisualStudio.Product.Community")


def ready_machine(**overrides) -> Facts:
    """A machine that satisfies every requirement for the pinned tag."""
    win = REQ["windows"]
    facts = Facts(
        platform="win32",
        windows_major=10,
        ram_gb=64.0,
        logical_cpus=32,
        vswhere_found=True,
        vs_installs=[VS_OK],
        vs_satisfying=[VS_OK],
        vs_selected_path=VS_OK.path,
        sdk_versions=[win["windows_sdk"]["min_version"]],
        sdk_include_versions=[win["windows_sdk"]["include_version"]],
        debugger_version=win["debugging_tools"]["min_version"],
        git_version="2.51.0",
        git_config={k: v["expected"] for k, v in win["git_config"].items()},
        long_paths_enabled=True,
        path_entries=[PathEntry(r"D:\ghost\depot_tools", has_gclient=True, has_python=True,
                                has_git=True),
                      PathEntry(r"C:\Program Files\Git\cmd", has_git=True)],
        env=dict(win["env"]),
        volume=VolumeFacts(r"D:\ghost", 600.0, "ReFS"),
    )
    return dataclasses.replace(facts, **overrides)


def by_name(results):
    return {r.name: r for r in results}


class ReadyMachineTest(unittest.TestCase):
    def test_everything_passes(self):
        results = check_env.evaluate(REQ, ready_machine())
        bad = [r for r in results if r.status in (Status.FAIL, Status.WARN)]
        self.assertEqual(bad, [])


class RuleTest(unittest.TestCase):
    def status_of(self, name, **overrides):
        return by_name(check_env.evaluate(REQ, ready_machine(**overrides)))[name].status

    def test_non_windows_host_is_rejected_outright(self):
        results = check_env.evaluate(REQ, Facts(platform="linux"))
        self.assertEqual([(r.name, r.status) for r in results], [("platform", Status.FAIL)])

    def test_ram_thresholds(self):
        self.assertIs(self.status_of("ram", ram_gb=4.0), Status.FAIL)
        self.assertIs(self.status_of("ram", ram_gb=12.0), Status.WARN)
        self.assertIs(self.status_of("ram", ram_gb=32.0), Status.PASS)

    def test_visual_studio_missing(self):
        self.assertIs(self.status_of("visual_studio", vs_installs=[], vs_satisfying=[]),
                      Status.FAIL)

    def test_visual_studio_without_required_components(self):
        result = by_name(check_env.evaluate(REQ, ready_machine(vs_satisfying=[])))["visual_studio"]
        self.assertIs(result.status, Status.FAIL)
        self.assertIn("Microsoft.VisualStudio.Component.VC.ATLMFC", result.remedy)

    def test_visual_studio_invisible_to_chromium_needs_install_variable(self):
        build_tools = VsInstall(
            "17.14.37516.0", r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools",
            "Microsoft.VisualStudio.Product.BuildTools")
        result = by_name(check_env.evaluate(REQ, ready_machine(
            vs_installs=[build_tools], vs_satisfying=[build_tools],
            vs_selected_path=None)))["visual_studio"]
        self.assertIs(result.status, Status.FAIL)
        self.assertEqual(result.remedy,
                         f"Set the user environment variable vs2022_install={build_tools.path}")

    def test_visual_studio_selected_install_must_be_the_complete_one(self):
        result = by_name(check_env.evaluate(REQ, ready_machine(
            vs_selected_path=r"C:\Program Files\Microsoft Visual Studio\18\Preview")))
        self.assertIs(result["visual_studio"].status, Status.FAIL)

    def test_sdk_needs_matching_servicing_release(self):
        self.assertIs(self.status_of("windows_sdk", sdk_versions=["10.0.26100.1742"]), Status.FAIL)
        self.assertIs(self.status_of("windows_sdk", sdk_versions=["10.0.26100.9000"]), Status.PASS)

    def test_newer_sdk_line_does_not_substitute(self):
        # Chromium compiles against Include/10.0.26100.0 at this tag, so a
        # different SDK build number is not an acceptable stand-in.
        self.assertIs(self.status_of("windows_sdk", sdk_versions=["10.0.28000.2270"]), Status.FAIL)

    def test_sdk_headers_must_be_present(self):
        self.assertIs(self.status_of("windows_sdk", sdk_include_versions=["10.0.22621.0"]),
                      Status.FAIL)

    def test_debugger(self):
        self.assertIs(self.status_of("debugging_tools", debugger_version=None), Status.FAIL)
        self.assertIs(self.status_of("debugging_tools", debugger_version="10.0.22621.1"),
                      Status.FAIL)

    def test_git_config_severity_follows_requirements(self):
        cfg = {k: v["expected"] for k, v in REQ["windows"]["git_config"].items()}
        results = by_name(check_env.evaluate(REQ, ready_machine(
            git_config=dict(cfg, **{"core.autocrlf": "true", "core.fscache": None}))))
        self.assertIs(results["git_config:core.autocrlf"].status, Status.FAIL)
        self.assertIs(results["git_config:core.fscache"].status, Status.WARN)

    def test_git_config_values_compare_case_insensitively(self):
        cfg = {k: v["expected"].upper() for k, v in REQ["windows"]["git_config"].items()}
        self.assertIs(self.status_of("git_config:core.autocrlf", git_config=cfg), Status.PASS)

    def test_depot_tools_absent_is_a_warning_before_bootstrap(self):
        self.assertIs(self.status_of("depot_tools_path",
                                     path_entries=[PathEntry(r"C:\Git\cmd", has_git=True)]),
                      Status.WARN)

    def test_depot_tools_shadowed_by_system_git_fails(self):
        entries = [PathEntry(r"C:\Program Files\Git\cmd", has_git=True),
                   PathEntry(r"D:\ghost\depot_tools", has_gclient=True)]
        self.assertIs(self.status_of("depot_tools_path", path_entries=entries), Status.FAIL)

    def test_toolchain_env_var(self):
        self.assertIs(self.status_of("env:DEPOT_TOOLS_WIN_TOOLCHAIN",
                                     env={"DEPOT_TOOLS_WIN_TOOLCHAIN": None}), Status.FAIL)
        self.assertIs(self.status_of("env:DEPOT_TOOLS_WIN_TOOLCHAIN",
                                     env={"DEPOT_TOOLS_WIN_TOOLCHAIN": "1"}), Status.FAIL)

    def test_build_root_disk_thresholds(self):
        self.assertIs(self.status_of("build_root", volume=VolumeFacts(r"D:\g", 53.0, "NTFS")),
                      Status.FAIL)
        self.assertIs(self.status_of("build_root", volume=VolumeFacts(r"D:\g", 180.0, "NTFS")),
                      Status.WARN)
        self.assertIs(self.status_of("build_root", volume=VolumeFacts(r"D:\g", 300.0, "NTFS")),
                      Status.PASS)

    def test_build_root_rejects_fat_and_spaces(self):
        self.assertIs(self.status_of("build_root", volume=VolumeFacts(r"E:\g", 900.0, "FAT32")),
                      Status.FAIL)
        self.assertIs(self.status_of("build_root",
                                     volume=VolumeFacts(r"D:\my builds\g", 900.0, "NTFS")),
                      Status.FAIL)

    def test_no_build_root_is_informational(self):
        self.assertIs(self.status_of("build_root", volume=None), Status.INFO)


class ChromiumVsSelectionTest(unittest.TestCase):
    VS = REQ["windows"]["visual_studio"]
    ENV = {"ProgramFiles": r"C:\Program Files"}
    VS2022 = r"C:\Program Files\Microsoft Visual Studio\2022"
    VS2026 = r"C:\Program Files\Microsoft Visual Studio\18"

    def select(self, existing, **env):
        return check_env.chromium_vs_selection(self.VS, dict(self.ENV, **env),
                                               lambda p: p in existing)

    def test_prefers_2026_over_2022(self):
        chosen = self.select({self.VS2022 + r"\Community", self.VS2026 + r"\BuildTools"})
        self.assertEqual(chosen, self.VS2026 + r"\BuildTools")

    def test_edition_order_matches_vs_toolchain(self):
        chosen = self.select({self.VS2022 + r"\BuildTools", self.VS2022 + r"\Professional"})
        self.assertEqual(chosen, self.VS2022 + r"\Professional")

    def test_install_variable_overrides_fixed_paths(self):
        custom = r"E:\VS\BuildTools"
        chosen = self.select({custom, self.VS2022 + r"\Community"}, vs2022_install=custom)
        self.assertEqual(chosen, custom)

    def test_program_files_x86_is_not_searched(self):
        self.assertIsNone(self.select(
            {r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"}))


class ReportTest(unittest.TestCase):
    def test_report_lists_remedies_for_failures_only(self):
        results = check_env.evaluate(REQ, ready_machine(debugger_version=None))
        report = check_env.format_report(results)
        self.assertIn("To fix:", report)
        self.assertIn("[FAIL] debugging_tools", report)
        self.assertIn("1 failure(s), 0 warning(s).", report)


class ProbeSmokeTest(unittest.TestCase):
    """Runs the real probes on whatever machine executes the tests."""

    def test_collect_facts_runs_on_this_host(self):
        facts = check_env.collect_facts(REQ, build_root=None)
        self.assertEqual(facts.platform, sys.platform)
        # Evaluation must handle whatever this host reports without raising.
        results = check_env.evaluate(REQ, facts)
        self.assertTrue(results)

    @unittest.skipUnless(sys.platform == "win32", "Windows probes")
    def test_windows_probes_return_plausible_values(self):
        facts = check_env.collect_facts(REQ, build_root=None)
        self.assertGreater(facts.ram_gb, 1)
        self.assertGreaterEqual(facts.windows_major, 10)
        for version in facts.sdk_versions:
            repo.parse_version(version)


if __name__ == "__main__":
    unittest.main()
