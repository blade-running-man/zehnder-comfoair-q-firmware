"""tools/make_release.py: the ESP Web Tools manifest and the checks that guard what it publishes.

Everything here is a pure function over bytes and dicts, so it runs on a bare interpreter with no
build. The layout tests are the reason the file exists: the one mistake that looks like a
simplification -- publishing the merged factory image -- erases the owner's settings on every
update, and only check_layout() stands between that and a green release.
"""
import json
import struct

import pytest

from conftest import _load

mr = _load("make_release")

# This tree's real layout (partitions.csv, flasher_args.json of the m5stack-nanoc6 build).
OFFSETS = {"bootloader": 0x0, "partitions": 0x8000, "otadata": 0xD000, "firmware": 0x10000}


def table_blob(rows):
    out = b""
    for label, ptype, sub, off, size in rows:
        out += b"\xaa\x50" + bytes([ptype, sub]) + struct.pack("<II", off, size)
        out += label.encode().ljust(16, b"\0") + b"\0" * 4
    return out + b"\xeb\xeb" + b"\xff" * 30


TABLE_ROWS = [("nvs", 1, 2, 0x9000, 0x4000), ("otadata", 1, 0, 0xD000, 0x2000),
              ("phy_init", 1, 1, 0xF000, 0x1000), ("ota_0", 0, 0x10, 0x10000, 0x1F0000),
              ("ota_1", 0, 0x11, 0x200000, 0x1F0000)]
TABLE = mr.parse_partition_table(table_blob(TABLE_ROWS))


def app_image(version=b"1.2.0", chip_id=0x000D):
    head = bytearray(b"\xe9" + b"\0" * 23)
    struct.pack_into("<H", head, 12, chip_id)
    seg = b"\0" * 8
    desc = struct.pack("<I", 0xABCD5432) + b"\0" * 12 + version.ljust(32, b"\0")
    desc += b"comfoair-q".ljust(32, b"\0")
    return bytes(head) + seg + desc + b"\0" * 256


def parts_sized(sizes):
    return [(name, OFFSETS[name], size) for name, size in sizes.items()]


REAL_SIZES = {"bootloader": 22336, "partitions": 3072, "otadata": 8192, "firmware": 1460608}


# --- manifest ----------------------------------------------------------------------------------

def test_manifest_has_the_esp_web_tools_fields_and_flash_order():
    parts = [("comfoair-q-1.2.0-firmware.bin", 0x10000), ("comfoair-q-1.2.0-bootloader.bin", 0),
             ("comfoair-q-1.2.0-otadata.bin", 0xD000), ("comfoair-q-1.2.0-partitions.bin", 0x8000)]
    m = mr.build_manifest("1.2.0", "ESP32-C6", parts)
    assert m["name"] == "ComfoAir Q"
    assert m["version"] == "1.2.0"
    assert m["new_install_prompt_erase"] is True
    assert m["new_install_improv_wait_time"] == 0
    assert len(m["builds"]) == 1 and m["builds"][0]["chipFamily"] == "ESP32-C6"
    offsets = [p["offset"] for p in m["builds"][0]["parts"]]
    assert offsets == [0x0, 0x8000, 0xD000, 0x10000]
    assert m["builds"][0]["parts"][0]["path"].endswith("-bootloader.bin")
    # Offsets are JSON numbers, not "0x8000" strings: ESP Web Tools adds them as integers.
    assert all(isinstance(o, int) for o in offsets)
    json.dumps(m)


# Every row of the versioning spec's table that is not a release: commits past the tag, a dirty
# tree (on the tag or past it), no tag at all, no git, and a release candidate.
UNRELEASED = ["0.1.1-dev.3+gabc1234", "0.1.1-dev.3+gabc1234.dirty", "0.1.1-dev.0+gabc1234.dirty",
              "0.0.0-dev.173+g74bb1a4", "0.0.0+unknown", "0.2.0-rc.1"]


@pytest.mark.parametrize("version", UNRELEASED)
def test_manifest_refuses_an_unreleased_version_unless_allowed(version):
    with pytest.raises(mr.ReleaseError, match="not a release"):
        mr.build_manifest(version, "ESP32-C6", [("x.bin", 0)])
    # Allowed, it is published as-is: the -dev / +g<sha> / .dirty parts ARE the marking.
    assert mr.build_manifest(version, "ESP32-C6", [("x.bin", 0)],
                             allow_unreleased=True)["version"] == version


def test_a_release_version_passes():
    mr.check_version("0.1.0")


@pytest.mark.parametrize("bad", ["", "../etc", "v1 2", "a/b", "x" * 40, "v1.2.0", "1a63e14-dirty",
                                 "99.99.99-dev.9999+g1234567.dirty"])
