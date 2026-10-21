#!/usr/bin/env python3
"""Assemble dist/: the ESP Web Tools install site and the OTA file, from one board build.

Run after `pio run -e m5stack-nanoc6`. Everything the manifest says is READ from that build,
not written here by hand, because each hand-written copy is a value that can drift from the
image it describes:

* offsets      -- flasher_args.json, which ESP-IDF writes from the same sdkconfig and
                  partitions.csv the bootloader was built against; then cross-checked against
                  partitions.bin, which is the table that actually lands on the flash;
* chipFamily   -- the chip_id in the app image header (esp_image_header_t bytes 12-13);
* version      -- esp_app_desc_t.version inside firmware.bin, which is exactly the string the
                  running firmware reports in /api/system. CMakeLists.txt sets PROJECT_VER from
                  tools/version.py (SemVer from the v* tag), so on a clean tagged commit it is the
                  tag without its `v`. Only such a release version is published
                  (version.is_release()); anything else needs --allow-unreleased and is
                  published as-is, so its -dev/+g<sha>/.dirty parts mark it on the page and in
                  the manifest.

DO NOT publish firmware.factory.bin instead of the four parts. esptool's merge-bin pads every
gap between parts with 0xFF, so that image starts at 0x0 and runs through 0x9000-0xD000: writing
it erases NVS -- the owner's Wi-Fi, the web password, the broker -- on EVERY install, including
an update. Four separate parts leave NVS alone; a clean install is ESP Web Tools' own "Erase
device" checkbox (new_install_prompt_erase), which the person ticks on purpose. check_layout()
refuses any part set that overlaps NVS, so a later "simplify to one file" fails here.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import struct
import sys
from pathlib import Path
from typing import Dict, List, Sequence, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent))
import version as semver  # noqa: E402  -- tools/version.py, the one source of the version

ROOT = Path(__file__).resolve().parents[1]
PROJECT = "comfoair-q"
NAME = "ComfoAir Q"

# flasher_args.json key -> (part name in the published file name, file PlatformIO writes).
# flasher_args.json names the IDF-native paths (comfoair-q.bin, partition_table/...), which
# PlatformIO does not produce; it writes these four at the top of the build directory instead.
# Only the OFFSETS are taken from flasher_args.json.
PARTS = (
    ("bootloader", "bootloader", "bootloader.bin"),
    ("partition-table", "partitions", "partitions.bin"),
    ("otadata", "otadata", "ota_data_initial.bin"),
    ("app", "firmware", "firmware.bin"),
)

# esp_chip_id_t (esp_app_format.h) -> ESP Web Tools chipFamily (its const.d.ts). Only chips this
# tree could plausibly build for; an unknown id is refused rather than guessed.
CHIP_FAMILY = {0x0000: "ESP32", 0x0002: "ESP32-S2", 0x0005: "ESP32-C3", 0x0009: "ESP32-S3",
               0x000C: "ESP32-C2", 0x000D: "ESP32-C6", 0x0010: "ESP32-H2"}

class ReleaseError(Exception):
    """A build that must not be published, with the reason a person can act on."""


def read_offsets(flasher_args: dict) -> Dict[str, int]:
    """Offsets of the four parts, keyed by flasher_args.json key."""
    out = {}
    for key, _, _ in PARTS:
        try:
            out[key] = int(flasher_args[key]["offset"], 16)
        except (KeyError, TypeError, ValueError) as e:
            raise ReleaseError(f"flasher_args.json has no usable '{key}.offset' ({e!r})") from e
    return out


def parse_partition_table(blob: bytes) -> List[Tuple[str, int, int, int, int]]:
    """(label, type, subtype, offset, size) per entry of a binary partition table.

    32-byte entries starting with magic 0xAA 0x50 (esp_partition_info_t); the list ends at the
    first entry without it (the MD5 entry 0xEB 0xEB, or 0xFF padding)."""
    rows = []
    for i in range(0, len(blob) - 31, 32):
        e = blob[i:i + 32]
        if e[:2] != b"\xaa\x50":
            break
        offset, size = struct.unpack_from("<II", e, 4)
        label = e[12:28].split(b"\0", 1)[0].decode("ascii", "replace")
        rows.append((label, e[2], e[3], offset, size))
    if not rows:
        raise ReleaseError("partitions.bin holds no partition entries")
    return rows


def app_image_info(image: bytes) -> Tuple[str, str]:
    """(chipFamily, version) read from an app image.

    esp_image_header_t is 24 bytes (magic 0xE9 at 0, chip_id LE u16 at 12), then the first
    segment header (8 bytes), then esp_app_desc_t: magic 0xABCD5432 at +0, version[32] at +16.
    That fixed position is how esp_app_get_description() itself finds it on the device."""
    if len(image) < 0x50 or image[0] != 0xE9:
        raise ReleaseError("firmware.bin is not an ESP app image (byte 0 is not 0xE9)")
    (chip_id,) = struct.unpack_from("<H", image, 12)
    if chip_id not in CHIP_FAMILY:
        raise ReleaseError(f"firmware.bin is built for unknown chip id 0x{chip_id:04x}")
    (desc_magic,) = struct.unpack_from("<I", image, 32)
    if desc_magic != 0xABCD5432:
        raise ReleaseError("firmware.bin carries no esp_app_desc_t where ESP-IDF puts it")
    version = image[48:80].split(b"\0", 1)[0].decode("ascii", "replace")
    return CHIP_FAMILY[chip_id], version


def check_layout(parts: Sequence[Tuple[str, int, int]],
                 table: Sequence[Tuple[str, int, int, int, int]]) -> None:
    """Refuse a part set that would damage what an update must keep, or disagree with the table.

    `parts` is (name, offset, size). The table is the one being flashed, so it is the authority
    on where NVS and the slots are."""
    nvs = [(r[3], r[3] + r[4]) for r in table if r[1] == 1 and r[2] == 2]  # data/nvs
    if not nvs:
        raise ReleaseError("partition table has no NVS partition; refusing to guess where it is")
    spans = sorted((off, off + size, name) for name, off, size in parts)
    for (_, a1, an), (b0, _, bn) in zip(spans, spans[1:]):
        if b0 < a1:
            raise ReleaseError(f"parts overlap: {an} ends at 0x{a1:x}, {bn} starts at 0x{b0:x}")
    for n0, n1 in nvs:
        for p0, p1, name in spans:
            if p0 < n1 and n0 < p1:
                raise ReleaseError(
                    f"{name} [0x{p0:x}, 0x{p1:x}) overlaps NVS [0x{n0:x}, 0x{n1:x}): "
                    "installing it would erase the device's settings on every update")
    offsets = {name: off for name, off, _ in parts}
    otadata = [r[3] for r in table if r[1] == 1 and r[2] == 0]  # data/ota
    if "otadata" in offsets and otadata[:1] != [offsets["otadata"]]:
        raise ReleaseError("otadata offset in flasher_args.json disagrees with partitions.bin")
    # The bootloader boots the first app partition when otadata is blank, which is what
    # ota_data_initial.bin is -- so the app part must land exactly there.
    first_app = min((r[3] for r in table if r[1] == 0), default=None)
    if "firmware" in offsets and offsets["firmware"] != first_app:
        raise ReleaseError(f"app offset 0x{offsets['firmware']:x} is not the first app partition "
                           f"in partitions.bin (0x{first_app or 0:x})")


def check_version(version: str, allow_unreleased: bool = False) -> None:
    """Refuse a version that is not SemVer, or not a release unless explicitly allowed.

    SemVer's alphabet ([0-9A-Za-z.+-]) is also what keeps the version safe inside every file
    name and the page. A published image has to be reproducible from its tag: a pre-release
    (-dev.N: commits past the tag) or build metadata (+g<sha>, .dirty: uncommitted changes) is
    a build nobody -- the owner in six months included -- can rebuild from a release."""
    try:
        semver.validate(version)
    except semver.VersionError as e:
        raise ReleaseError(str(e)) from None
    if not semver.is_release(version) and not allow_unreleased:
        raise ReleaseError(f"version {version!r} is not a release (it has a pre-release or "
                           "build metadata: commits past the tag, or a dirty tree); build "
                           "exactly on a v* tag from a clean tree, or pass --allow-unreleased "
                           "for a local test that will not be published")


def part_filename(version: str, part: str) -> str:
    return f"{PROJECT}-{version}-{part}.bin"


def build_manifest(version: str, chip_family: str, parts: Sequence[Tuple[str, int]],
                   allow_unreleased: bool = False) -> dict:
    """The ESP Web Tools manifest. `parts` is (path, offset); written in flash order.

    new_install_prompt_erase: without Improv every install is a "new install" to ESP Web Tools,
    and with this set it asks, checkbox unticked by default -- so an update keeps NVS and a clean
    install is one deliberate tick. Left false, ESP Web Tools ERASES a device without Improv
    unasked (install-dialog.js: "Default is to erase a device that does not support Improv").
    new_install_improv_wait_time 0: the firmware does not speak Improv Serial, so the dialog
    neither probes before the install nor waits 10 s for an answer after it."""
    check_version(version, allow_unreleased)
    ordered = sorted(parts, key=lambda p: p[1])
    return {
        "name": NAME,
        "version": version,
        "new_install_prompt_erase": True,
        "new_install_improv_wait_time": 0,
        "builds": [{
            "chipFamily": chip_family,
            "parts": [{"path": path, "offset": offset} for path, offset in ordered],
        }],
    }


def render_page(template: str, version: str, ota_file: str) -> str:
    """Fill the page's two tokens; refuse a page that lost one or grew an unknown one."""
    for token in ("@VERSION@", "@OTA_BIN@"):
        if token not in template:
            raise ReleaseError(f"installer page has no {token} token")
    page = template.replace("@VERSION@", version).replace("@OTA_BIN@", ota_file)
    left = re.findall(r"@[A-Z_]+@", page)
    if left:
        raise ReleaseError(f"installer page has unknown tokens: {sorted(set(left))}")
    return page


