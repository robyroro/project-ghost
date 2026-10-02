# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import copy
import json
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

import installer_smoke
import repo

EXP = installer_smoke.Expectations(
    product_path="Browser", company_path="Project Ghost", app_name="Project Ghost", prog_id_prefix="GhostHTM",
    pdf_prog_id_prefix="GhostPDF", url_scheme="projectghost", product_name="Project Ghost",
    company_name="Project Ghost", release_version="152.0.7977.14001",
    web_version="152.0.7977.140")
LOCALAPPDATA = r"C:\Users\WDAGUtilityAccount\AppData\Local"
CHROME = LOCALAPPDATA + r"\Project Ghost\Browser\Application\chrome.exe"
PROGRAMS = r"C:\Users\WDAGUtilityAccount\AppData\Roaming\Microsoft\Windows\Start Menu\Programs"
DESKTOP = r"C:\Users\WDAGUtilityAccount\Desktop"

INSTALLED = {
    "chrome_exe": CHROME,
    "start_menu": PROGRAMS,
    "desktop": DESKTOP,
    "files": {"chrome.exe": True, "setup.exe": True},
    "uninstall": {"DisplayName": "Project Ghost", "Publisher": "Project Ghost",
                  "DisplayVersion": "152.0.7977.14001"},
    "software": ["Clients", "Microsoft", "Project Ghost"],
    "product_parent_keys": ["Browser"],
    "start_menu_internet": ["Project Ghost.ABCDEFGHIJKLMNOPQRSTUVWXYZ"],
    "classes": ["GhostHTM.ABCDEFGHIJKLMNOPQRSTUVWXYZ", "GhostPDF.ABCDEFGHIJKLMNOPQRSTUVWXYZ",
                "projectghost"],
    "shortcuts": {PROGRAMS + r"\Project Ghost.lnk": CHROME,
                  DESKTOP + r"\Project Ghost.lnk": CHROME},
    "version_info": {"ProductName": "Project Ghost", "CompanyName": "Project Ghost"},
}
UNINSTALLED = {
    "chrome_exe": CHROME,
    "start_menu": PROGRAMS,
    "desktop": DESKTOP,
    "files": {"chrome.exe": False, "setup.exe": False},
    "uninstall": None,
    "software": ["Clients", "Microsoft", "Project Ghost"],
    "product_parent_keys": [],
    "start_menu_internet": [],
    "classes": [],
    "shortcuts": {},
    "version_info": None,
}


def changed(snapshot, **changes):
    result = copy.deepcopy(snapshot)
    result.update(changes)
    return result


class ExpectationsTest(unittest.TestCase):
    def test_come_from_the_branding_directory_the_pin_and_the_installer(self):
        exp = installer_smoke.expectations(repo.REPO_ROOT, release_version="152.0.7977.14901")
        self.assertEqual(exp, installer_smoke.Expectations(
            product_path="Browser", company_path="Project Ghost", app_name="Project Ghost",
            prog_id_prefix="GhostHTM",
            pdf_prog_id_prefix="GhostPDF", url_scheme="projectghost",
            product_name="Project Ghost", company_name="Project Ghost",
            release_version="152.0.7977.14901", web_version=repo.read_chromium_version()))

    def test_round_trip_through_json(self):
        # The build machine writes them; the sandbox reads them.
        self.assertEqual(installer_smoke.Expectations(**json.loads(json.dumps(EXP.__dict__))),
                         EXP)