def test_version_must_be_semver_that_fits(bad):
    # Not even --allow-unreleased lets a non-SemVer string into a file name or the page.
    with pytest.raises(mr.ReleaseError):
        mr.check_version(bad, allow_unreleased=True)


@pytest.mark.parametrize("flag", ["--allow-unreleased", "--allow-dirty"])
def test_both_flag_spellings_allow_unreleased(flag):
    assert mr.make_parser().parse_args([flag]).allow_unreleased is True
    assert mr.make_parser().parse_args([]).allow_unreleased is False


def test_part_filename():
    assert mr.part_filename("1.2.0", "ota") == "comfoair-q-1.2.0-ota.bin"


# --- what is read from the build ---------------------------------------------------------------

def test_offsets_read_from_flasher_args():
    flasher = {"bootloader": {"offset": "0x0"}, "partition-table": {"offset": "0x8000"},
               "otadata": {"offset": "0xd000"}, "app": {"offset": "0x10000"}}
    assert mr.read_offsets(flasher) == {"bootloader": 0, "partition-table": 0x8000,
                                        "otadata": 0xD000, "app": 0x10000}
    del flasher["otadata"]
    with pytest.raises(mr.ReleaseError, match="otadata"):
        mr.read_offsets(flasher)


def test_partition_table_parsed_until_the_md5_entry():
    assert [r[0] for r in TABLE] == ["nvs", "otadata", "phy_init", "ota_0", "ota_1"]
    assert TABLE[0] == ("nvs", 1, 2, 0x9000, 0x4000)


def test_app_image_gives_chip_family_and_version():
    assert mr.app_image_info(app_image()) == ("ESP32-C6", "1.2.0")
    assert mr.app_image_info(app_image(chip_id=0x0005))[0] == "ESP32-C3"
    with pytest.raises(mr.ReleaseError, match="unknown chip"):
        mr.app_image_info(app_image(chip_id=0x0042))
    with pytest.raises(mr.ReleaseError, match="0xE9"):
        mr.app_image_info(b"\x1f\x8b" + app_image()[2:])  # a gzip, say


# --- layout: what an install must not touch -----------------------------------------------------

def test_the_real_four_part_layout_passes():
    mr.check_layout(parts_sized(REAL_SIZES), TABLE)


def test_a_merged_factory_image_is_refused_because_it_covers_nvs():
    # firmware.factory.bin: one file from 0x0 to the end of the app, 0xFF in the gaps.
    with pytest.raises(mr.ReleaseError, match="NVS"):
        mr.check_layout([("factory", 0x0, 0x10000 + REAL_SIZES["firmware"])], TABLE)


def test_a_bootloader_grown_into_nvs_is_refused():
    with pytest.raises(mr.ReleaseError, match="overlap"):
        mr.check_layout(parts_sized(dict(REAL_SIZES, bootloader=0x9000)), TABLE)


def test_otadata_offset_must_match_the_table():
    parts = [("otadata", 0xE000, 0x2000) if p[0] == "otadata" else p
             for p in parts_sized(REAL_SIZES)]
    with pytest.raises(mr.ReleaseError, match="otadata"):
        mr.check_layout(parts, TABLE)


def test_app_must_land_on_the_first_app_partition():
    parts = [("firmware", 0x200000, REAL_SIZES["firmware"]) if p[0] == "firmware" else p
             for p in parts_sized(REAL_SIZES)]
    with pytest.raises(mr.ReleaseError, match="first app partition"):
        mr.check_layout(parts, TABLE)


def test_a_table_without_nvs_is_refused_rather_than_guessed():
    with pytest.raises(mr.ReleaseError, match="no NVS"):
        mr.check_layout(parts_sized(REAL_SIZES), [r for r in TABLE if r[0] != "nvs"])


# --- page ---------------------------------------------------------------------------------------

def test_page_tokens_filled_and_policed():
    page = mr.render_page("<p>@VERSION@</p><a href='@OTA_BIN@'>", "v1", "comfoair-q-v1-ota.bin")
    assert page == "<p>v1</p><a href='comfoair-q-v1-ota.bin'>"
    with pytest.raises(mr.ReleaseError, match="@OTA_BIN@"):
        mr.render_page("@VERSION@", "v1", "x")
    with pytest.raises(mr.ReleaseError, match="unknown"):
        mr.render_page("@VERSION@ @OTA_BIN@ @BOARD@", "v1", "x")


def test_the_shipped_page_renders():
    page = (mr.ROOT / "installer" / "index.html").read_text()
    out = mr.render_page(page, "1.2.0", "comfoair-q-1.2.0-ota.bin")
    assert 'manifest="manifest.json"' in out
    assert "comfoair-q-1.2.0-ota.bin" in out
