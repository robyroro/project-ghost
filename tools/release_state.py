# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""A release's state.json: what each stage of tools/release.py did.

    {"tag": T, "stages": {"build": {"inputs": {...}, "outputs": {...},
                                     "started": ISO 8601, "finished": ISO 8601}}}

A stage is done when it is recorded with the same inputs. A stage's inputs
include the hashes of the earlier stages' outputs it uses, so a stage that
runs again makes every later one that depends on it run again too.
"""

from __future__ import annotations

import datetime
import json
import os
from dataclasses import dataclass, field
from pathlib import Path


class StateError(Exception):
    pass


@dataclass
class State:
    path: Path
    tag: str
    stages: dict[str, dict] = field(default_factory=dict)

    @classmethod
    def load(cls, path: Path, tag: str) -> State:
        if not path.exists():
            return cls(path, tag)
        doc = json.loads(path.read_text(encoding="utf-8"))
        if doc.get("tag") != tag:
            raise StateError(f"{path} is the state of {doc.get('tag')}, not of {tag}")
        return cls(path, tag, doc.get("stages", {}))

    def is_done(self, stage: str, inputs: dict) -> bool:
        record = self.stages.get(stage)
        return record is not None and record["inputs"] == inputs

    def outputs(self, stage: str) -> dict:
        return self.stages[stage]["outputs"]

    def record(self, stage: str, inputs: dict, outputs: dict, started: datetime.datetime,
               finished: datetime.datetime) -> None:
        self.stages[stage] = {"inputs": inputs, "outputs": outputs,
                              "started": started.isoformat(timespec="seconds"),
                              "finished": finished.isoformat(timespec="seconds")}
        self.save()

    def forget(self, stage: str) -> None:
        self.stages.pop(stage, None)
        self.save()

    def save(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        temporary = self.path.with_name(self.path.name + ".new")
        temporary.write_text(json.dumps({"tag": self.tag, "stages": self.stages}, indent=1)
                             + "\n", encoding="utf-8", newline="\n")
        os.replace(temporary, self.path)
