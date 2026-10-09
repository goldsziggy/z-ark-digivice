#!/usr/bin/env python3
"""Package/preview the reviewed Waveshare development build. Never flash by default.

Only this reviewed binary set is supported. A future firmware release must update
the evidence and this allowlist after review; there is deliberately no bypass.
Uses Python's standard library and an existing official esptool 4.12.0 environment.
"""

import argparse
import glob
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shlex
import struct
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
BOARD = "waveshare-esp32-s3-touch-lcd-1.46-standard-glass-sku29565"
FIRMWARE_SOURCE = "6e058e18326b9a28f14b788a4e58c4c44bab5b75"
REVIEW_SOURCE = FIRMWARE_SOURCE
BUILD_EVIDENCE = "docs/evidence/esp-build-physical-playtest.json"
EMBEDDED_PROJECT_VERSION = "6e058e1"
SDK = {"version": "5.3.6", "commit": "79e3454c68248bd7d881721c9b2e94561378a8ce"}
ESPTOOL = "4.12.0"
FLASH_BYTES = 16 * 1024 * 1024
SETTINGS = {"flash_mode": "dio", "flash_size": "16MB", "flash_freq": "80m"}
FILES = {
    "bootloader": "bootloader/bootloader.bin",
    "partition-table": "partition_table/partition-table.bin",
    "otadata": "ota_data_initial.bin",
    "app": "digivice.bin",
}
# These are the measured waveshare artifacts in esp-build-physical-playtest.json, not offsets.
HASHES = {
    "bootloader": "3e2d70618d82d036333b9db42a6299d95bfc820f5c72adcc9aab1dd29e3474e9",
    "partition-table": "703550d3bc9af51a7cf8c96f486ba5acb13f81d2301035b3eae0da4f793d33f1",
    "otadata": "7d2c7ac4888bfd75cd5f56e8d61f69595121183afc81556c876732fd3782c62f",
    "app": "427e0f82dee8fa9b6c641081f75e2eae3a2d796456b7ab935c2d8d00c59087ec",
}
CONFIG_HASH = "d9b33f63d9f7d17ecdfc4ee0647f2dbd8c63cd25132cdeb330e86e8a99c9ea5d"


