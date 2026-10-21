"""tools/version.py: the firmware version, derived from the v* tag and nothing else.

The derivation table in tools/version.py's module docstring is the contract; every row of it
has a test here. The pure functions run on a bare interpreter. The repository tests build
throwaway git repositories under pytest's tmp_path, because `--match`, `--long` and `--dirty` are
git's behaviour, not ours, and a parser tested only against strings we typed would be tested
against our idea of git.
"""
import os
import subprocess
import sys

import pytest

from conftest import _load

v = _load("version")


# --- the contract table, row by row -------------------------------------------------------

def test_exactly_on_a_tag_clean_is_the_tag():
    assert v.semver_from_describe("v0.1.0-0-gabc1234") == "0.1.0"


def test_commits_after_a_tag_are_the_next_patch_as_a_pre_release():
    assert v.semver_from_describe("v0.1.0-3-gabc1234") == "0.1.1-dev.3+gabc1234"


def test_dirty_after_a_tag_marks_the_build_metadata():
    assert v.semver_from_describe("v0.1.0-3-gabc1234-dirty") == "0.1.1-dev.3+gabc1234.dirty"


def test_dirty_on_the_tag_itself_is_not_the_release():
    # A dirty tree at the tag is the tag plus changes nobody committed: it must not claim 0.1.0.
    out = v.semver_from_describe("v0.1.0-0-gabc1234-dirty")
    assert out == "0.1.1-dev.0+gabc1234.dirty"
    assert not v.is_release(out)


def test_no_tag_at_all_counts_the_history():
    assert v.semver_untagged(173, "74bb1a4", dirty=False) == "0.0.0-dev.173+g74bb1a4"
    assert v.semver_untagged(173, "74bb1a4", dirty=True) == "0.0.0-dev.173+g74bb1a4.dirty"


def test_no_git_is_unknown():
    assert v.semver_from_describe(None) == "0.0.0+unknown"
    assert not v.is_release("0.0.0+unknown")


def test_the_sha_is_cut_to_seven_even_if_git_widened_it():
    # git widens --abbrev when seven digits are ambiguous; the 31-character budget assumes seven.
    assert v.semver_from_describe("v0.1.0-3-gabc1234def") == "0.1.1-dev.3+gabc1234"
    assert v.semver_untagged(1, "abc1234def", dirty=False) == "0.0.0-dev.1+gabc1234"


# --- pre-release tags -----------------------------------------------------------------------

def test_an_rc_tag_is_its_own_version():
    assert v.semver_from_describe("v0.2.0-rc.1-0-gabc1234") == "0.2.0-rc.1"
    assert not v.is_release("0.2.0-rc.1")


def test_commits_after_an_rc_stay_below_the_final():
    # Bumping the PATCH of 0.2.0-rc.1 would give 0.2.1-dev.3 -- above 0.2.0, which is not out.
    out = v.semver_from_describe("v0.2.0-rc.1-3-gabc1234")
    assert out == "0.2.0-rc.1.dev.3+gabc1234"
    assert v.semver_key("0.2.0-rc.1") < v.semver_key(out) < v.semver_key("0.2.0-rc.2")
    assert v.semver_key(out) < v.semver_key("0.2.0")


# --- tags that are refused ------------------------------------------------------------------

@pytest.mark.parametrize("describe", [
    "v0.01.0-0-gabc1234",     # leading zero: SemVer §2
    "v01.0.0-0-gabc1234",
    "v1.2-0-gabc1234",        # matches v[0-9]* but is not X.Y.Z
    "v1.2.3-rc.01-0-gabc1234",  # numeric pre-release identifier with a leading zero (§9)
    "v1.2.3+meta-0-gabc1234",   # build metadata belongs to the build, not the tag
])
def test_a_tag_that_is_not_semver_is_refused(describe):
    with pytest.raises(v.VersionError):
        v.semver_from_describe(describe)


@pytest.mark.parametrize("describe", ["", "v0.1.0", "abc1234", "v0.1.0-x-gabc1234"])
def test_output_that_is_not_describe_long_is_refused(describe):
    with pytest.raises(v.VersionError):
        v.semver_from_describe(describe)


# --- the 31-character limit -----------------------------------------------------------------

