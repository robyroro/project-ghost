# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import contextlib
import io
from pathlib import Path

import patches
from tests.gitutil import GitTestCase

TAG = "100.0.0.0"
TRAILERS = "Why: Ghost needs a hook here.\nUpstream: not upstreamable: product-specific\n"


def quiet():
    stack = contextlib.ExitStack()
    stack.enter_context(contextlib.redirect_stdout(io.StringIO()))
    stack.enter_context(contextlib.redirect_stderr(io.StringIO()))
    return stack


class SeriesTestCase(GitTestCase):
    """An upstream repo at TAG plus two downstream commits on top."""

    def setUp(self):
        super().setUp()
        self.upstream = self.init_repo("upstream")
        self.commit(self.upstream, {
            "chrome/browser/prefs.cc": "void Register() {\n  A();\n  B();\n}\n",
            "net/base/socket.cc": "int Connect() { return 0; }\n",
            "chrome/installer/crlf.txt": b"line1\r\nline2\r\n",
        }, "Initial upstream")
        self.git(self.upstream, "tag", TAG)
        self.src = self.tmp / "src"
        self.git(self.tmp, "clone", "-q", str(self.upstream), str(self.src))
        self.git(self.src, "checkout", "-q", "-b", "work", TAG)
        self.commit(self.src, {"chrome/browser/prefs.cc":
                               "void Register() {\n  A();\n  ghost::Register();\n  B();\n}\n"},
                    "prefs: call into //ghost\n\nHook for Ghost defaults.\n\n" + TRAILERS)
        self.commit(self.src, {"net/base/socket.cc": "int Connect() { return 1; }\n",
                               "chrome/installer/crlf.txt": b"line1\r\nchanged\r\n"},
                    "net: change connect\n\n" + TRAILERS)
        self.patches_dir = self.tmp / "patches"
        self.patches_dir.mkdir()

    def export(self, src=None):
        with quiet():
            return patches.export(src or self.src, TAG, self.patches_dir)

    def fresh_checkout(self, name: str, rev: str = TAG) -> Path:
        dest = self.tmp / name
        self.git(self.tmp, "clone", "-q", str(self.upstream), str(dest))
        self.git(dest, "checkout", "-q", rev)
        return dest

    def snapshot(self) -> dict[str, bytes]:
        return {p.name: p.read_bytes() for p in patches.list_patches(self.patches_dir)}


class ExportApplyTest(SeriesTestCase):
    def test_export_writes_numbered_series(self):
        self.assertEqual(self.export(), 0)
        self.assertEqual(list(self.snapshot()), ["0001-prefs-call-into-ghost.patch",
                                                 "0002-net-change-connect.patch"])

    def test_round_trip_reproduces_tree_and_bytes(self):
        self.export()
        first = self.snapshot()
        other = self.fresh_checkout("other")
        with quiet():
            self.assertEqual(patches.apply(other, TAG, "ghost/test", self.patches_dir), 0)
        self.assertEqual(self.git(other, "rev-parse", "HEAD^{tree}"),
                         self.git(self.src, "rev-parse", "HEAD^{tree}"))
        # CRLF content must survive mail splitting byte for byte.
        self.assertEqual((other / "chrome/installer/crlf.txt").read_bytes(), b"line1\r\nchanged\r\n")
        self.export(src=other)
        self.assertEqual(self.snapshot(), first)

    def test_export_ignores_user_diff_and_format_config(self):
        self.export()
        baseline = self.snapshot()
        for key, value in [("diff.noprefix", "true"), ("diff.mnemonicPrefix", "true"),
                           ("diff.algorithm", "histogram"), ("format.numbered", "true"),
                           ("format.signature", "custom"), ("format.signOff", "true"),
                           ("diff.context", "8")]:
            self.git(self.src, "config", key, value)
        self.export()
        self.assertEqual(self.snapshot(), baseline)

    def test_export_replaces_stale_patches(self):
        (self.patches_dir / "0009-stale.patch").write_text("old")
        self.export()
        self.assertNotIn("0009-stale.patch", self.snapshot())

    def test_export_rejects_merge_commits(self):
        self.git(self.src, "checkout", "-q", "-b", "side", TAG)
        self.commit(self.src, {"other.txt": "x\n"}, "side\n\n" + TRAILERS)
        self.git(self.src, "checkout", "-q", "work")
        self.git(self.src, "merge", "-q", "--no-edit", "side")
        with self.assertRaisesRegex(patches.PatchError, "merge commits"):
            self.export()

    def test_conflict_stops_with_am_in_progress(self):
        self.export()
        self.commit(self.upstream, {"chrome/browser/prefs.cc":
                                    "void Register() {\n  A();\n  C();\n  B();\n}\n"},
                    "Upstream edits the same lines")
        self.git(self.upstream, "tag", "101.0.0.0")
        other = self.fresh_checkout("conflict", "101.0.0.0")
        with quiet():
            code = patches.apply(other, "101.0.0.0", "ghost/test", self.patches_dir)
        self.assertEqual(code, 1)
        self.assertTrue((other / ".git" / "rebase-apply").is_dir())

    def test_apply_refuses_dirty_tree(self):
        self.export()
        other = self.fresh_checkout("dirty")
        (other / "net/base/socket.cc").write_text("dirty\n")
        with self.assertRaisesRegex(patches.PatchError, "uncommitted"):
            patches.apply(other, TAG, "ghost/test", self.patches_dir)

    def test_apply_refuses_existing_branch_unless_forced(self):
        self.export()
        other = self.fresh_checkout("existing")
        self.git(other, "branch", "ghost/test", TAG)
        with self.assertRaisesRegex(patches.PatchError, "already exists"):
            patches.apply(other, TAG, "ghost/test", self.patches_dir)
        with quiet():
            self.assertEqual(patches.apply(other, TAG, "ghost/test", self.patches_dir,
                                           force=True), 0)

    def test_missing_base_explains_how_to_fetch_it(self):
        with self.assertRaisesRegex(patches.PatchError, "fetch --depth=1 origin tag 999"):
            patches.export(self.src, "refs/tags/999.0.0.0", self.patches_dir)


