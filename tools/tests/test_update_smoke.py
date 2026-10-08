# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import json
import subprocess
import tempfile
import unittest
import unittest.mock
from pathlib import Path

import installer_smoke
import update_smoke


class RequestLogTest(unittest.TestCase):
    def test_allowed_requests_pass(self):
        lines = [json.dumps({"path": "/update?cup2key=1:2",
                             "body": {"request": {"protocol": "4.0", "apps": []}}})]
        self.assertEqual(update_smoke.evaluate_requests(lines), [])

    def test_forbidden_keys_and_no_requests_fail(self):
        lines = [json.dumps({"path": "/update",
                             "body": {"request": {"protocol": "4.0", "hw": {}}}})]
        self.assertEqual(update_smoke.evaluate_requests(lines),
                         ["request 1 carries request.hw"])
        self.assertEqual(update_smoke.evaluate_requests([]), ["the updater sent no request"])

    def test_event_requests_fail_even_when_scrubbed(self):
        # The scrubber removes an app's `event` list, so an event request
        # reaches the server as an app without `updatecheck`.
        check = {"appid": "{a}", "version": "1", "updatecheck": {}}
        event = {"appid": "{b}", "version": "1", "enabled": True}
        lines = [json.dumps({"path": "/update?cup2key=1:2",
                             "body": {"request": {"protocol": "4.0", "apps": [check]}}}),
                 json.dumps({"path": "/update",
                             "body": {"request": {"protocol": "4.0", "apps": [event]}}})]
        self.assertEqual(update_smoke.evaluate_requests(lines),
                         ["request 2 is not an update check: app {b} has no updatecheck"
                          " (an event request)"])


class UpdaterLogTest(unittest.TestCase):
    def test_only_loopback_urls(self):
        log = "x http://127.0.0.1:8484/update y\nz https://example.com/a\n"
        self.assertEqual(update_smoke.foreign_urls(log), ["https://example.com/a"])

    def test_xml_namespaces_are_not_requests(self):
        # The updater logs its scheduled task's XML, whose namespace is a URI.
        log = '<Task xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">'
        self.assertEqual(update_smoke.foreign_urls(log), [])


class RemoteModeTest(unittest.TestCase):
    def test_the_server_is_an_allowed_host(self):
        log = ("a https://203.0.113.5/update b https://203.0.113.5/releases/x.crx3\n"
               "c http://127.0.0.1:8484/update d https://example.com/a\n")
        self.assertEqual(update_smoke.foreign_urls(log, ("https://203.0.113.5",)),
                         ["http://127.0.0.1:8484/update", "https://example.com/a"])

    def test_client_address(self):
        self.assertEqual(update_smoke.client_address("198.51.100.7 51234 203.0.113.5 22\n"),
                         "198.51.100.7")
        with self.assertRaises(ValueError):
            update_smoke.client_address("")

    def test_the_sandbox_gets_network_only_when_asked(self):
        import installer_smoke
        from pathlib import Path
        args = (Path("i"), Path("t"), Path("p"), Path("r"))
        self.assertIn("<Networking>Disable</Networking>", installer_smoke.sandbox_config(*args))
        self.assertIn("<Networking>Enable</Networking>",
                      installer_smoke.sandbox_config(*args, networking=True))

    def test_argument_combinations(self):
        def problem(*argv):
            return update_smoke.argument_problem(update_smoke.parser().parse_args(
                ["sandbox", "--release-version", "1.0.0.1", "--appid", "{a}", *argv]))
        self.assertIsNone(problem("--offline-installer", "o.exe", "--update-crx", "u.crx3",
                                  "--update-version", "1.0.0.2"))
        self.assertIsNone(problem("--offline-installer", "o.exe", "--update-version", "1.0.0.2",
                                  "--server", "https://203.0.113.5"))
        self.assertIsNone(problem("--online-installer", "u.exe", "--server",
                                  "https://203.0.113.5"))
        for argv in ((), ("--offline-installer", "o.exe", "--online-installer", "u.exe"),
                     ("--offline-installer", "o.exe", "--update-version", "1.0.0.2"),
                     ("--online-installer", "u.exe"),
                     ("--online-installer", "u.exe", "--server", "https://h",
                      "--update-version", "1.0.0.2")):
            with self.subTest(argv):
                self.assertIsNotNone(problem(*argv))