class InstalledTest(unittest.TestCase):
    def failures(self, **changes):
        return installer_smoke.evaluate_installed(changed(INSTALLED, **changes), EXP)

    def test_a_complete_install_passes(self):
        self.assertEqual(installer_smoke.evaluate_installed(INSTALLED, EXP), [])

    def test_missing_browser(self):
        self.assertTrue(self.failures(files={"chrome.exe": False, "setup.exe": True}))

    def test_apps_and_features_entry_names_the_product_and_version(self):
        for field, wrong in (("DisplayName", "Chromium"), ("Publisher", "The Chromium Authors"),
                             ("DisplayVersion", "151.0.0.0")):
            entry = dict(INSTALLED["uninstall"], **{field: wrong})
            self.assertTrue(any(field in f for f in self.failures(uninstall=entry)), field)
        self.assertTrue(self.failures(uninstall=None))

    def test_apps_and_features_shows_the_release_version_not_chromiums(self):
        entry = dict(INSTALLED["uninstall"], DisplayVersion=EXP.web_version)
        self.assertTrue(any("DisplayVersion" in f for f in self.failures(uninstall=entry)))

    def test_browser_registration(self):
        classes = INSTALLED["classes"]
        self.assertTrue(self.failures(start_menu_internet=[]))
        for missing in classes:
            self.assertTrue(self.failures(classes=[c for c in classes if c != missing]), missing)

    def test_registry_directory(self):
        self.assertTrue(self.failures(product_parent_keys=[]))

    def test_nothing_is_written_outside_the_company_key(self):
        # Software\Browser would be a generic name another program could own.
        self.assertTrue(any("outside" in f for f in self.failures(
            software=INSTALLED["software"] + ["Browser"])))

    def test_shortcuts_are_named_for_the_product_and_open_it(self):
        self.assertTrue(self.failures(shortcuts={DESKTOP + r"\Project Ghost.lnk": CHROME}))
        self.assertTrue(self.failures(shortcuts={
            PROGRAMS + r"\Project Ghost.lnk": r"C:\elsewhere\chrome.exe",
            DESKTOP + r"\Project Ghost.lnk": CHROME}))

    def test_file_properties_name_the_product(self):
        self.assertTrue(self.failures(version_info={"ProductName": "Chromium",
                                                    "CompanyName": "Project Ghost"}))

    def test_nothing_is_named_chromium(self):
        # Ghost can be installed next to Chromium only if it claims none of
        # Chromium's names.
        self.assertTrue(self.failures(software=["Chromium", "Project Ghost"]))
        self.assertTrue(self.failures(classes=INSTALLED["classes"] + ["ChromiumHTM.ABC"]))
        self.assertTrue(self.failures(shortcuts=dict(INSTALLED["shortcuts"],
                                                     **{PROGRAMS + r"\Chromium.lnk": CHROME})))


class UninstalledTest(unittest.TestCase):
    def failures(self, **changes):
        return installer_smoke.evaluate_uninstalled(changed(UNINSTALLED, **changes), EXP)

    def test_a_clean_uninstall_passes(self):
        self.assertEqual(installer_smoke.evaluate_uninstalled(UNINSTALLED, EXP), [])

    def test_leftovers_fail(self):
        self.assertTrue(self.failures(files={"chrome.exe": True, "setup.exe": False}))
        self.assertTrue(self.failures(uninstall=INSTALLED["uninstall"]))
        self.assertTrue(self.failures(start_menu_internet=INSTALLED["start_menu_internet"]))
        self.assertTrue(self.failures(classes=["GhostHTM.ABCDEFGHIJKLMNOPQRSTUVWXYZ"]))
        self.assertTrue(self.failures(shortcuts={DESKTOP + r"\Project Ghost.lnk": CHROME}))

    def test_every_registration_is_a_leftover(self):
        # 152.0.7977.140's uninstall left the PDF ProgID behind
        # (crbug.com/40384442).
        for leftover in INSTALLED["classes"]:
            self.assertTrue(self.failures(classes=[leftover]), leftover)
        self.assertTrue(self.failures(product_parent_keys=["Browser"]))

    def test_the_company_key_may_hold_only_the_updater(self):
        self.assertEqual(self.failures(product_parent_keys=["Update"]), [])
        self.assertTrue(self.failures(product_parent_keys=["Update", "Other"]))
        self.assertTrue(self.failures(software=UNINSTALLED["software"] + ["Browser"]))

    def test_a_shortcut_to_the_browser_under_another_name_is_a_leftover(self):
        # The browser creates profile shortcuts such as "Person 1 - Project Ghost".
        self.assertTrue(self.failures(shortcuts={DESKTOP + r"\Person 1 - Project Ghost.lnk":
                                                 CHROME.upper()}))

    def test_a_clean_machine_is_what_install_starts_from(self):
        # The same check guards `run` against a machine that has Ghost already.
        self.assertEqual(installer_smoke.evaluate_uninstalled(UNINSTALLED, EXP), [])
        self.assertTrue(installer_smoke.evaluate_uninstalled(INSTALLED, EXP))


class LayoutTest(unittest.TestCase):
    def test_company_and_product_paths(self):
        self.assertEqual(EXP.install_dir_parts, ("Project Ghost", "Browser"))
        self.assertEqual(EXP.registry_root, "Project Ghost")
        self.assertEqual(EXP.uninstall_key, "Project Ghost Browser")

    def test_without_a_company(self):
        exp = installer_smoke.Expectations(**{**EXP.__dict__, "company_path": ""})
        self.assertEqual(exp.install_dir_parts, ("Browser",))
        self.assertEqual(exp.registry_root, "Browser")
        self.assertEqual(exp.uninstall_key, "Browser")