def test_31_characters_pass_and_32_are_refused():
    ok = v.semver_from_describe("v9.99.98-9999-g1234567-dirty")
    assert ok == "9.99.99-dev.9999+g1234567.dirty" and len(ok) == 31
    with pytest.raises(v.VersionError, match="31"):
        v.semver_from_describe("v99.99.98-9999-g1234567-dirty")  # 99.99.99-dev.9999+...: 32


def test_the_spec_realistic_maximum_fits():
    assert len(v.validate("1.10.10-dev.999+g1234567.dirty")) == 30


# --- validator ------------------------------------------------------------------------------

@pytest.mark.parametrize("good", ["0.0.0", "1.2.3", "1.0.0-alpha", "1.0.0-0.3.7", "1.0.0-x.7.z.92",
                                  "1.0.0+20130313144700", "1.0.0-beta+exp.sha.5114f85",
                                  "1.0.0-alpha-a.b-c"])
def test_semver_org_examples_are_valid(good):
    assert v.validate(good) == good


@pytest.mark.parametrize("bad", ["", "v1.2.3", "1.2", "1.2.3.4", "01.2.3", "1.2.3-", "1.2.3+",
                                 "1.2.3-01", "1.2.3 ", "../etc", "a/b"])
def test_non_semver_is_refused(bad):
    with pytest.raises(v.VersionError):
        v.validate(bad)


def test_is_release():
    assert v.is_release("0.1.0")
    assert not v.is_release("0.1.1-dev.3+gabc1234")
    assert not v.is_release("0.1.0+gabc1234")
    assert not v.is_release("not a version")


# --- precedence, SemVer §11 -----------------------------------------------------------------

def test_dev_builds_sort_between_the_releases():
    assert v.semver_key("0.1.0") < v.semver_key("0.1.1-dev.3+gabc1234") < v.semver_key("0.1.1")
    assert v.semver_key("0.1.1-dev.3+g1") < v.semver_key("0.1.1-dev.10+g2")


def test_semver_org_precedence_chain():
    chain = ["1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta", "1.0.0-beta.2",
             "1.0.0-beta.11", "1.0.0-rc.1", "1.0.0", "1.0.1", "1.1.0", "2.0.0"]
    keys = [v.semver_key(x) for x in chain]
    assert keys == sorted(keys) and len(set(keys)) == len(keys)


def test_build_metadata_does_not_order():
    assert v.semver_key("1.0.0+a") == v.semver_key("1.0.0+b") == v.semver_key("1.0.0")


def test_numbers_compare_numerically_not_as_text():
    assert v.semver_key("1.9.0") < v.semver_key("1.10.0")


# --- against real git -----------------------------------------------------------------------

GIT_ENV = dict(os.environ, GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_NOSYSTEM="1",
               GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@t", GIT_COMMITTER_NAME="t",
               GIT_COMMITTER_EMAIL="t@t")


def git(repo, *args):
    return subprocess.run(["git", "-C", str(repo), *args], env=GIT_ENV, check=True,
                          capture_output=True, text=True).stdout.strip()


@pytest.fixture
def repo(tmp_path):
    r = tmp_path / "repo"
    r.mkdir()
    git(r, "init", "-q", "-b", "main")
    (r / "f").write_text("0")
    git(r, "add", "f")
    git(r, "commit", "-q", "-m", "c0")
    return r


def commit(repo, n=1):
    for i in range(n):
        (repo / "f").write_text(str(os.urandom(4)))
        git(repo, "commit", "-q", "-am", f"c{i}")


def sha7(repo):
    return git(repo, "rev-parse", "--short=7", "HEAD")


def test_repo_untagged(repo):
    commit(repo, 2)
    assert v.version_for_repo(repo) == f"0.0.0-dev.3+g{sha7(repo)}"


def test_repo_on_tag_then_after_then_dirty(repo):
    git(repo, "tag", "-a", "v0.1.0", "-m", "0.1.0")
    assert v.version_for_repo(repo) == "0.1.0"
    commit(repo, 3)
    assert v.version_for_repo(repo) == f"0.1.1-dev.3+g{sha7(repo)}"
    (repo / "f").write_text("edited")
    assert v.version_for_repo(repo) == f"0.1.1-dev.3+g{sha7(repo)}.dirty"


def test_untracked_files_do_not_make_it_dirty(repo):
    git(repo, "tag", "-a", "v0.1.0", "-m", "0.1.0")
    (repo / "new").write_text("x")
    assert v.version_for_repo(repo) == "0.1.0"


