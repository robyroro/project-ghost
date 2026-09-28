# patches/

The changes this project carries against Chromium, as a `git format-patch` series applied in numeric order on top of the tag in [CHROMIUM_VERSION](../CHROMIUM_VERSION).

These files are generated. Don't edit them by hand: apply the series with `tools/patches.py apply`, change the commits in the Chromium checkout, then regenerate with `tools/patches.py export`. The workflow, the required `Why:`/`Upstream:` trailers, and the rules for when a patch is acceptable at all are in [docs/patching.md](../docs/patching.md).

The series is empty until Phase 1. We only add patches that can be built and tested against the pinned tag.