class LaunchTest(unittest.TestCase):
    def test_the_browser_reports_chromiums_version(self):
        self.assertEqual(installer_smoke.evaluate_launch(
            {"product": "Chrome/152.0.7977.140"}, EXP), [])
        self.assertTrue(installer_smoke.evaluate_launch({"product": "Chrome/151.0.1.2"}, EXP))

    def test_the_browser_never_reports_the_release_version(self):
        self.assertTrue(installer_smoke.evaluate_launch(
            {"product": "Chrome/152.0.7977.14001"}, EXP))


class DisposableTest(unittest.TestCase):
    def test_runs_only_where_an_install_does_no_harm(self):
        self.assertTrue(installer_smoke.is_disposable("WDAGUtilityAccount", flag=False))
        self.assertTrue(installer_smoke.is_disposable("robyv", flag=True))
        self.assertFalse(installer_smoke.is_disposable("robyv", flag=False))


class SandboxConfigTest(unittest.TestCase):
    def setUp(self):
        self.config = ET.fromstring(installer_smoke.sandbox_config(
            installer_dir=Path(r"D:\out\vanilla"), tools_dir=Path(r"D:\ghost\tools"),
            python_dir=Path(r"C:\Python314"), results_dir=Path(r"C:\tmp\results & more")))

    def test_has_no_network(self):
        # The installer needs none, and the launch step then contacts nothing.
        self.assertEqual(self.config.findtext("Networking"), "Disable")

    def test_only_the_results_folder_is_writable(self):
        folders = {f.findtext("SandboxFolder"): (f.findtext("HostFolder"), f.findtext("ReadOnly"))
                   for f in self.config.iter("MappedFolder")}
        self.assertEqual(folders, {
            r"C:\ghost\installer": (r"D:\out\vanilla", "true"),
            r"C:\ghost\tools": (r"D:\ghost\tools", "true"),
            r"C:\ghost\python": (r"C:\Python314", "true"),
            r"C:\ghost\results": (r"C:\tmp\results & more", "false"),
        })

    def test_runs_the_smoke_test_then_shuts_the_sandbox_down(self):
        command = self.config.findtext("LogonCommand/Command")
        self.assertIn(r"C:\ghost\python\python.exe C:\ghost\tools\installer_smoke.py run "
                      r"--installer C:\ghost\installer\mini_installer.exe "
                      r"--results C:\ghost\results", command)
        self.assertTrue(command.endswith("shutdown /s /t 0\""))

    def test_keeps_the_runs_output(self):
        # Without it, a run that fails before writing its result leaves no trace.
        command = self.config.findtext("LogonCommand/Command")
        self.assertIn(r"--results C:\ghost\results > C:\ghost\results\run.log 2>&1 & shutdown",
                      command)


class StageTest(unittest.TestCase):
    def test_installer_runs_from_a_writable_copy(self):
        # mini_installer unpacks into a directory it creates beside itself;
        # from the read-only mapped folder it exits with 109
        # (UNABLE_TO_GET_WORK_DIRECTORY).
        with tempfile.TemporaryDirectory() as src, tempfile.TemporaryDirectory() as work:
            original = Path(src) / "mini_installer.exe"
            original.write_bytes(b"MZ installer")
            staged = installer_smoke.stage_installer(original, Path(work))
            self.assertEqual(staged, Path(work) / "mini_installer.exe")
            self.assertEqual(staged.read_bytes(), b"MZ installer")


class ResultTest(unittest.TestCase):
    def test_report_lists_every_step_and_failure(self):
        result = {"steps": [{"name": "install", "failures": []},
                            {"name": "uninstall", "failures": ["uninstall entry left behind"]}]}
        report = installer_smoke.format_result(result)
        self.assertIn("ok      install", report)
        self.assertIn("FAILED  uninstall", report)
        self.assertIn("uninstall entry left behind", report)

    def test_a_result_passes_only_when_every_step_does(self):
        self.assertTrue(installer_smoke.passed({"steps": [{"name": "a", "failures": []}]}))
        self.assertFalse(installer_smoke.passed({"steps": [{"name": "a", "failures": ["x"]}]}))
        self.assertFalse(installer_smoke.passed({"steps": []}))

    def test_reads_the_result_the_sandbox_writes(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / installer_smoke.RESULT_FILE
            path.write_text(json.dumps({"steps": [{"name": "install", "failures": []}]}))
            self.assertTrue(installer_smoke.passed(installer_smoke.read_result(Path(tmp))))


if __name__ == "__main__":
    unittest.main()