class Invalid(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise Invalid(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def safe_file(root, name):
    require(isinstance(name, str) and "\\" not in name, "Invalid file path")
    rel = PurePosixPath(name)
    require(name and not rel.is_absolute() and ".." not in rel.parts,
            "Absolute/traversing paths are forbidden")
    path = root / name
    cursor = path
    while cursor != root:
        require(not cursor.is_symlink(), "Symlinked package/build files are forbidden")
        cursor = cursor.parent
    require(not root.is_symlink(), "Symlinked package/build directories are forbidden")
    require(path.resolve().is_relative_to(root.resolve()), "File escapes directory")
    return path


def read_bounded(path, limit):
    require(path.is_file() and path.stat().st_size <= limit, f"Missing/oversized file: {path.name}")
    with path.open("rb") as stream:
        data = stream.read(limit + 1)
    require(len(data) <= limit, f"Oversized file: {path.name}")
    return data


def read_json(path):
    value = json.loads(read_bounded(path, 128 * 1024))
    require(isinstance(value, dict), f"Expected JSON object: {path.name}")
    return value


def partitions(data):
    """Read IDF's generated binary table, including its MD5 record."""
    rows = {}
    for pos in range(0, len(data), 32):
        record = data[pos:pos + 32]
        require(len(record) == 32, "Truncated partition table")
        if record[:2] == b"\xeb\xeb":
            require(record[16:] == hashlib.md5(data[:pos]).digest(), "Partition MD5 mismatch")
            return rows
        require(record[:2] == b"\xaa\x50", "Invalid partition record")
        _, kind, subtype, offset, size, label, flags = struct.unpack("<HBBII16sI", record)
        name = label.rstrip(b"\0").decode("ascii")
        require(name not in rows and size and offset + size <= FLASH_BYTES and flags == 0,
                "Invalid/duplicate/encrypted partition")
        require(offset % 4096 == 0 and size % 4096 == 0, "Unaligned partition")
        require(all(offset + size <= row[2] or offset >= row[2] + row[3] for row in rows.values()),
                "Overlapping partitions")
        rows[name] = (kind, subtype, offset, size)
    raise Invalid("Partition table has no MD5 record")


def validate_images(images, offsets, table_offset):
    require(set(images) == set(FILES) and set(offsets) == set(FILES), "Unexpected image set")
    for role, data in images.items():
        require(sha(data) == HASHES[role], f"Unreviewed/checksum-mismatched {role} binary")
    table = partitions(images["partition-table"])
    require(table["ota_0"][:2] == (0, 0x10) and table["otadata"][:2] == (1, 0),
            "Unsupported partition types")
    require(offsets == {"bootloader": 0, "partition-table": table_offset,
                        "otadata": table["otadata"][2], "app": table["ota_0"][2]},
            "Flash offsets disagree with the generated partition table")
    require(table_offset == min(row[2] for row in table.values()) - 4096,
            "Partition-table offset is inconsistent with layout")
    require(len(images["bootloader"]) <= table_offset and len(images["partition-table"]) <= 4096
            and len(images["app"]) <= table["ota_0"][3]
            and len(images["otadata"]) == table["otadata"][3], "Image exceeds its partition")
    regions = sorted((offsets[role], offsets[role] + ((len(data) + 4095) // 4096) * 4096)
                     for role, data in images.items())
    require(all(0 <= start < end <= FLASH_BYTES and start % 4096 == 0 for start, end in regions),
            "Invalid flash extent")
    require(all(a[1] <= b[0] for a, b in zip(regions, regions[1:])), "Overlapping flash extents")
    for role in ("app", "bootloader"):
        require(images[role][0] == 0xE9 and struct.unpack_from("<H", images[role], 12)[0] == 9,
                "Image is not ESP32-S3")
    app = images["app"]
    require(struct.unpack_from("<I", app, 32)[0] == 0xABCD5432
            and app[48:80].rstrip(b"\0") == EMBEDDED_PROJECT_VERSION.encode("ascii")
            and app[80:112].rstrip(b"\0") == b"digivice"
            and app[144:176].rstrip(b"\0") == ("v" + SDK["version"]).encode("ascii"),
            "Wrong application build descriptor")


def package(build, output):
    build = build.absolute()
    evidence = read_json(ROOT / BUILD_EVIDENCE)
    require(evidence["sdkIdentity"] == SDK, "Unreviewed SDK")
    require(evidence["firmwareSourceCommit"] == FIRMWARE_SOURCE
            and evidence["embeddedProjectVersion"] == EMBEDDED_PROJECT_VERSION,
            "Unreviewed firmware source/build descriptor")
    # The firmware source may be unchanged while guide/browser commits advance.
    for source in evidence["sourceFiles"]:
        require(sha(read_bounded(safe_file(ROOT, source["path"]), 4 * 1024 * 1024)) == source["sha256"],
                f"Source changed since reviewed build: {source['path']}")
    config = read_bounded(safe_file(build, "sdkconfig"), 128 * 1024)
    require(sha(config) == CONFIG_HASH == evidence["sdkconfigSha256"],
            "Wrong/unreviewed board profile or sdkconfig")
    require(b"CONFIG_DIGIVICE_BOARD_WAVESHARE_146=y\n" in config
            and b'CONFIG_IDF_TARGET="esp32s3"\n' in config, "Wrong board/chip")
    match = re.search(rb"^CONFIG_PARTITION_TABLE_OFFSET=(0x[0-9a-fA-F]+)$", config, re.M)
    require(match is not None, "Missing configured partition-table offset")
    table_offset = int(match[1], 16)
    args = read_json(safe_file(build, "flasher_args.json"))
    require(args["extra_esptool_args"] == {"after": "hard_reset", "before": "default_reset",
                                           "stub": True, "chip": "esp32s3"},
            "Unsupported chip/reset/encryption arguments")
    require(args["flash_settings"] == SETTINGS and args["write_flash_args"] ==
            ["--flash_mode", "dio", "--flash_size", "16MB", "--flash_freq", "80m"],
            "Unsupported flash settings")
    images, offsets = {}, {}
    for role, name in FILES.items():
        entry = args[role]
        require(entry["file"] == name and entry["encrypted"] == "false", "Wrong/encrypted image")
        offsets[role] = int(entry["offset"], 0)
        images[role] = read_bounded(safe_file(build, name), 3 * 1024 * 1024)
        measured = next(({"bytes": item["bytes"], "sha256": item["sha256"]}
                         for item in evidence["artifacts"]
                         if item["path"] == "firmware/build-waveshare/" + name), None)
        require(measured == {"bytes": len(images[role]), "sha256": sha(images[role])},
                f"Build does not match reviewed evidence: {name}")
    require({int(k, 0): v for k, v in args["flash_files"].items()} ==
            {offsets[role]: name for role, name in FILES.items()}, "Conflicting flash file map")
    validate_images(images, offsets, table_offset)
    manifest = {"formatVersion": 1, "developmentFirmware": True, "board": BOARD,
                "profile": "waveshare", "chip": "esp32s3", "sdk": SDK,
                "esptoolVersion": ESPTOOL, "firmwareSource": FIRMWARE_SOURCE,
                "reviewSource": REVIEW_SOURCE, "embeddedProjectVersion": EMBEDDED_PROJECT_VERSION,
                "sdkconfigSha256": CONFIG_HASH, "flashSettings": SETTINGS,
                "partitionTableOffset": table_offset,
                "images": {role: {"file": name, "offset": offsets[role],
                                  "bytes": len(images[role]), "sha256": sha(images[role])}
                           for role, name in FILES.items()}}
    output.mkdir(parents=False, exist_ok=False)
    for role, name in FILES.items():
        dest = output / name
        dest.parent.mkdir(exist_ok=True)
        dest.write_bytes(images[role])
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Verified development package: {output}\nFour binaries + manifest only; no private assets or credentials.")


def load_package(directory):
    directory = directory.absolute()
    manifest = read_json(safe_file(directory, "manifest.json"))
    expected = {"formatVersion": 1, "developmentFirmware": True, "board": BOARD,
                "profile": "waveshare", "chip": "esp32s3", "sdk": SDK,
                "esptoolVersion": ESPTOOL, "firmwareSource": FIRMWARE_SOURCE,
                "reviewSource": REVIEW_SOURCE, "sdkconfigSha256": CONFIG_HASH,
                "embeddedProjectVersion": EMBEDDED_PROJECT_VERSION,
                "flashSettings": SETTINGS}
    require(all(manifest.get(k) == v for k, v in expected.items()), "Unsupported board/build manifest")
    require(set(manifest["images"]) == set(FILES), "Unexpected image set")
    images, offsets = {}, {}
    for role, name in FILES.items():
        entry = manifest["images"][role]
        path = safe_file(directory, entry["file"])
        require(entry["file"] == name, "Unexpected image path")
        images[role] = read_bounded(path, 3 * 1024 * 1024)
        require(type(entry["offset"]) is int and entry["bytes"] == len(images[role])
                and entry["sha256"] == sha(images[role]), f"Manifest integrity mismatch: {role}")
        offsets[role] = entry["offset"]
    validate_images(images, offsets, manifest["partitionTableOffset"])
    return images, offsets


def command(python, port, directory, offsets, roles):
    args = [python, "-I", "-m", "esptool", "--chip", "esp32s3", "--port", port,
            "--baud", "115200", "--before", "default_reset", "--after", "hard_reset",
            "write_flash", "--flash_mode", SETTINGS["flash_mode"],
            "--flash_freq", SETTINGS["flash_freq"], "--flash_size", SETTINGS["flash_size"]]
    for role in sorted(roles, key=lambda name: offsets[name]):
        args.extend([hex(offsets[role]), str(directory / FILES[role])])
    return args


def flash(args):
    require(bool(re.fullmatch(r"/dev/(?:cu\.[A-Za-z0-9._-]+|tty(?:USB|ACM)[0-9]+)", args.port)),
            "Use an explicit local /dev/cu.* (macOS) or /dev/ttyUSB*/ttyACM* port")
    images, offsets = load_package(args.package)
    roles = list(FILES) if args.initial_install else ["app"]
    print("DEVELOPMENT firmware: native walking, saved care, sprite battles and friendly Nearby; physical acceptance remains a separate check.")
    if args.initial_install:
        print("Initial install replaces bootloader + partition table + OTA selection + app.\n"
              "OTA selection resets to ota_0. Existing NVS bytes are not erased; unrelated layouts are not migrated.")
    else:
        print("App-only upgrade: requires this exact existing layout and ota_0 selected.\n"
              "NVS and OTA selection are untouched; the helper cannot verify the attached board/layout.")
    cmd = command(args.python, args.port, args.package.absolute(), offsets, roles)
    if not args.execute:
        print("DRY RUN: no serial port opened. Proposed command:\n" + shlex.join(cmd))
        return
    require(args.acknowledge_boot_selection_reset if args.initial_install else args.acknowledge_existing_layout,
            "Execution requires --acknowledge-boot-selection-reset for initial install, "
            "or --acknowledge-existing-layout for app-only upgrade")
    result = subprocess.run([args.python, "-I", "-m", "esptool", "version"],
                            check=True, capture_output=True, text=True, timeout=15)
    require(result.stdout.strip().splitlines()[-1:] == [ESPTOOL],
            f"Use the official existing esptool {ESPTOOL} environment; nothing was flashed")
    # Execute from a validated in-memory snapshot, not mutable user package paths.
    with tempfile.TemporaryDirectory(prefix="digivice-flash-") as temporary:
        directory = Path(temporary)
        for role in roles:
            dest = directory / FILES[role]
            dest.parent.mkdir(exist_ok=True)
            dest.write_bytes(images[role])
        print("Executing official esptool from a verified temporary binary snapshot.", flush=True)
        subprocess.run(command(args.python, args.port, directory, offsets, roles), check=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    sub.add_parser("ports", help="List serial path names without opening ports (macOS/Linux)")
    pack = sub.add_parser("package", help="Copy only the reviewed waveshare build into a NEW directory")
    pack.add_argument("--build", type=Path, default=ROOT / "firmware/build-waveshare")
    pack.add_argument("--output", type=Path, required=True)
    write = sub.add_parser("flash", help="Validate and show a flash command; dry run is the default")
    write.add_argument("--package", type=Path, required=True)
    write.add_argument("--port", required=True)
    write.add_argument("--python", default=sys.executable, help="Existing official esptool 4.12.0 Python")
    mode = write.add_mutually_exclusive_group()
    mode.add_argument("--execute", action="store_true", help="Actually open the selected port and flash")
    mode.add_argument("--dry-run", action="store_true", help="Explicitly select the safe default")
    write.add_argument("--initial-install", action="store_true")
    write.add_argument("--acknowledge-boot-selection-reset", action="store_true")
    write.add_argument("--acknowledge-existing-layout", action="store_true")
    args = parser.parse_args(argv)
    try:
        if args.action == "ports":
            found = sorted(set(path for pattern in ("/dev/cu.*", "/dev/ttyUSB*", "/dev/ttyACM*")
                               for path in glob.glob(pattern)))
            print("\n".join(found) if found else "No serial paths found. No ports were opened.")
        elif args.action == "package":
            package(args.build, args.output)
        else:
            flash(args)
        return 0
    except (Invalid, OSError, KeyError, TypeError, ValueError, subprocess.SubprocessError) as error:
        # Never dump the process environment, serial input, or external stderr.
        print(f"Stopped: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
