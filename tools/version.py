#!/usr/bin/env python3
"""The firmware version: SemVer 2.0.0 derived from the v* git tag, and from nothing else.

THE CONTRACT is the table below; tests/python/test_version.py tests every row of it, and
docs/development/releasing.md documents it for people. The tag is the only source: no
version.txt, no PROJECT_VER typed into CMake, no version in package.json -- every hand-written
copy is a second value that drifts from the first. CMakeLists.txt (PROJECT_VER, so
esp_app_desc_t.version), tools/make_release.py and the release workflow all ask this script.
A tag is `vMAJOR.MINOR.PATCH[-PRERELEASE]` in strict SemVer 2.0.0; a tag that is not, or a
result longer than 31 characters (esp_app_desc_t.version), refuses the build.

    exactly on v0.1.0, clean      0.1.0
    N commits after v0.1.0        0.1.1-dev.N+g<sha7>          next PATCH as a pre-release
    the same, dirty tree          0.1.1-dev.N+g<sha7>.dirty
    no v* tag at all              0.0.0-dev.N+g<sha7>          N = commits in the history
    no git (a source archive)     0.0.0+unknown

Only a release version -- no pre-release and no build metadata, i.e. built exactly on its tag
from a clean tree -- is published by make_release.py and the workflow (is_release()).

Two further rows, which follow from the ones above:
* a dirty tree ON the tag is `0.1.1-dev.0+g<sha7>.dirty`, not `0.1.0`: it is the tag plus
  changes nobody committed, and claiming the release number for it is exactly the lie a version
  exists to prevent. It is the N-commits row with N = 0, so no extra rule is needed.
* N commits after a pre-release tag v0.2.0-rc.1 is `0.2.0-rc.1.dev.N+g<sha7>`. DO NOT bump the
  PATCH there: 0.2.1-dev.N ranks ABOVE 0.2.0 (SemVer §11), which has not been released yet.
  Appending identifiers keeps it above rc.1 and below both rc.2 and 0.2.0.

`python3 tools/version.py` prints the version for this repository; `--check-tag vX.Y.Z` exits
non-zero unless the tree's version is exactly that tag; `--cmake` adds the git files whose change
must re-run CMake (see CMakeLists.txt).
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import List, Optional, Sequence, Tuple

ROOT = Path(__file__).resolve().parents[1]
UNKNOWN = "0.0.0+unknown"

# esp_app_desc_t.version is char[32]: 31 characters and the NUL. ESP-IDF truncates a longer
# PROJECT_VER without a word, and a truncated SemVer string is usually not SemVer any more
# ("...+g1234567.dirt"), so a version that does not fit is refused, not shortened.
MAX_LEN = 31

# The official SemVer 2.0.0 regular expression, verbatim from
# https://semver.org/#is-there-a-suggested-regular-expression-regex-to-check-a-semver-string
# (the numbered-groups variant). Applied with fullmatch and re.ASCII: Python's `$` also matches
# before a trailing newline, and its `\d` matches any Unicode digit -- neither is SemVer.
SEMVER_RE = re.compile(
    r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)"
    r"(?:-((?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*)(?:\.(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*))*))?"
    r"(?:\+([0-9a-zA-Z-]+(?:\.[0-9a-zA-Z-]+)*))?$", re.ASCII)

# `git describe --tags --long --dirty`: <tag>-<N>-g<sha>[-dirty]. The tag part is greedy so a
# pre-release tag's own hyphens (v0.2.0-rc.1) stay inside it; --long guarantees the -N-g tail.
DESCRIBE_RE = re.compile(r"v(?P<tag>.+)-(?P<n>\d+)-g(?P<sha>[0-9a-f]+)(?P<dirty>-dirty)?",
                         re.ASCII)

# Only v-prefixed tags whose next character is a digit are versions. Any other tag in the
# repository (release-7, nightly, a marker from earlier tooling) is not ours to interpret.
MATCH = "v[0-9]*"


class VersionError(ValueError):
    """A version that must not be built, with the reason a person can act on."""


def _parse(version: str) -> Tuple[int, int, int, Optional[str], Optional[str]]:
    m = SEMVER_RE.fullmatch(version) if isinstance(version, str) else None
    if not m:
        raise VersionError(f"{version!r} is not a SemVer 2.0.0 version (https://semver.org)")
    return int(m[1]), int(m[2]), int(m[3]), m[4], m[5]


def validate(version: str) -> str:
    """The version itself when it is SemVer and fits esp_app_desc_t; VersionError otherwise."""
    _parse(version)
    if len(version) > MAX_LEN:
        raise VersionError(f"version {version!r} is {len(version)} characters; "
                           f"esp_app_desc_t.version holds {MAX_LEN} and ESP-IDF would cut it "
                           "silently into something that is no longer SemVer")
    return version


def is_release(version: str) -> bool:
    """A publishable version: valid, no pre-release and no build metadata -- a build exactly on
    its tag from a clean tree; anything else is a development build and is not released."""
    try:
        _, _, _, pre, build = _parse(version)
    except VersionError:
        return False
    return pre is None and build is None


def semver_key(version: str) -> tuple:
    """A sort key implementing SemVer §11 precedence. The web UI mirrors this; keep it plain.

    MAJOR.MINOR.PATCH numerically; a version without a pre-release ranks above one with
    (1.0.0-rc.1 < 1.0.0); pre-release identifiers compare left to right, numeric ones
    numerically and below alphanumeric ones, alphanumeric ones in ASCII order, and a shorter
    list of otherwise equal identifiers ranks lower (that last rule is Python's own tuple
    order). Build metadata is ignored (§10), so 1.0.0+a and 1.0.0+b are equal."""
    major, minor, patch, pre, _ = _parse(version)
    if pre is None:
        return (major, minor, patch, (1,))
    ids = tuple((0, int(i), "") if i.isdigit() else (1, 0, i) for i in pre.split("."))
    return (major, minor, patch, (0, ids))


def _sha7(sha: str) -> str:
    # git widens --abbrev when seven digits are ambiguous; the length budget assumes seven, and
    # the hash is a hint for a human, not an identifier anything resolves.
    return sha[:7]


def semver_untagged(count: int, sha: str, dirty: bool) -> str:
    """No v* tag anywhere in the history: 0.0.0-dev.<commits>+g<sha7>[.dirty]."""
    return validate(f"0.0.0-dev.{int(count)}+g{_sha7(sha)}" + (".dirty" if dirty else ""))


def semver_from_describe(describe: Optional[str]) -> str:
    """The contract table (module docstring) applied to the output of
    `git describe --tags --long --dirty --match 'v[0-9]*'`; None means no git."""
    if describe is None:
        return UNKNOWN
    m = DESCRIBE_RE.fullmatch(describe)
    if not m:
        raise VersionError(f"{describe!r} is not `git describe --tags --long` output")
    tag, n, dirty = m["tag"], int(m["n"]), bool(m["dirty"])
    try:
        major, minor, patch, pre, build = _parse(tag)
    except VersionError:
        raise VersionError(f"tag 'v{tag}' is not vMAJOR.MINOR.PATCH[-PRERELEASE] in SemVer "
                           "2.0.0 (no leading zeros); delete or replace it") from None
    if build is not None:
        raise VersionError(f"tag 'v{tag}' carries build metadata; a tag names a version, the "
                           "build adds its own metadata")
    if n == 0 and not dirty:
        return validate(tag)
    meta = f"+g{_sha7(m['sha'])}" + (".dirty" if dirty else "")
    if pre is None:
        return validate(f"{major}.{minor}.{patch + 1}-dev.{n}{meta}")
    return validate(f"{major}.{minor}.{patch}-{pre}.dev.{n}{meta}")


def _git(repo: Path, *args: str) -> Optional[str]:
    try:
        r = subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True)
    except OSError:  # no git installed at all
        return None
    return r.stdout.strip() if r.returncode == 0 else None


def _is_own_repo(repo: Path) -> bool:
    # DO NOT drop this check: a source archive unpacked inside some other checkout would
    # otherwise be stamped with that checkout's tags and commit count. ESP-IDF's own
    # git-describe path has exactly that flaw.
    top = _git(repo, "rev-parse", "--show-toplevel")
    return (top is not None and os.path.realpath(top) == os.path.realpath(repo)
            and _git(repo, "rev-parse", "--verify", "-q", "HEAD") is not None)


def version_for_repo(repo: Path = ROOT) -> str:
    """The version of the tree at `repo`. VersionError for a v* tag that is not SemVer."""
    if not _is_own_repo(repo):
        return UNKNOWN
    described = _git(repo, "describe", "--tags", "--long", "--dirty", "--abbrev=7",
                     f"--match={MATCH}")
    if described is not None:
        return semver_from_describe(described)
    # No matching tag: describe fails, so the parts come from two commands that cannot. The
    # --match stays on the second one too: without it `--always` names the nearest NON-v
    # annotated tag ("release-7-1-g...") instead of falling back to the bare hash.
    count = _git(repo, "rev-list", "--count", "HEAD")
    head = _git(repo, "describe", "--always", "--dirty", "--abbrev=7", f"--match={MATCH}")
    if count is None or head is None:
        return UNKNOWN
    return semver_untagged(int(count), head.removesuffix("-dirty"), head.endswith("-dirty"))


def watch_paths(repo: Path = ROOT) -> List[Path]:
    """The git files whose change can change the version: HEAD (a checkout), the current
    branch's ref (a commit), refs/tags (a new tag), packed-refs (refs moved by `git gc`).

    HEAD lives in the worktree's own git dir; refs live in the COMMON dir, which differs from it
    in a linked worktree (where .git is a file pointing at .git/worktrees/<name>). Only existing
    paths are returned: a missing file as a CMake dependency makes Ninja re-run CMake forever.
    Dirtiness is not here -- no single file records it; see tools/pio_version.py."""
    if not _is_own_repo(repo):
        return []
    git_dir = _git(repo, "rev-parse", "--absolute-git-dir")
    common = _git(repo, "rev-parse", "--git-common-dir")
    if git_dir is None or common is None:
        return []
    common_dir = Path(common) if os.path.isabs(common) else Path(repo) / common
    common_dir = Path(os.path.realpath(common_dir))
    out = [Path(git_dir) / "HEAD"]
    branch = _git(repo, "symbolic-ref", "-q", "HEAD")
    if branch:
        out.append(common_dir / branch)
    out += [common_dir / "refs" / "tags", common_dir / "packed-refs"]
    return [p for p in out if p.exists()]


def stamp_is_stale(stamp: Path, current: str) -> bool:
    """True when the version CMake last configured with (written to `stamp`) is not `current`."""
    try:
        return stamp.read_text().strip() != current
    except OSError:
        return True


def main(argv: Sequence[str] = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--repo", type=Path, default=ROOT, help="repository root (default: this one)")
    ap.add_argument("--check-tag", metavar="TAG",
                    help="exit non-zero unless the tree's version is exactly this vX.Y.Z tag")
    ap.add_argument("--cmake", action="store_true",
                    help="print the version, then one git path per line to re-configure on")
    args = ap.parse_args(argv)
    try:
        version = version_for_repo(args.repo)
    except VersionError as e:
        print(f"version: {e}", file=sys.stderr)
        return 1
    if args.check_tag is not None:
        if args.check_tag != f"v{version}":
            print(f"version: the tag is {args.check_tag!r} but the tree is {version!r}; a "
                  "release is built exactly on its tag from a clean tree", file=sys.stderr)
            return 1
    print(version)
    if args.cmake:
        for path in watch_paths(args.repo):
            print(path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
