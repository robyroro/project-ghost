#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Names the product in the Windows installer's strings.

The installer takes its strings from chrome/app/chromium_strings.grd, so
shortcuts, the Apps & features entry, firewall rules and installer messages
say "Chromium". //chrome/installer/util:generate_strings writes them to a .rc
file with every translation resolved; this rewrites that file for the product
named in the branding file:

- The product and company names become the branding values in every
  language. They are names, and upstream's translations of them vary (fr-CA
  calls the company "Les auteurs de Chrome").
- In other strings, "Chromium", and "Google Chrome" or "Google Chromium" in
  some translations, become the product name.
- A translation that still doesn't name the product where the en-US string
  does, or that still names Chromium, Chrome or Google, is replaced by the
  en-US string. Some translations transliterate the name, and a few name
  Google Chrome.

Rewriting the generated file rather than the .grd keeps translations matched
to their messages: they are keyed by a fingerprint of the English text.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# String ids whose whole value is a name, and the branding key that gives it.
NAMES = {"IDS_PRODUCT_NAME": "PRODUCT_FULLNAME",
         "IDS_ABOUT_VERSION_COMPANY_NAME": "COMPANY_FULLNAME"}
_ENTRY = re.compile(r'^  (\w+) "(.*)"$')
_UPSTREAM_NAMES = re.compile(r"Chromium|Chrome|Google")


def read_branding(path: Path) -> dict[str, str]:
    """Parses a BRANDING file: KEY=VALUE lines."""
    values = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        key, sep, value = line.partition("=")
        if sep:
            values[key.strip()] = value.strip()
    return values


def parse(rc_text: str) -> list[tuple[str, str]]:
    """Returns (resource name, unescaped value) for each STRINGTABLE entry."""
    return [(m.group(1), m.group(2).replace('""', '"')) for line in rc_text.split("\n")
            if (m := _ENTRY.match(line))]


def _escape(value: str) -> str:
    return value.replace('"', '""')


def brand(rc_text: str, branding: dict[str, str], header: str) -> str:
    product = branding["PRODUCT_FULLNAME"]
    entries = dict(parse(rc_text))
    # Resource names are <string id>_<language>; every string has en-US.
    ids = sorted({k[:-len("_EN_US")] for k in entries if k.endswith("_EN_US")},
                 key=len, reverse=True)

    def string_id(name: str) -> str:
        return next(i for i in ids if name.startswith(i + "_"))

    def rename(value: str) -> str:
        for upstream in ("Google Chromium", "Google Chrome", "Chromium"):
            value = value.replace(upstream, product)
        return value

    english = {i: rename(entries[i + "_EN_US"]) for i in ids}

    def branded(name: str, value: str) -> str:
        sid = string_id(name)
        if sid in NAMES:
            return branding[NAMES[sid]]
        new = rename(value)
        if ((product in english[sid] and product not in new)
                or _UPSTREAM_NAMES.search(new.replace(product, ""))):
            return english[sid]
        return new

    lines = rc_text.split("\n")
    for i, line in enumerate(lines):
        if m := _ENTRY.match(line):
            name = m.group(1)
            lines[i] = f'  {name} "{_escape(branded(name, entries[name]))}"'
        elif line.startswith("#include "):
            # The rewritten file lives in another directory than the header.
            lines[i] = f'#include "{header}"'
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--branding", type=Path, required=True, help="BRANDING file")
    parser.add_argument("--header", required=True,
                        help="the strings header, as included from the gen directory")
    parser.add_argument("input", type=Path, help=".rc file from generate_strings")
    parser.add_argument("output", type=Path)
    args = parser.parse_args(argv)
    # create_string_rc.py writes UTF-16 with a byte order mark and LF endings.
    text = args.input.read_text(encoding="utf-16")
    args.output.write_text(brand(text, read_branding(args.branding), args.header),
                           encoding="utf-16", newline="\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