def test_non_v_tags_are_ignored(repo):
    git(repo, "tag", "-a", "v0.1.0", "-m", "0.1.0")
    commit(repo)
    git(repo, "tag", "-a", "release-7", "-m", "not ours")
    git(repo, "tag", "nightly")
    assert v.version_for_repo(repo) == f"0.1.1-dev.1+g{sha7(repo)}"


def test_only_non_v_tags_is_untagged(repo):
    git(repo, "tag", "-a", "release-7", "-m", "not ours")
    assert v.version_for_repo(repo) == f"0.0.0-dev.1+g{sha7(repo)}"


def test_an_rc_tag_in_a_repo(repo):
    git(repo, "tag", "-a", "v0.2.0-rc.1", "-m", "rc")
    assert v.version_for_repo(repo) == "0.2.0-rc.1"


def test_a_bad_v_tag_is_refused(repo):
    git(repo, "tag", "-a", "v0.01.0", "-m", "bad")
    with pytest.raises(v.VersionError, match="v0.01.0"):
        v.version_for_repo(repo)


def test_no_repository_is_unknown(tmp_path):
    assert v.version_for_repo(tmp_path) == "0.0.0+unknown"


def test_a_directory_inside_someone_elses_repo_is_unknown(repo):
    # A source archive unpacked inside another checkout must not take that checkout's version.
    (repo / "unpacked").mkdir()
    assert v.version_for_repo(repo / "unpacked") == "0.0.0+unknown"


def test_watch_paths_cover_head_the_branch_and_the_tags(repo):
    git(repo, "tag", "-a", "v0.1.0", "-m", "0.1.0")
    paths = {os.path.relpath(p, repo / ".git") for p in v.watch_paths(repo)}
    assert {"HEAD", "refs/heads/main", "refs/tags"} <= paths
    assert all(os.path.exists(p) for p in v.watch_paths(repo))


def test_watch_paths_in_a_worktree_reach_the_common_dir(repo, tmp_path):
    wt = tmp_path / "wt"
    git(repo, "worktree", "add", "-q", "-b", "side", str(wt))
    paths = [str(p) for p in v.watch_paths(wt)]
    common = str((repo / ".git").resolve())
    assert any(p.endswith(os.path.join("worktrees", "wt", "HEAD")) for p in paths)
    assert os.path.join(common, "refs", "heads", "side") in paths


def test_stamp_staleness(tmp_path):
    stamp = tmp_path / "project_ver.txt"
    assert v.stamp_is_stale(stamp, "0.1.0")          # never configured with a stamp
    stamp.write_text("0.1.0")
    assert not v.stamp_is_stale(stamp, "0.1.0")
    assert v.stamp_is_stale(stamp, "0.1.1-dev.1+gabc1234")


# --- CLI ------------------------------------------------------------------------------------

def run_cli(*args):
    return subprocess.run([sys.executable, str(v.__file__), *args], env=GIT_ENV,
                          capture_output=True, text=True)


def test_cli_prints_the_version(repo):
    git(repo, "tag", "-a", "v0.1.0", "-m", "0.1.0")
    r = run_cli("--repo", str(repo))
    assert r.returncode == 0 and r.stdout == "0.1.0\n"


def test_cli_check_tag(repo):
    git(repo, "tag", "-a", "v0.1.0", "-m", "0.1.0")
    assert run_cli("--repo", str(repo), "--check-tag", "v0.1.0").returncode == 0
    assert run_cli("--repo", str(repo), "--check-tag", "0.1.0").returncode != 0
    assert run_cli("--repo", str(repo), "--check-tag", "v0.1.1").returncode != 0
    (repo / "f").write_text("edited")
    r = run_cli("--repo", str(repo), "--check-tag", "v0.1.0")
    assert r.returncode != 0 and "dirty" in r.stderr


def test_cli_cmake_mode_lists_the_version_then_the_paths(repo):
    r = run_cli("--repo", str(repo), "--cmake")
    lines = r.stdout.splitlines()
    assert r.returncode == 0 and lines[0] == f"0.0.0-dev.1+g{sha7(repo)}"
    assert all(os.path.exists(p) for p in lines[1:]) and len(lines) > 1


def test_cli_refuses_a_bad_tag_with_a_message(repo):
    git(repo, "tag", "-a", "v1.2", "-m", "bad")
    r = run_cli("--repo", str(repo))
    assert r.returncode != 0 and "v1.2" in r.stderr
