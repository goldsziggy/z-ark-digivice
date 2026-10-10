#!/usr/bin/env python3
"""Deploy firmware and SD sprites to connected Digivice boards without touching saves.

Subcommands (see docs/DEPLOYMENT.md):
  boards                         list connected boards by MAC; no reset, no serial commands
  status   --mac MAC [--watch S] read-only `device status`; --watch prints touch-counter changes
  saves    --nvs FILE [--against SUMMARY.json]
                                 decode a save from an NVS backup with the host CLI; --against
                                 fails when anything other than schema/rules differs
  firmware --mac MAC [--mac MAC ...] [--app BIN] [--execute]
                                 per board: back up NVS + app slot, check the partition table,
                                 write the app only at 0x20000, read it back, boot, compare saves
  sprites  --mac MAC --dir DIR [--forms A-B] [--execute]
                                 push DSFnnnnn.DVA sprites to the board's SD card over USB

Every write needs --execute; without it each command is a dry run. The tool never erases,
never writes the bootloader, partition table, OTA data or NVS, and never copies a save
between boards. Backups go outside the repository (default ../device-backups).
"""
import argparse
import base64
import datetime
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import time
import zlib

ROOT = Path(__file__).resolve().parents[1]
TOOLCHAINS = Path(os.environ.get("DIGIVICE_TOOLCHAINS", ROOT.parent / ".toolchains")).resolve()
BACKUPS = Path(os.environ.get("DIGIVICE_BACKUPS", ROOT.parent / "device-backups")).resolve()
VENV_PYTHON = TOOLCHAINS / "esptool-venv" / "bin" / "python"
ESPTOOL = TOOLCHAINS / "esptool-venv" / "bin" / "esptool"
CLI = ROOT / "build" / "digivice-core"
BUILD = ROOT / "firmware" / "build-waveshare"
APP_ADDRESS, APP_SLOT = 0x20000, 0x300000
NVS_ADDRESS, NVS_BYTES = 0x9000, 0x10000
PARTITION_ADDRESS = 0x8000
PROMPT = b"digivice>"


