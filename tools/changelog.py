#!/usr/bin/env python3
"""Print one version's section of CHANGELOG.md: the text of its GitHub release.

The release workflow publishes this as the release body and fails when the section is missing:
a release nobody described is one the owner cannot tell apart from the previous one (see
docs/development/releasing.md). A Python function instead of an awk
one-liner in the workflow, because the workflow cannot be run before the first tag and this can:
tests/python/test_changelog.py pins the two edges awk gets wrong without a word -- the last
section running into the link references at the bottom of the file, and `0.1` matching
`## [0.1.0]`.

The format is Keep a Changelog 1.1.0 (https://keepachangelog.com/en/1.1.0/): one
`## [X.Y.Z] - YYYY-MM-DD` heading per release, `## [Unreleased]` on top, and
`[X.Y.Z]: <url>` link reference definitions at the end.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Sequence

ROOT = Path(__file__).resolve().parents[1]

HEADING = re.compile(r"^## \[(?P<name>[^\]]+)\]")
# A Markdown link reference definition ("[0.1.0]: https://..."): the footer, not section text.
LINK_REF = re.compile(r"^\[[^\]]+\]:\s")


class ChangelogError(Exception):
    """No usable section for the version asked for."""


def section(text: str, version: str) -> str:
    """The body under `## [version]`, without its heading, stripped. A leading `v` is dropped,
    so the tag name works as well as the version."""
    want = version[1:] if version.startswith("v") else version
    body, inside = [], False
    for line in text.splitlines():
        m = HEADING.match(line)
        if inside and (m or line.startswith("# ") or LINK_REF.match(line)):
            break
        if m and m["name"] == want:
            inside = True
        elif inside:
            body.append(line)
    if not inside:
        raise ChangelogError(f"CHANGELOG.md has no '## [{want}]' section; write one before "
                             "releasing -- it becomes the release notes")
    out = "\n".join(body).strip()
    if not out:
        raise ChangelogError(f"CHANGELOG.md section [{want}] is empty")
    return out


def main(argv: Sequence[str] = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--section", required=True, help="version (X.Y.Z, vX.Y.Z or Unreleased)")
    ap.add_argument("--file", type=Path, default=ROOT / "CHANGELOG.md")
    args = ap.parse_args(argv)
    try:
        print(section(args.file.read_text(), args.section))
    except (ChangelogError, OSError) as e:
        print(f"changelog: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