class RecoveryArgumentsTest(unittest.TestCase):
    def problem(self, *extra):
        args = update_smoke.parser().parse_args(
            ["sandbox", "--offline-installer", "s.exe", "--release-version", "1",
             "--update-crx", "u.crx3", "--update-version", "2", "--appid", "{A}", *extra])
        return update_smoke.argument_problem(args)

    def test_recovery_needs_both(self):
        self.assertIsNotNone(self.problem("--recovery-crx", "r.crx3"))
        self.assertIsNotNone(self.problem("--recovery-version", "3"))
        self.assertIsNone(self.problem("--recovery-crx", "r.crx3", "--recovery-version", "3"))

    def test_recovery_serves_its_own_package(self):
        self.assertIsNotNone(self.problem("--recovery-crx", "r.crx3", "--recovery-version", "3",
                                          "--server", "https://203.0.113.5"))

    def test_signing_flags_parse(self):
        args = update_smoke.parser().parse_args(
            ["sandbox", "--offline-installer", "s.exe", "--release-version", "1",
             "--update-crx", "u.crx3", "--update-version", "2", "--appid", "{A}", "--tagged",
             "--codesign-cert", "c.cer", "--cup-key", "k.json"])
        self.assertTrue(args.tagged)
        self.assertEqual(args.codesign_cert.name, "c.cer")
        self.assertEqual(args.cup_key.name, "k.json")


class RolloutArgumentsTest(unittest.TestCase):
    BASE = ["sandbox", "--offline-installer", "s.exe", "--release-version", "1.0.0.1",
            "--update-crx", "u.crx3", "--update-version", "1.0.0.2", "--appid", "{a}"]

    def problem(self, *extra):
        return update_smoke.argument_problem(update_smoke.parser().parse_args(self.BASE
                                                                             + list(extra)))

    def test_rollout_with_the_offline_installer_and_an_update(self):
        self.assertIsNone(self.problem("--rollout-server", "srv", "--cup-version", "2"))

    def test_rollout_serves_its_own_package(self):
        self.assertIn("no --server", self.problem("--rollout-server", "srv", "--server", "x"))

    def test_rollout_and_the_recovery_drill_are_separate_runs(self):
        self.assertIn("separate", self.problem("--rollout-server", "srv", "--recovery-crx", "r",
                                               "--recovery-version", "1.0.0.3"))

    def test_the_run_command_carries_rollout(self):
        args = update_smoke.parser().parse_args(["run", "--payload", "p", "--results", "r",
                                                 "--appid", "{a}", "--rollout",
                                                 "--cup-version", "2"])
        self.assertTrue(args.rollout)
        self.assertEqual(args.cup_version, 2)


class SandboxFailureTest(unittest.TestCase):
    EXP = installer_smoke.Expectations(
        product_path="Browser", company_path="", app_name="Ghost", prog_id_prefix="GhostHTML",
        pdf_prog_id_prefix="GhostPDF", url_scheme="ghost", product_name="Project Ghost",
        company_name="Project Ghost", release_version="1.0.0.1", web_version="1.0.0.1")

    def test_a_server_that_cannot_start_is_reported(self):
        # The host waits for the result file; without one it waited its whole timeout.
        missing = ModuleNotFoundError("No module named '_cffi_backend'")
        with tempfile.TemporaryDirectory() as d, unittest.mock.patch.object(
                update_smoke.candidate_server, "CandidateServer", side_effect=missing):
            results = Path(d)
            result = update_smoke.run(results, results, self.EXP, "{a}", "1.0.0.2",
                                      rollout=True)
            self.assertTrue((results / update_smoke.RESULT_FILE).exists())
        self.assertEqual(result["steps"][-1]["name"], "error")
        self.assertIn("_cffi_backend", result["steps"][-1]["failures"][0])

    def test_the_sandbox_python_must_import_the_server(self):
        failed = subprocess.CompletedProcess([], 1, "", "ModuleNotFoundError: No module named "
                                                       "'_cffi_backend'\n")
        with unittest.mock.patch.object(update_smoke.subprocess, "run",
                                        return_value=failed) as run:
            problem = update_smoke.sandbox_python_problem(Path("srv"))
        self.assertIn("_cffi_backend", problem)
        self.assertIn("-s -m pip install --no-user", problem)
        argv = run.call_args.args[0]
        self.assertEqual(argv[1], "-I")  # no user site-packages: the sandbox has none
        self.assertIn("ghost_update", argv[-1])

    def test_a_sandbox_python_that_imports_the_server_is_fine(self):
        ok = subprocess.CompletedProcess([], 0, "", "")
        with unittest.mock.patch.object(update_smoke.subprocess, "run", return_value=ok):
            self.assertIsNone(update_smoke.sandbox_python_problem(Path("srv")))

    def test_no_sandbox_starts_when_its_python_cannot_run_the_server(self):
        with unittest.mock.patch.object(update_smoke, "sandbox_python_problem",
                                        return_value="missing"),                 unittest.mock.patch.object(update_smoke.subprocess, "Popen") as popen:
            code = update_smoke.run_in_sandbox(Path("s.exe"), Path("u.crx3"), "1.0.0.1",
                                               "1.0.0.2", "{a}", 60, rollout_server=Path("srv"),
                                               cup_version=2)
        self.assertEqual(code, 2)
        popen.assert_not_called()


if __name__ == "__main__":
    unittest.main()
