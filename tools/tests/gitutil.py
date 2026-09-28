# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Hermetic git repositories for tests.

Developer and CI machines carry git config that changes output (autocrlf on
Windows runners, diff.algorithm, format.* settings). Tests must see the same
git behaviour everywhere, so system and global config are disabled for the
whole test process via isolated_git_env().
"""

from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock


class GitTestCase(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.tmp = Path(tmp.name)
        empty_config = self.tmp / "empty-gitconfig"
        empty_config.write_text("")
        env = {
            "GIT_CONFIG_NOSYSTEM": "1",
            "GIT_CONFIG_GLOBAL": str(empty_config),
            "GIT_AUTHOR_NAME": "Test Author",
            "GIT_AUTHOR_EMAIL": "author@example.invalid",
            "GIT_AUTHOR_DATE": "2026-01-01T00:00:00+00:00",
            "GIT_COMMITTER_NAME": "Test Committer",
            "GIT_COMMITTER_EMAIL": "committer@example.invalid",
            "GIT_COMMITTER_DATE": "2026-01-01T00:00:00+00:00",
        }
        patcher = mock.patch.dict(os.environ, env)
        patcher.start()
        self.addCleanup(patcher.stop)

    def git(self, repo: Path, *args: str) -> str:
        proc = subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True,
                              encoding="utf-8")
        if proc.returncode != 0:
            raise AssertionError(f"git {' '.join(args)} failed:\n{proc.stdout}{proc.stderr}")
        return proc.stdout

    def init_repo(self, name: str) -> Path:
        path = self.tmp / name
        path.mkdir()
        self.git(path, "init", "-q", "-b", "main")
        return path

    def write(self, repo: Path, rel: str, content: str | bytes) -> None:
        path = repo / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        if isinstance(content, bytes):
            path.write_bytes(content)
        else:
            path.write_text(content, encoding="utf-8", newline="\n")

    def commit(self, repo: Path, files: dict[str, str | bytes], message: str) -> str:
        for rel, content in files.items():
            self.write(repo, rel, content)
        self.git(repo, "add", "-A")
        self.git(repo, "commit", "-q", "-m", message)
        return self.git(repo, "rev-parse", "HEAD").strip()
