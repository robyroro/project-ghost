# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Release builds' timestamp, which build/args/release.gn names for gn.

Upstream's //build/compute_build_timestamp.py gives official builds the
last commit's time plus chrome/VERSION's PATCH, in seconds. Ghost's release
builds rewrite that PATCH for every respin (ADR 0007), and the timestamp is
in every link's /TIMESTAMP, so a respin relinked every host tool (protoc,
nasm, tblgen, ...), which then regenerated all they write: thousands of
actions. This script adds the Chromium release's PATCH instead, which
changes only with the pin, so respins of one release share a timestamp.
Ghost runs no symbol server, the reason upstream tells patches apart here.

gn runs it with one argument, the build type. A release build's main
toolchain is official; some host toolchains aren't, and get upstream's
answer, which doesn't read chrome/VERSION.
"""

import calendar
import datetime
import subprocess
import sys
from pathlib import Path

GHOST = Path(__file__).resolve().parent.parent
COMMIT_TIME = GHOST.parent / "build" / "util" / "LASTCHANGE.committime"
CHROMIUM_VERSION = GHOST / "CHROMIUM_VERSION"
UPSTREAM = GHOST.parent / "build" / "compute_build_timestamp.py"


def build_timestamp(commit_time: Path, chromium_version: Path) -> int:
    seconds = int(commit_time.read_text(encoding="utf-8").strip())
    # Upstream round-trips through datetime too; the result is the same int.
    date = datetime.datetime.fromtimestamp(seconds, datetime.timezone.utc)
    patch = int(chromium_version.read_text(encoding="utf-8").strip().split(".")[3])
    return calendar.timegm(date.utctimetuple()) + patch


def main(argv: list[str]) -> int:
    if argv == ["official"]:
        print(build_timestamp(COMMIT_TIME, CHROMIUM_VERSION))
        return 0
    if argv == ["default"]:
        return subprocess.run([sys.executable, str(UPSTREAM), "default"]).returncode
    print(f"usage: {Path(__file__).name} official|default (got {argv})", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