def fail(message):
    raise SystemExit(f"STOP: {message}")


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def load_installer():
    spec = importlib.util.spec_from_file_location("install_sd_usb", ROOT / "scripts" / "install-sd-usb.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


installer = load_installer()


# --- boards -----------------------------------------------------------------

def connected():
    """MAC -> port from USB enumeration. The ESP32-S3 USB serial number is its MAC."""
    from serial.tools import list_ports
    boards = {}
    for item in list_ports.comports():
        try:
            mac = installer.mac(item.serial_number)
        except installer.Invalid:
            continue
        if item.device.startswith("/dev/tty."):
            continue  # macOS lists each board twice; cu.* never waits for carrier detect.
        boards[mac] = item.device
    return boards


def port_for(mac):
    boards = connected()
    if mac not in boards:
        fail(f"board {mac} is not connected (connected: {', '.join(boards) or 'none'})")
    return boards[mac]


# --- esptool ----------------------------------------------------------------

def esptool(port, *args, before="default-reset", after="no-reset", baud=921600):
    command = [str(ESPTOOL), "--chip", "esp32s3", "-p", port, "-b", str(baud),
               "--before", before, "--after", after, *map(str, args)]
    if any("erase" in part.lower() for part in command):
        fail("refusing an esptool command that mentions erase")
    print("  $ esptool " + " ".join(map(str, args)), flush=True)
    result = subprocess.run(command, text=True, capture_output=True)
    if result.returncode:
        print(result.stdout[-2000:], result.stderr[-2000:], sep="\n")
        fail(f"esptool failed ({result.returncode})")
    return result.stdout + result.stderr


def read_flash(port, address, size, path, before="default-reset"):
    esptool(port, "read-flash", hex(address), size, path, before=before)
    return path.read_bytes()


def boot_log(port, seconds=15):
    """Hard-reset into the app and capture its boot output until the console prompt."""
    import serial
    esptool(port, "chip-id", before="no-reset", after="hard-reset", baud=115200)
    deadline = time.time() + 8
    while not Path(port).exists() and time.time() < deadline:
        time.sleep(0.05)
    time.sleep(0.3)
    handle = serial.Serial()
    handle.port, handle.baudrate, handle.timeout = port, 115200, 0.15
    handle.dsrdtr = handle.rtscts = False
    handle.dtr = handle.rts = False
    handle.open()
    try:
        text, end = b"", time.time() + seconds
        while time.time() < end and PROMPT not in text:
            text += handle.read(4096)
        settle = time.time() + 2
        while time.time() < settle:
            text += handle.read(4096)
    finally:
        handle.close()
    return text


# --- saves ------------------------------------------------------------------

def save_chunks(blob):
    """Locate save_a/save_b blob chunks in an ESP-IDF NVS partition image."""
    page_bytes, entry_bytes, entries, written = 4096, 32, 126, 2
    found = []
    for page_index in range(len(blob) // page_bytes):
        page = blob[page_index * page_bytes:(page_index + 1) * page_bytes]
        index = 0
        while index < entries:
            bit = index * 2
            if (page[32 + bit // 8] >> (bit % 8)) & 3 != written:
                index += 1
                continue
            offset = 64 + index * entry_bytes
            raw = page[offset:offset + entry_bytes]
            span, key = raw[2], raw[8:24].split(b"\x00", 1)[0]
            if span < 1 or index + span > entries:
                index += 1
                continue
            if key in (b"save_a", b"save_b") and raw[1] == 0x42:
                found.append({"key": key.decode(), "version": raw[3] & 0x80, "index": raw[3] & 0x7F,
                              "size": struct.unpack_from("<H", raw, 24)[0],
                              "at": page_index * page_bytes + offset + entry_bytes})
            index += span
    return found


def newest_snapshot(blob):
    groups = {}
    for chunk in save_chunks(blob):
        groups.setdefault((chunk["key"], chunk["version"]), []).append(chunk)
    best = None
    for parts in groups.values():
        data = b"".join(blob[c["at"]:c["at"] + c["size"]] for c in sorted(parts, key=lambda c: c["index"]))
        if len(data) < 16 or data[:4] != b"DGVS":
            continue
        total = struct.unpack_from("<H", data, 6)[0] + 12
        if total != len(data) or zlib.crc32(data[:-4]) != struct.unpack_from("<I", data, total - 4)[0]:
            continue
        sequence = struct.unpack_from("<I", data, 12)[0]
        if not best or sequence > best[0]:
            best = (sequence, data)
    if not best:
        fail("no valid save snapshot in this NVS image")
    return best[1]


def decode(snapshot):
    if not CLI.exists():
        fail(f"host CLI missing: build it first (cmake --build build) -> {CLI}")
    result = subprocess.run([str(CLI), "--replay-snapshot", base64.b64encode(snapshot).decode()],
                            input="", capture_output=True, text=True)  # The CLI also reads stdin.
    if result.returncode:
        fail("host CLI could not decode the save: " + (result.stderr or result.stdout)[:400])
    state = json.loads(result.stdout)
    collection = state.get("collection") or []
    return {
        "schema": state.get("schemaVersion"), "rules": state.get("rulesVersion"),
        "sequence": state.get("sequence"), "phase": state.get("phase"), "mode": state.get("battleMode"),
        "level": state.get("level"), "bond": state.get("bond"), "captures": state.get("captures"),
        "active": state.get("activeCreatureId"), "party": state.get("partyMemberIds"),
        "count": len(collection),
        "members": [[m.get("id"), m.get("formId"), m.get("name"), m.get("level")] for m in collection],
    }


def save_differences(before, after, ignore=("schema", "rules")):
    return {key: [before.get(key), after.get(key)] for key in sorted(set(before) | set(after))
            if key not in ignore and before.get(key) != after.get(key)}


# --- sprites ----------------------------------------------------------------

def check_dva(name, data):
    """The firmware's DVA1 acceptance rules (firmware/runtime/sprite.cpp)."""
    problem = None
    if len(data) < 112 or len(data) > 128 * 1024 or data[:4] != b"DVA1":
        problem = "not a DVA1 file within 128 KiB"
    else:
        version, header = struct.unpack_from("<HH", data, 4)
        width, height = struct.unpack_from("<HH", data, 12)
        frames, packed = struct.unpack_from("<HH", data, 20)
        payload, crc = struct.unpack_from("<II", data, 24)
        if (version, header, data[16], data[17], data[18], data[19]) != (1, 32, 16, 6, 0, 0):
            problem = "unsupported header"
        elif width not in (16, 32) or height != width or not 6 <= frames <= 48 or packed != width * height // 2:
            problem = "invalid size or frame count"
        elif payload != 80 + frames * packed or len(data) != 32 + payload or zlib.crc32(data[32:]) != crc:
            problem = "length or CRC mismatch"
        else:
            expected = 0
            for clip in range(6):
                record = data[64 + clip * 8:72 + clip * 8]
                count, ms, first = record[1], struct.unpack_from("<H", record, 2)[0], struct.unpack_from("<H", record, 4)[0]
                if record[0] != clip or not 1 <= count <= 8 or not 40 <= ms <= 2000 or first != expected:
                    problem = "invalid animation table"
                    break
                expected += count
            if not problem and expected != frames:
                problem = "animation table does not cover every frame"
    if problem:
        fail(f"{name}: {problem}; the firmware would reject it")


def sprite_assets(directory, forms):
    names = sorted(p.name for p in directory.iterdir() if re.fullmatch(r"DSF\d{5}\.DVA", p.name))
    if forms:
        low, high = forms
        names = [n for n in names if low <= int(n[3:8]) <= high]
    names = [n for n in names if 1 <= int(n[3:8]) <= 512]
    if not names:
        fail("no DSFnnnnn.DVA files selected")
    assets = []
    for name in names:
        data = installer.file_bytes(directory / name, 128 * 1024)
        check_dva(name, data)
        assets.append(installer.Asset(name, data, sha256(data)))
    return assets


# --- commands ---------------------------------------------------------------

def command_boards(_):
    boards = connected()
    if not boards:
        print("No Digivice boards enumerated. Check the USB-C data cable and that the board is powered.")
    for mac, port in sorted(boards.items()):
        print(f"{mac}  {port}")


def open_console(mac):
    port = port_for(mac)
    connection = installer.open_serial(port)
    client = installer.SerialProtocol(connection)
    identity = installer.identify(client, mac)
    print(f"{mac} on {port}: firmware {identity['firmware']}", flush=True)
    return connection


def command_status(args):
    mac = installer.mac(args.mac)
    connection = open_console(mac)
    try:
        start, previous, pending = time.time(), None, b""
        while True:
            connection.write(b"device status\n")
            lines, deadline = [], time.time() + 1.5
            while time.time() < deadline:
                pending += connection.read(4096)
                *complete, pending = pending.split(b"\n")
                lines += [l.decode("utf-8", "replace").strip().removeprefix("digivice>").strip() for l in complete]
                if any(l.startswith("device mainStack") for l in lines):
                    break
            if not args.watch:
                print("\n".join(l for l in lines if l))
                return
            touch = next((l for l in lines if l.startswith("device touch ")), None)
            if touch and touch != previous:
                print(f"{time.time() - start:6.1f}s {touch}", flush=True)
                previous = touch
            if time.time() - start >= args.watch:
                return
            time.sleep(0.4)
    finally:
        connection.close()


def command_saves(args):
    summary = decode(newest_snapshot(Path(args.nvs).read_bytes()))
    print(json.dumps(summary, indent=2))
    if args.against:
        differences = save_differences(json.loads(Path(args.against).read_text()), summary)
        if differences:
            print("DIFFERENCES beyond schema/rules:", json.dumps(differences, indent=2))
            raise SystemExit(1)
        print("OK: only schema/rules may differ.")


def built_app(path):
    app = Path(path).read_bytes()
    if not app or app[0] != 0xE9 or len(app) > APP_SLOT:
        fail(f"{path} is not an ESP application image that fits the 3 MiB slot")
    description = BUILD / "project_description.json"
    version = json.loads(description.read_text()).get("project_version") if description.exists() else None
    if not version or version.endswith("-dirty"):
        fail(f"build a clean tree first (project_version={version!r}); see docs/DEPLOYMENT.md")
    table = (BUILD / "partition_table" / "partition-table.bin").read_bytes()
    return app, version, table


def deploy_board(mac, app, version, table, args):
    port = port_for(mac)
    stamp = datetime.date.today().isoformat()
    folder = BACKUPS / f"{stamp}-{version}" / mac.replace(":", "-")
    if (folder / "nvs-before-flash.bin").exists():
        folder = folder.with_name(folder.name + "-" + datetime.datetime.now().strftime("%H%M%S"))
    folder.mkdir(parents=True, exist_ok=True)
    print(f"\n== {mac} on {port} -> backups in {folder}", flush=True)
    record = {"mac": mac, "version": version, "appSha256": sha256(app), "appBytes": len(app)}

    nvs = read_flash(port, NVS_ADDRESS, NVS_BYTES, folder / "nvs-before-flash.bin")
    installed_table = read_flash(port, PARTITION_ADDRESS, len(table), folder / "partition-table.bin", before="no-reset")
    if installed_table != table:
        fail("the board's partition table differs from this build; an app-only update is not valid. Stop and review.")
    before = decode(newest_snapshot(nvs))
    (folder / "save-before.json").write_text(json.dumps(before, indent=2))
    print(f"  save: schema {before['schema']} rules {before['rules']} sequence {before['sequence']} "
          f"phase {before['phase']} members {before['count']}", flush=True)

    installed = read_flash(port, APP_ADDRESS, len(app), folder / "app-before.bin", before="no-reset")
    if installed == app:
        print("  app already installed; no write", flush=True)
        record["written"] = False
    else:
        esptool(port, "write-flash", "--flash-mode", "dio", "--flash-freq", "80m", "--flash-size", "16MB",
                hex(APP_ADDRESS), args.app, baud=460800)
        if read_flash(port, APP_ADDRESS, len(app), folder / "app-readback.bin", before="no-reset") != app:
            fail("application readback does not match; do not boot. Re-run the write after checking the cable.")
        print("  app written and read back", flush=True)
        record["written"] = True

    log = boot_log(port)
    (folder / "boot.log").write_bytes(log)
    text = log.decode("utf-8", "replace")
    record["booted"] = PROMPT in log and f"App version:      {version}" in text
    record["recovery"] = "RECOVERY" in text
    record["sd"] = next((l.strip() for l in text.splitlines() if l.startswith("SD asset cache")), "no SD line")
    if not record["booted"] or record["recovery"]:
        fail(f"{mac} did not reach the app prompt on {version} (see {folder / 'boot.log'}). Do not reflash blindly.")
    print(f"  booted {version}; {record['sd']}", flush=True)

    after_nvs = read_flash(port, NVS_ADDRESS, NVS_BYTES, folder / "nvs-after-boot.bin")
    after = decode(newest_snapshot(after_nvs))
    (folder / "save-after.json").write_text(json.dumps(after, indent=2))
    record["nvsUnchangedByBoot"] = after_nvs == nvs
    record["saveDifferences"] = save_differences(before, after, ignore=("schema", "rules", "sequence"))
    left = boot_log(port)
    record["leftRunning"] = PROMPT in left
    (folder / "deploy.json").write_text(json.dumps(record, indent=2))
    print(f"  NVS unchanged by boot: {record['nvsUnchangedByBoot']}; save differences: "
          f"{record['saveDifferences'] or 'none'}; left running: {record['leftRunning']}", flush=True)
    if record["saveDifferences"]:
        fail(f"{mac}'s save changed beyond schema/rules/sequence; backups are in {folder}. Review before continuing.")


def command_firmware(args):
    app, version, table = built_app(args.app)
    macs = [installer.mac(m) for m in args.mac]
    ports = {mac: port_for(mac) for mac in macs}
    if BACKUPS == ROOT or ROOT in BACKUPS.parents:
        fail("backups must live outside the repository (set DIGIVICE_BACKUPS)")
    print(f"App {version}: {len(app):,} bytes, sha256 {sha256(app)}, {APP_SLOT - len(app):,} bytes free in the slot")
    for mac, port in ports.items():
        print(f"  {mac} on {port}")
    if not args.execute:
        print("DRY RUN: nothing was read or written. Re-run with --execute to deploy.")
        return
    for mac in macs:
        deploy_board(mac, app, version, table, args)
    print("\nDEPLOY OK")


def command_sprites(args):
    forms = None
    if args.forms:
        low, _, high = args.forms.partition("-")
        forms = (int(low), int(high or low))
    assets = sprite_assets(Path(args.dir), forms)
    mac = installer.mac(args.mac)
    print(f"{len(assets)} sprites, {sum(a.size for a in assets):,} bytes, all pass the firmware DVA checks")
    report = {"mac": mac, "files": len(assets), "execute": args.execute}
    port = port_for(mac)
    connection = installer.open_serial(port)
    try:
        client = installer.SerialProtocol(connection)
        installer.run_device(client, assets, mac, args.execute, None, report,
                             emit=lambda line: print(line, flush=True))
    except (installer.Invalid, installer.TransferError) as error:
        report.update(result="FAILED", error=str(error))
        fail(str(error))
    finally:
        connection.close()
        folder = BACKUPS / f"{datetime.date.today().isoformat()}-sprites" / mac.replace(":", "-")
        folder.mkdir(parents=True, exist_ok=True)
        name = "sprites-" + ("install" if args.execute else "inspect") + datetime.datetime.now().strftime("-%H%M%S.json")
        descriptor = os.open(folder / name, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(descriptor, "w") as out:
            json.dump(report, out, indent=1)
    states = {}
    for row in report.get("preflight", []):
        states[row["state"]] = states.get(row["state"], 0) + 1
    print(f"{report.get('result')}: on card before {states}; verified after {report.get('finalVerifiedFiles', 0)}")


def main():
    try:
        import serial  # noqa: F401  (pyserial lives in the task-local esptool environment)
    except ImportError:
        if VENV_PYTHON.exists() and Path(sys.executable).resolve() != VENV_PYTHON.resolve():
            os.execv(str(VENV_PYTHON), [str(VENV_PYTHON), __file__, *sys.argv[1:]])
        fail(f"pyserial is missing; expected the esptool environment at {VENV_PYTHON}")
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("boards").set_defaults(run=command_boards)
    status = commands.add_parser("status")
    status.add_argument("--mac", required=True)
    status.add_argument("--watch", type=float, default=0, help="seconds to print touch-counter changes")
    status.set_defaults(run=command_status)
    saves = commands.add_parser("saves")
    saves.add_argument("--nvs", required=True, help="NVS partition backup (nvs-before-flash.bin)")
    saves.add_argument("--against", help="earlier save summary JSON; fail on non-schema/rules changes")
    saves.set_defaults(run=command_saves)
    firmware = commands.add_parser("firmware")
    firmware.add_argument("--mac", action="append", required=True, help="repeat for each board")
    firmware.add_argument("--app", default=str(BUILD / "digivice.bin"))
    firmware.add_argument("--execute", action="store_true")
    firmware.set_defaults(run=command_firmware)
    sprites = commands.add_parser("sprites")
    sprites.add_argument("--mac", required=True)
    sprites.add_argument("--dir", required=True, help="folder of DSFnnnnn.DVA files")
    sprites.add_argument("--forms", help="limit to a form range, e.g. 277-465")
    sprites.add_argument("--execute", action="store_true")
    sprites.set_defaults(run=command_sprites)
    args = parser.parse_args()
    args.run(args)


if __name__ == "__main__":
    main()