def tree_version() -> str:
    """The version this checkout would build now, or "" when it cannot be told."""
    try:
        return semver.version_for_repo(ROOT)
    except semver.VersionError:
        return ""


def assemble(build: Path, installer: Path, out: Path, allow_unreleased: bool) -> List[Path]:
    flasher = json.loads((build / "flasher_args.json").read_text())
    offsets = read_offsets(flasher)
    blobs = {}
    for key, part, filename in PARTS:
        path = build / filename
        if not path.is_file():
            raise ReleaseError(f"{path} is missing; run `pio run -e m5stack-nanoc6` first")
        blobs[part] = (offsets[key], path.read_bytes())
    chip_family, version = app_image_info(blobs["firmware"][1])
    check_layout([(p, off, len(b)) for p, (off, b) in blobs.items()],
                 parse_partition_table(blobs["partitions"][1]))
    check_version(version, allow_unreleased)
    tree = tree_version()
    if tree and tree != version:
        print(f"warning: the image says {version!r} but the tree is {tree!r}; the image is what "
              "gets published", file=sys.stderr)

    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    written = []
    for part, (offset, blob) in blobs.items():
        (out / part_filename(version, part)).write_bytes(blob)
        written.append(out / part_filename(version, part))
    manifest = build_manifest(version, chip_family,
                              [(part_filename(version, p), off) for p, (off, _) in blobs.items()],
                              allow_unreleased)
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    ota = out / part_filename(version, "ota")
    ota.write_bytes(blobs["firmware"][1])
    written += [out / "manifest.json", ota]
    for src in sorted(installer.iterdir()):
        if src.is_file():
            dst = out / src.name
            if src.suffix == ".html":
                dst.write_text(render_page(src.read_text(), version, ota.name))
            else:
                shutil.copyfile(src, dst)
            written.append(dst)
    return written


def make_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--build-dir", type=Path, default=ROOT / ".pio/build/m5stack-nanoc6")
    ap.add_argument("--installer", type=Path, default=ROOT / "installer")
    ap.add_argument("--out", type=Path, default=ROOT / "dist")
    # --allow-dirty is the flag's old name, kept so existing notes and muscle memory still work;
    # it now covers every unreleased version, not only a dirty one.
    ap.add_argument("--allow-unreleased", "--allow-dirty", dest="allow_unreleased",
                    action="store_true",
                    help="accept a pre-release, dev or dirty image (local testing only; "
                         "never publish it)")
    return ap


def main(argv: Sequence[str] = None) -> int:
    args = make_parser().parse_args(argv)
    try:
        written = assemble(args.build_dir, args.installer, args.out, args.allow_unreleased)
    except ReleaseError as e:
        print(f"make_release: {e}", file=sys.stderr)
        return 1
    print(json.dumps(json.loads((args.out / "manifest.json").read_text()), indent=2))
    for path in written:
        print(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.relative_to(args.out)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
