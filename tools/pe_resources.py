#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Icons inside Windows programs: the icon groups of a PE file, the images of an .ico file.

  check-icon --ico ICO FILE...   exit 1 unless each file's first icon group,
                                 the one Explorer shows, holds exactly ICO's
                                 images

The resource compiler copies each image of an .ico byte for byte into an
RT_ICON resource and lists them, in order, in an RT_GROUP_ICON. So "this
program carries that .ico" is a comparison of bytes. Standard library only:
installer_smoke runs this inside Windows Sandbox with a bare Python.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

RT_ICON = 3
RT_GROUP_ICON = 14
_RESOURCE_DIRECTORY = 2
_SUBDIRECTORY = 0x80000000


class PEError(ValueError):
    pass


@dataclass(frozen=True)
class IconGroup:
    name: str | int
    images: tuple[bytes, ...]


class _PE:
    def __init__(self, data: bytes):
        self.data = data
        if data[:2] != b"MZ":
            raise PEError("not a PE file: no MZ header")
        pe = self._u32(0x3C)
        if data[pe:pe + 4] != b"PE\0\0":
            raise PEError("not a PE file: no PE signature")
        coff = pe + 4
        section_count = self._u16(coff + 2)
        optional = coff + 20
        optional_size = self._u16(coff + 16)
        magic = self._u16(optional)
        if magic not in (0x10B, 0x20B):
            raise PEError(f"unknown optional header magic {magic:#x}")
        directories = optional + (96 if magic == 0x10B else 112)
        count = self._u32(directories - 4)  # NumberOfRvaAndSizes
        self.resource_rva = (self._u32(directories + 8 * _RESOURCE_DIRECTORY)
                             if count > _RESOURCE_DIRECTORY else 0)
        if not self.resource_rva:
            raise PEError("no resource section")
        table = optional + optional_size
        self.sections = []
        for i in range(section_count):
            virtual_size, rva, raw_size, raw = struct.unpack_from("<IIII", data,
                                                                  table + 40 * i + 8)
            self.sections.append((rva, max(virtual_size, raw_size), raw))
        self.resource_base = self.offset(self.resource_rva)

    def _u16(self, at: int) -> int:
        return struct.unpack_from("<H", self.data, at)[0]

    def _u32(self, at: int) -> int:
        return struct.unpack_from("<I", self.data, at)[0]

    def offset(self, rva: int) -> int:
        for start, size, raw in self.sections:
            if start <= rva < start + size:
                return raw + rva - start
        raise PEError(f"RVA {rva:#x} is in no section")

    def entries(self, directory: int) -> list[tuple[str | int, int]]:
        """A resource directory's entries: (name or id, target), in file order."""
        at = self.resource_base + directory
        named, ids = struct.unpack_from("<HH", self.data, at + 12)
        result = []
        for i in range(named + ids):
            name, target = struct.unpack_from("<II", self.data, at + 16 + 8 * i)
            if name & _SUBDIRECTORY:
                s = self.resource_base + (name & ~_SUBDIRECTORY)
                length = self._u16(s)
                name = self.data[s + 2:s + 2 + 2 * length].decode("utf-16-le")
            result.append((name, target))
        return result

    def resources(self, kind: int) -> list[tuple[str | int, bytes]]:
        """Every resource of one type, in directory order: (name, data of its first language)."""
        types = dict(self.entries(0))
        if kind not in types:
            return []
        result = []
        for name, target in self.entries(types[kind] & ~_SUBDIRECTORY):
            language_target = self.entries(target & ~_SUBDIRECTORY)[0][1]
            rva, size = struct.unpack_from("<II", self.data,
                                           self.resource_base + language_target)
            start = self.offset(rva)
            result.append((name, self.data[start:start + size]))
        return result


def icon_groups(data: bytes) -> list[IconGroup]:
    """A PE file's icon groups in directory order: named ones first, then by id.
    Windows shows the first as the file's icon."""
    try:
        pe = _PE(data)
        icons = dict(pe.resources(RT_ICON))
        groups = []
        for name, group in pe.resources(RT_GROUP_ICON):
            count = struct.unpack_from("<H", group, 4)[0]
            ids = [struct.unpack_from("<H", group, 6 + 14 * i + 12)[0] for i in range(count)]
            missing = [i for i in ids if i not in icons]
            if missing:
                raise PEError(f"icon group {name} names missing icons {missing}")
            groups.append(IconGroup(name, tuple(icons[i] for i in ids)))
        return groups
    except (struct.error, IndexError, KeyError) as e:
        raise PEError(f"malformed PE file: {e}") from None


def ico_images(data: bytes) -> list[bytes]:
    """An .ico file's images, in directory order."""
    try:
        reserved, kind, count = struct.unpack_from("<HHH", data, 0)
        if reserved != 0 or kind != 1:
            raise PEError("not an .ico file")
        images = []
        for i in range(count):
            size, offset = struct.unpack_from("<II", data, 6 + 16 * i + 8)
            images.append(data[offset:offset + size])
        return images
    except struct.error as e:
        raise PEError(f"malformed .ico file: {e}") from None


def digests(images) -> list[str]:
    return [hashlib.sha256(image).hexdigest() for image in images]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    check = sub.add_parser("check-icon")
    check.add_argument("--ico", type=Path, required=True)
    check.add_argument("files", type=Path, nargs="+")
    args = parser.parse_args(argv)

    want = digests(ico_images(args.ico.read_bytes()))
    failed = 0
    for path in args.files:
        try:
            groups = icon_groups(path.read_bytes())
            reason = ("" if groups and digests(groups[0].images) == want
                      else f"its first icon group is not {args.ico.name}")
        except PEError as e:
            reason = str(e)
        print(f"{'FAILED' if reason else 'ok':8}{path}" + (f": {reason}" if reason else ""))
        failed += bool(reason)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