class CheckTest(SeriesTestCase):
    def setUp(self):
        super().setUp()
        self.export()

    def errors(self):
        return [i.message for i in patches.check(self.patches_dir) if i.error]

    def rewrite(self, name, old: bytes, new: bytes):
        path = self.patches_dir / name
        data = path.read_bytes()
        self.assertIn(old, data)
        path.write_bytes(data.replace(old, new, 1))

    def test_exported_series_is_clean(self):
        issues = patches.check(self.patches_dir)
        self.assertEqual([i.message for i in issues if i.error], [])
        warnings = [i for i in issues if not i.error]
        self.assertEqual([w.patch for w in warnings], ["0002-net-change-connect.patch"])

    def test_missing_trailer(self):
        self.rewrite("0001-prefs-call-into-ghost.patch", b"Upstream: not", b"Upstreamx: not")
        self.assertEqual(self.errors(), ["commit message lacks a 'Upstream:' trailer"])

    def test_empty_trailer_value_does_not_count(self):
        self.rewrite("0001-prefs-call-into-ghost.patch", b"Why: Ghost needs a hook here.",
                     b"Why:")
        self.assertEqual(self.errors(), ["commit message lacks a 'Why:' trailer"])

    def test_crlf_in_message(self):
        self.rewrite("0001-prefs-call-into-ghost.patch", b"Hook for Ghost defaults.\n",
                     b"Hook for Ghost defaults.\r\n")
        self.assertEqual(self.errors(), ["commit message or headers contain CR characters"])

    def test_non_contiguous_numbering(self):
        (self.patches_dir / "0002-net-change-connect.patch").rename(
            self.patches_dir / "0003-net-change-connect.patch")
        self.assertEqual(self.errors(), ["expected sequence number 0002; numbering must be "
                                         "contiguous from 0001"])

    def test_hand_written_patch_is_flagged(self):
        self.rewrite("0001-prefs-call-into-ghost.patch", b"From " + b"0" * 40,
                     b"From " + b"1" * 40)
        self.assertEqual(self.errors(),
                         ["missing zero-hash From line; regenerate with patches.py export"])

    def test_patch_into_own_tree_is_rejected(self):
        self.commit(self.src, {"ghost/foo.cc": "x\n"}, "misplaced\n\n" + TRAILERS)
        self.export()
        self.assertIn("touches ghost/foo.cc; //ghost code belongs in this repository, not in an "
                      "upstream patch", self.errors())

    def test_binary_patch_is_rejected(self):
        self.commit(self.src, {"chrome/app/icon.png": bytes(range(256))}, "icon\n\n" + TRAILERS)
        self.export()
        self.assertTrue(any("binary changes" in e for e in self.errors()))


class StatsTest(SeriesTestCase):
    def test_counts_and_review_flags(self):
        self.export()
        rows = {r.name: r for r in patches.stats(self.patches_dir)}
        prefs = rows["0001-prefs-call-into-ghost.patch"]
        self.assertEqual((prefs.files, prefs.added, prefs.removed, prefs.sensitive),
                         (1, 1, 0, False))
        net = rows["0002-net-change-connect.patch"]
        self.assertEqual((net.files, net.added, net.removed), (2, 2, 2))
        self.assertTrue(net.needs_second_reviewer)

    def test_removed_lines_starting_with_dashes_are_counted(self):
        diff = (b"diff --git a/x b/x\n--- a/x\n+++ b/x\n@@ -1,2 +1,1 @@\n"
                b"--- looks like a header\n keep\n")
        self.assertEqual(patches.count_changes(diff), (0, 1))

    def test_large_patch_needs_second_reviewer(self):
        row = patches.PatchStat("p", 1, patches.LARGE_PATCH_LINES, 1, sensitive=False)
        self.assertTrue(row.needs_second_reviewer)
