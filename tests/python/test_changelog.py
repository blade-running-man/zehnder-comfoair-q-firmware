"""tools/changelog.py: the release notes are one section of CHANGELOG.md, or the release fails.

The release rule (docs/development/releasing.md): the workflow takes the release text from the
matching section and refuses to publish when there is none -- a release nobody described is a
release the owner cannot tell apart from the previous one.
"""
import subprocess
import sys

import pytest

from conftest import _load

cl = _load("changelog")

DOC = """# Changelog

All notable changes are documented here.

## [Unreleased]

### Added

- Something not out yet.

## [0.2.0] - 2026-11-01

### Fixed

- A thing.

## [0.1.0] - 2026-10-20

### Added

- The first release.
- Its second line.

[Unreleased]: https://example.invalid/compare/v0.2.0...HEAD
[0.2.0]: https://example.invalid/compare/v0.1.0...v0.2.0
[0.1.0]: https://example.invalid/releases/tag/v0.1.0
"""


def test_a_middle_section_stops_at_the_next_heading():
    assert cl.section(DOC, "0.2.0") == "### Fixed\n\n- A thing."


def test_the_last_section_stops_at_the_link_references():
    assert cl.section(DOC, "0.1.0") == "### Added\n\n- The first release.\n- Its second line."


def test_unreleased_is_a_section_like_any_other():
    assert cl.section(DOC, "Unreleased") == "### Added\n\n- Something not out yet."


def test_a_leading_v_is_accepted():
    assert cl.section(DOC, "v0.2.0") == cl.section(DOC, "0.2.0")


def test_a_missing_section_is_refused():
    with pytest.raises(cl.ChangelogError, match="0.3.0"):
        cl.section(DOC, "0.3.0")


def test_a_prefix_does_not_match_a_longer_version():
    with pytest.raises(cl.ChangelogError):
        cl.section(DOC, "0.1")


def test_an_empty_section_is_refused():
    with pytest.raises(cl.ChangelogError, match="empty"):
        cl.section("## [0.1.0] - 2026-10-20\n\n## [0.0.9]\n- x\n", "0.1.0")


def test_the_shipped_changelog_has_an_unreleased_section():
    assert cl.section((cl.ROOT / "CHANGELOG.md").read_text(), "Unreleased")


def test_cli(tmp_path):
    f = tmp_path / "CHANGELOG.md"
    f.write_text(DOC)
    ok = subprocess.run([sys.executable, cl.__file__, "--file", str(f), "--section", "0.2.0"],
                        capture_output=True, text=True)
    assert ok.returncode == 0 and ok.stdout == "### Fixed\n\n- A thing.\n"
    bad = subprocess.run([sys.executable, cl.__file__, "--file", str(f), "--section", "9.9.9"],
                         capture_output=True, text=True)
    assert bad.returncode == 1 and "9.9.9" in bad.stderr and bad.stdout == ""
