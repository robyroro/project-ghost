# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import io
import struct
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

import pe_resources

RSRC_RVA, RSRC_RAW = 0x1000, 0x200


def _resource_section(resources: dict, rva: int) -> bytes:
    """A resource tree: {type: [(name, data)]}, one language (1033) each.
    Entries are written in the order given; put named ones first, as the
    format requires."""
    def size(n): return 16 + 8 * n
    types = sorted(resources)
    off = size(len(types))
    type_off, lang_off, entry_off, str_off, data_off = {}, {}, {}, {}, {}
    for t in types:
        type_off[t] = off
        off += size(len(resources[t]))
    for t in types:
        for i, _ in enumerate(resources[t]):
            lang_off[t, i] = off
            off += size(1)
    for t in types:
        for i, _ in enumerate(resources[t]):
            entry_off[t, i] = off
            off += 16
    for t in types:
        for i, (name, _) in enumerate(resources[t]):
            if isinstance(name, str):
                str_off[t, i] = off
                off += 2 + 2 * len(name)
    for t in types:
        for i, (_, blob) in enumerate(resources[t]):
            data_off[t, i] = off
            off += len(blob)
    out = bytearray(off)

    def directory(at, entries):
        named = sum(1 for n, _ in entries if n & 0x80000000)
        struct.pack_into("<IIHHHH", out, at, 0, 0, 0, 0, named, len(entries) - named)
        for k, (n, target) in enumerate(entries):
            struct.pack_into("<II", out, at + 16 + 8 * k, n, target)

    directory(0, [(t, 0x80000000 | type_off[t]) for t in types])
    for t in types:
        items = resources[t]
        directory(type_off[t], [
            ((0x80000000 | str_off[t, i]) if isinstance(name, str) else name,
             0x80000000 | lang_off[t, i]) for i, (name, _) in enumerate(items)])
        for i, (name, blob) in enumerate(items):
            directory(lang_off[t, i], [(1033, entry_off[t, i])])
            struct.pack_into("<IIII", out, entry_off[t, i], rva + data_off[t, i], len(blob), 0, 0)
            if isinstance(name, str):
                struct.pack_into("<H", out, str_off[t, i], len(name))
                out[str_off[t, i] + 2:str_off[t, i] + 2 + 2 * len(name)] = name.encode("utf-16-le")
            out[data_off[t, i]:data_off[t, i] + len(blob)] = blob
    return bytes(out)


def make_pe(resources: dict | None, pe32_plus: bool = True) -> bytes:
    """A minimal PE file whose one section holds `resources`, or that has no
    resource directory when `resources` is None."""
    section = _resource_section(resources or {}, RSRC_RVA)
    directories = 112 if pe32_plus else 96
    opt_size = directories + 16 * 8
    header = bytearray(RSRC_RAW)
    header[0:2] = b"MZ"
    struct.pack_into("<I", header, 0x3C, 64)
    header[64:68] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", header, 68, 0x8664 if pe32_plus else 0x14C, 1, 0, 0, 0,
                     opt_size, 0x22)
    opt = 88
    struct.pack_into("<H", header, opt, 0x20B if pe32_plus else 0x10B)
    struct.pack_into("<I", header, opt + directories - 4, 16)
    if resources is not None:
        struct.pack_into("<II", header, opt + directories + 16, RSRC_RVA, len(section))
    sec = opt + opt_size
    header[sec:sec + 8] = b".rsrc\0\0\0"
    struct.pack_into("<IIII", header, sec + 8, len(section), RSRC_RVA, len(section), RSRC_RAW)
    return bytes(header) + section


def group(*entries) -> bytes:
    """An RT_GROUP_ICON: entries are (icon id, image)."""
    data = struct.pack("<HHH", 0, 1, len(entries))
    for icon_id, image in entries:
        data += struct.pack("<BBBBHHIH", 16, 16, 0, 0, 1, 32, len(image), icon_id)
    return data


def ico(*images) -> bytes:
    data = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    for image in images:
        data += struct.pack("<BBBBHHII", 16, 16, 0, 0, 1, 32, len(image), offset)
        offset += len(image)
    return data + b"".join(images)


APP = make_pe({
    pe_resources.RT_ICON: [(1, b"app16"), (2, b"app32"), (3, b"doc16")],
    pe_resources.RT_GROUP_ICON: [("IDR_MAINFRAME", group((1, b"app16"), (2, b"app32"))),
                                 (7, group((3, b"doc16")))],
})


class IconGroupsTest(unittest.TestCase):
    def test_groups_in_directory_order_with_their_images(self):
        groups = pe_resources.icon_groups(APP)
        self.assertEqual([g.name for g in groups], ["IDR_MAINFRAME", 7])
        self.assertEqual(groups[0].images, (b"app16", b"app32"))
        self.assertEqual(groups[1].images, (b"doc16",))

    def test_pe32_files(self):
        pe = make_pe({pe_resources.RT_ICON: [(1, b"x")],
                      pe_resources.RT_GROUP_ICON: [(1, group((1, b"x")))]}, pe32_plus=False)
        self.assertEqual(pe_resources.icon_groups(pe)[0].images, (b"x",))

    def test_a_file_without_icons_has_no_groups(self):
        self.assertEqual(pe_resources.icon_groups(make_pe({16: [(1, b"version")]})), [])

    def test_a_group_naming_a_missing_icon_is_an_error(self):
        pe = make_pe({pe_resources.RT_ICON: [(1, b"x")],
                      pe_resources.RT_GROUP_ICON: [(1, group((1, b"x"), (9, b"y")))]})
        with self.assertRaisesRegex(pe_resources.PEError, "missing icons"):
            pe_resources.icon_groups(pe)

    def test_errors(self):
        with self.assertRaisesRegex(pe_resources.PEError, "no MZ"):
            pe_resources.icon_groups(b"not a program")
        with self.assertRaisesRegex(pe_resources.PEError, "no resource"):
            pe_resources.icon_groups(make_pe(None))
        with self.assertRaises(pe_resources.PEError):
            pe_resources.icon_groups(APP[:400])


class IcoTest(unittest.TestCase):
    def test_images_in_order(self):
        self.assertEqual(pe_resources.ico_images(ico(b"a", b"bb")), [b"a", b"bb"])

    def test_not_an_ico(self):
        with self.assertRaises(pe_resources.PEError):
            pe_resources.ico_images(b"\0\0\2\0\1\0")

    def test_digests(self):
        self.assertEqual(pe_resources.digests([b"a"]),
                         ["ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb"])


class CheckIconTest(unittest.TestCase):
    def run_check(self, ico_data: bytes, pe_data: bytes) -> tuple[int, str]:
        with tempfile.TemporaryDirectory() as d:
            (Path(d) / "app.ico").write_bytes(ico_data)
            (Path(d) / "x.exe").write_bytes(pe_data)
            out = io.StringIO()
            with redirect_stdout(out):
                code = pe_resources.main(["check-icon", "--ico", str(Path(d) / "app.ico"),
                                          str(Path(d) / "x.exe")])
            return code, out.getvalue()

    def test_passes_when_the_first_group_is_the_ico(self):
        self.assertEqual(self.run_check(ico(b"app16", b"app32"), APP)[0], 0)

    def test_fails_otherwise(self):
        code, out = self.run_check(ico(b"doc16"), APP)
        self.assertEqual(code, 1)
        self.assertIn("FAILED", out)
        code, out = self.run_check(ico(b"a"), b"not a program")
        self.assertEqual(code, 1)
        self.assertIn("no MZ", out)


if __name__ == "__main__":
    unittest.main()
