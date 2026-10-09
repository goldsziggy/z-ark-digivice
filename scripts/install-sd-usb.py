#!/usr/bin/env python3
"""Install the reviewed SD pack through an explicitly selected Digivice USB port.

Default: validate all local files and print a plan without opening USB. --inspect
adds read-only device/hash checks. Only --execute sends SDPUT mutation commands.
Uses an existing pyserial installation; never installs tools, deliberately resets,
reconnects, switches devices, sends credentials, or copies game saves. Opening a
USB console can still reset the chip through OS/driver control-line behavior.
"""

import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import time


BOARD = "waveshare-esp32-s3-touch-lcd-1.46"
CHUNK_BYTES = 512
MAX_COMMAND_BYTES = 1535
SYNC_FENCE = b"~" * (MAX_COMMAND_BYTES + 1) + b"\n"
STARTUP_SETTLE_SECONDS = 10.0
STARTUP_MIN_SECONDS = 4.0
STARTUP_QUIET_SECONDS = 1.0
MAX_STARTUP_BYTES = 64 * 1024
MAX_MANIFEST_BYTES = 2 * 1024 * 1024
MAX_PACK_BYTES = 8 * 1024 * 1024
SCENES = {name + ".JPG" for name in
          ("MEADOW", "FOREST", "BEACH", "RUINS", "CAVERN", "SNOW", "VOLCANIC", "DIGITAL")}
METADATA = {"INDEX.JSON", "ATTRIB.TXT"}
HASH = re.compile(r"[0-9a-f]{64}\Z")


class Invalid(ValueError):
    pass


class TransferError(RuntimeError):
    pass


def require(condition, message):
    if not condition:
        raise Invalid(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def mac(value):
    require(isinstance(value, str), "A complete USB serial / chip MAC is required")
    compact = value.replace(":", "").replace("-", "")
    require(re.fullmatch(r"[0-9A-Fa-f]{12}", compact) is not None,
            "Use the complete 12-hex-digit USB serial / MAC, not a suffix or port alone")
    return ":".join(compact[i:i + 2].lower() for i in range(0, 12, 2))


def file_bytes(path, limit):
    require(not path.is_symlink() and path.is_file(), f"Expected a regular, non-symlink file: {path.name}")
    require(0 < path.stat().st_size <= limit, f"Invalid file size: {path.name}")
    with path.open("rb") as stream:
        data = stream.read(limit + 1)
    require(0 < len(data) <= limit, f"File changed or exceeds size bound: {path.name}")
    return data


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, "Duplicate JSON field")
        result[key] = value
    return result


@dataclass(frozen=True)
class Asset:
    name: str
    data: bytes
    digest: str

    @property
    def size(self):
        return len(self.data)


def load_pack(manifest, contents, expected_digest=None):
    """Snapshot all 4.7 MB before any device access; source changes cannot race writes."""
    raw = file_bytes(manifest, MAX_MANIFEST_BYTES)
    if expected_digest is None:
        pin = file_bytes(manifest.with_name("manifest.sha256"), 512).decode("ascii").split()
        require(len(pin) == 2 and pin[1] == manifest.name, "Invalid manifest.sha256 pin")
        expected_digest = pin[0]
    require(isinstance(expected_digest, str) and HASH.fullmatch(expected_digest), "Invalid manifest SHA-256")
    require(sha(raw) == expected_digest, "Manifest differs from its pinned SHA-256")
    document = json.loads(raw, object_pairs_hook=unique_object)
    require(isinstance(document, dict) and document.get("formatVersion") == 1, "Unsupported manifest")
    rows = document.get("files")
    require(isinstance(rows, list) and len(rows) == 261, "Expected the complete 261-file pack")
    require(contents.is_dir() and not contents.is_symlink(), "Expected a non-symlink SD-CONTENTS directory")
    assets, names, total = [], set(), 0
    for row in rows:
        require(isinstance(row, dict), "Invalid file record")
        name, size, digest = row.get("target"), row.get("bytes"), row.get("sha256")
        require(isinstance(name, str), "Invalid destination filename")
        sprite = re.fullmatch(r"DSF([0-9]{5})\.DVA", name)
        allowed_sprite = sprite is not None and 1 <= int(sprite[1]) <= 512
        require(allowed_sprite or name in SCENES | METADATA, f"Destination is not allowlisted: {name!r}")
        require(name.casefold() not in names, "Duplicate/case-colliding destination")
        names.add(name.casefold())
        limit = 2 * 1024 * 1024 if name in METADATA else 128 * 1024
        require(type(size) is int and 0 < size <= limit, f"Invalid declared size: {name}")
        require(isinstance(digest, str) and HASH.fullmatch(digest), f"Invalid SHA-256: {name}")
        data = file_bytes(contents / name, limit)
        require(len(data) == size and sha(data) == digest, f"Staged bytes/hash mismatch: {name}")
        total += size
        require(total <= MAX_PACK_BYTES, "Pack exceeds the bounded transfer budget")
        assets.append(Asset(name, data, digest))
    require({x.name for x in assets if x.name.endswith(".JPG")} == SCENES
            and {x.name for x in assets if x.name in METADATA} == METADATA
            and sum(x.name.endswith(".DVA") for x in assets) == 251,
            "Expected 251 sprites, eight scenes, INDEX.JSON and ATTRIB.TXT")
    require({p.name for p in contents.iterdir()} == {x.name for x in assets},
            "SD-CONTENTS contains unexpected or missing entries")
    return sorted(assets, key=lambda x: x.name), expected_digest


def select_port(ports, expected_mac, port=None):
    """Enumeration only. A path can narrow an explicit serial, never replace it."""
    selected = []
    for item in ports:
        try:
            same = mac(item.serial_number) == expected_mac
        except Invalid:
            same = False
        if same and (port is None or item.device == port):
            selected.append(item.device)
    require(len(selected) == 1, "Expected exactly one enumerated port matching the explicit full USB serial")
    return selected[0]


def parse_response(line):
    # Ignore bounded boot/console chatter; never log it (it is not our protocol).
    text = line.decode("ascii", errors="replace").strip()
    if re.search(r"(?:ESP-ROM:|rst:0x|USB_UART_CHIP_RESET|boot:.*(?:load|entropy))", text):
        raise TransferError("Device reset/boot detected; stop and re-identify in a new invocation")
    while text.startswith("digivice>"):
        text = text[len("digivice>"):].lstrip()
    if text.startswith("SDPUT TIMEOUT"):
        raise TransferError("Device installation lease timed out; partial retained; start a newly identified invocation")
    if text.startswith("device identity "):
        tag, tokens = "IDENTITY", text[len("device identity "):].split()
    elif text.startswith("SDPUT "):
        parts = text.split()
        if len(parts) < 2:
            raise TransferError("Malformed SDPUT response")
        tag, tokens = parts[1], parts[2:]
    else:
        return None
    fields = {}
    for token in tokens:
        if token.count("=") != 1:
            raise TransferError("Malformed response field")
        key, value = token.split("=", 1)
        if key in fields or not re.fullmatch(r"[A-Za-z][A-Za-z0-9]*", key) or not value:
            raise TransferError("Invalid/duplicate response field")
        fields[key] = value
    return tag, fields


class SerialProtocol:
    def __init__(self, serial, timeout=30.0, retries=3, clock=time.monotonic):
        self.serial, self.timeout, self.retries, self.clock = serial, timeout, retries, clock
        self.pending = b""
        self.command_retries = 0
        self.wire_bytes = 0
        self.lease_requested = False
        self.transport_safe = True
        self.synchronized = False

    def _settle_startup(self):
        """Discard initial USB boot chatter once, before sending any command.

        Reads use the connection's bounded 0.2-second timeout. Allow at least four
        seconds for delayed banners. Quiet only permits the identity handshake;
        it does not prove the application has booted.
        No startup bytes are parsed, logged or retained.
        """
        started = last_data = self.clock()
        discarded = 0
        while self.clock() - started < STARTUP_SETTLE_SECONDS:
            try:
                part = self.serial.read(min(4096, MAX_STARTUP_BYTES - discarded + 1))
            except (OSError, KeyboardInterrupt):
                self.transport_safe = False
                raise
            now = self.clock()
            if part:
                discarded += len(part)
                if discarded > MAX_STARTUP_BYTES:
                    self.transport_safe = False
                    raise TransferError("Initial USB startup output exceeds the bounded settle budget")
                last_data = now
            elif (now - last_data >= STARTUP_QUIET_SECONDS
                  and STARTUP_MIN_SECONDS <= now - started <= STARTUP_SETTLE_SECONDS):
                return
        self.transport_safe = False
        raise TransferError("Initial USB startup did not become quiet before the settle deadline")

    def synchronize(self):
        """Force readLine to discard any stale fragment, without executing it.

        This deliberately invalid 1536-character line is the sole framing
        exception; ordinary protocol commands remain <=1535 characters.
        A blank newline could accidentally submit an earlier gameplay fragment.
        """
        if self.synchronized:
            return
        if not self.transport_safe:
            raise TransferError("USB stream is no longer trusted")
        self._settle_startup()
        try:
            written = self.serial.write(SYNC_FENCE)
        except (OSError, KeyboardInterrupt):
            self.transport_safe = False
            raise
        if written != len(SYNC_FENCE):
            self.transport_safe = False
            raise TransferError("Partial USB synchronization write")
        self.wire_bytes += written
        self.synchronized = True

    def command(self, command, accept):
        if not self.transport_safe:
            raise TransferError("USB stream is no longer trusted; start a newly identified invocation")
        raw = command.encode("ascii")
        if len(raw) > MAX_COMMAND_BYTES or b"\n" in raw or b"\r" in raw:
            raise TransferError("Command exceeds the bounded line protocol")
        if command.startswith("sdput "):
            self.lease_requested = True
        for attempt in range(self.retries + 1):
            if attempt:
                self.command_retries += 1
            try:
                written = self.serial.write(raw + b"\n")
            except (OSError, KeyboardInterrupt):
                self.transport_safe = False
                raise
            if written != len(raw) + 1:
                self.transport_safe = False
                raise TransferError("Partial USB write; stop without reconnecting or appending commands")
            self.wire_bytes += written
            deadline = self.clock() + self.timeout
            while self.clock() < deadline:
                try:
                    part = self.serial.read_until(b"\n", 4096)
                except OSError:
                    self.transport_safe = False
                    raise
                self.pending += part
                if len(self.pending) > 4096:
                    self.transport_safe = False
                    raise TransferError("Overlong response; stop without retrying this connection")
                if not self.pending.endswith(b"\n"):
                    continue
                line, self.pending = self.pending, b""
                try:
                    response = parse_response(line)
                except TransferError:
                    self.transport_safe = False
                    raise
                if response is None:
                    continue
                tag, fields = response
                if tag == "ERROR":
                    if set(fields) != {"code"} or not re.fullmatch(r"[A-Z0-9_]+", fields["code"]):
                        raise TransferError("Malformed SDPUT error")
                    return response
                if accept(tag, fields):
                    return response
        raise TransferError("USB response timed out after bounded retries; partial transfer is preserved")


def number(fields, name):
    value = fields.get(name, "")
    if not re.fullmatch(r"0|[1-9][0-9]*", value):
        raise TransferError(f"Invalid {name} in device response")
    return int(value)


def exact_asset(fields, asset):
    if fields.get("name") != asset.name:
        return False  # A delayed reply for an earlier file must not acknowledge this one.
    if number(fields, "size") != asset.size or fields.get("sha256") != asset.digest:
        raise TransferError(f"Device returned a different file identity: {asset.name}")
    return True


def error_check(response, operation):
    tag, fields = response
    if tag == "ERROR":
        raise TransferError(f"{operation}: device refused with {fields['code']}; existing files/partial preserved")
    return response


def identify(client, expected_mac, firmware=None):
    client.synchronize()
    tag, fields = error_check(client.command("device identity", lambda t, f: t == "IDENTITY"), "identity")
    if set(fields) != {"mac", "board", "firmware", "sdput"} or mac(fields["mac"]) != expected_mac:
        raise TransferError("Reported chip MAC differs from the explicitly selected USB serial")
    if fields["sdput"] != "1":
        raise TransferError("Firmware does not advertise the required sdput=1 protocol")
    if fields["board"] != BOARD:
        raise TransferError("Reported board is not the reviewed Waveshare 1.46 target")
    if not re.fullmatch(r"[A-Za-z0-9._-]{1,40}", fields["firmware"]):
        raise TransferError("Invalid firmware identity")
    if firmware is not None and fields["firmware"] != firmware:
        raise TransferError("Reported firmware differs from --firmware")
    return fields


def verify(client, asset):
    result = client.command(f"sdput verify {asset.name} {asset.size} {asset.digest}",
                            lambda t, f: t == "VERIFIED" and exact_asset(f, asset))
    tag, fields = result
    if tag == "ERROR" and fields["code"] in ("MISSING", "CONFLICT"):
        return fields["code"]
    error_check(result, f"verify {asset.name}")
    return "VERIFIED"


def transfer(client, asset, progress):
    result = error_check(client.command(f"sdput begin {asset.name} {asset.size} {asset.digest}",
                         lambda t, f: t in ("READY", "EXISTS") and exact_asset(f, asset)),
                         f"begin {asset.name}")
    tag, fields = result
    if tag == "EXISTS":
        if verify(client, asset) != "VERIFIED":
            raise TransferError(f"Existing destination failed verification: {asset.name}")
        return {"name": asset.name, "result": "SKIPPED_IDENTICAL", "bytesSent": 0, "resumedAt": 0}
    offset = number(fields, "offset")
    if offset > asset.size or fields.get("prefixSha256") != sha(asset.data[:offset]):
        raise TransferError(f"Resume prefix differs from the local source: {asset.name}; no chunks sent")
    resumed = offset
    while offset < asset.size:
        payload = asset.data[offset:offset + CHUNK_BYTES]
        expected = offset + len(payload)

        def accepted(t, f):
            if t != "ACK":
                return False
            acknowledged = number(f, "offset")
            if acknowledged > expected:
                raise TransferError("ACK exceeds the one outstanding chunk; stop without skipping bytes")
            return acknowledged == expected

        error_check(client.command(f"sdput chunk {offset} {payload.hex()}", accepted),
                    f"chunk {asset.name} at {offset}")
        offset = expected
        progress(asset.name, offset, asset.size, len(payload))
    error_check(client.command("sdput finish", lambda t, f: t == "DONE" and exact_asset(f, asset)),
                f"finish {asset.name}")
    if verify(client, asset) != "VERIFIED":
        raise TransferError(f"Published destination failed verification: {asset.name}")
    return {"name": asset.name, "result": "VERIFIED", "bytesSent": asset.size - resumed, "resumedAt": resumed}


def _run_device(client, assets, expected_mac, execute=False, firmware=None, report=None,
                emit=print, clock=time.monotonic):
    report = report if report is not None else {}
    report["identity"] = identify(client, expected_mac, firmware)
    emit(f"Verified chip {expected_mac}; firmware {report['identity']['firmware']}")
    if execute:
        # A killed host can leave the previous engine session active until its
        # idle deadline. Close that logical session now; its owned bytes remain.
        error_check(client.command("sdput abort", lambda t, f: t in ("PAUSED", "IDLE")),
                    "pause prior installation session")
        report["previousLeasePaused"] = True
    plan = []
    for asset in assets:
        state = verify(client, asset)
        plan.append({"name": asset.name, "state": state, "bytes": asset.size, "sha256": asset.digest})
        if state == "CONFLICT":
            report["preflight"] = plan
            raise TransferError(f"Conflicting existing destination: {asset.name}; no files written")
    report["preflight"] = plan
    if not execute:
        report["result"] = "READ_ONLY_INSPECTION_PASS"
        return report
    started, last_print, sent = clock(), clock(), 0
    report["transfers"] = []

    def progress(name, offset, size, new_bytes):
        nonlocal sent, last_print
        sent += new_bytes
        now = clock()
        report["payloadBytesAcknowledged"] = sent
        if now - last_print >= 1:
            emit(f"{name}: {offset}/{size} bytes; {sent / max(now - started, 0.001):.0f} acknowledged B/s")
            last_print = now

    for index, asset in enumerate(assets, 1):
        # BEGIN/EXISTS sends no payload for an identical final, but also lets the
        # device clean matching owned metadata left by power loss after rename.
        # Read-only inspection above never invokes this recovery mutation.
        row = transfer(client, asset, progress)
        report["transfers"].append(row)
        emit(f"[{index}/{len(assets)}] {row['result']} {asset.name}")
    # Do not infer full-pack success from counts or earlier preflight hashes.
    report["finalVerifiedFiles"] = 0
    for asset in assets:
        if verify(client, asset) != "VERIFIED":
            raise TransferError(f"Final pack readback failed: {asset.name}")
        report["finalVerifiedFiles"] += 1
    elapsed = max(clock() - started, 0.001)
    report.update(result="PASS", payloadBytesAcknowledged=sent, transferSeconds=elapsed,
                  acknowledgedBytesPerSecond=sent / elapsed, commandRetries=client.command_retries,
                  wireBytesWritten=client.wire_bytes)
    return report


def run_device(client, assets, expected_mac, execute=False, firmware=None, report=None,
               emit=print, clock=time.monotonic):
    report = report if report is not None else {}
    try:
        return _run_device(client, assets, expected_mac, execute, firmware, report, emit, clock)
    finally:
        # Verify also acquires the installation lease. Abort closes the session
        # without deleting its owned partial and lets the UI resume. A mismatched
        # identity never attempts SDPUT and must not send this command.
        failed = sys.exc_info()[0] is not None
        if client.lease_requested and client.transport_safe:
            try:
                error_check(client.command("sdput abort", lambda t, f: t in ("PAUSED", "IDLE")),
                            "release installation lease")
                report["leaseReleased"] = True
            except (TransferError, OSError) as exc:
                report["leaseReleased"] = False
                report["leaseReleaseError"] = str(exc)
                if not failed:
                    report["result"] = "FAILED"
                    raise TransferError("Transfer checked, but UI lease release was not acknowledged") from exc
        elif client.lease_requested:
            report["leaseReleased"] = False
            report["leaseReleaseError"] = "Unsafe/disconnected USB stream; no command appended; firmware inactivity timeout releases lease"
        report["commandRetries"] = client.command_retries
        report["wireBytesWritten"] = client.wire_bytes


def open_serial(port):
    """Open once using ESP-IDF monitor 1.10.0's --no-reset line ordering.

    pySerial applies DTR before RTS during open. Caching False/False does not
    suppress those writes and can introduce a reset transition when the OS has
    asserted the lines. Cache True/True, then release RTS before DTR, as the
    official monitor does. This cannot guarantee a reset-free OS/driver open.
    No automatic retry, reopen, hard reset or input flush is added here.
    """
    import serial  # Existing SDK Python/pyserial only; dry-run never imports it.
    connection = serial.Serial(port=None, baudrate=115200, timeout=0.2, write_timeout=5)
    try:
        connection.rts = True
        connection.dtr = True
        connection.port = port
        connection.open()
        connection.rts = False
        # Espressif's RTS primitive also reapplies current DTR for usbser.sys.
        connection.dtr = connection.dtr
        connection.dtr = False
    except BaseException:
        # The caller has not received the descriptor yet. Do not leak it if a
        # post-open control-line ioctl fails, and retain the original error.
        try:
            connection.close()
        except Exception:
            pass
        raise
    return connection


def available_ports():
    from serial.tools import list_ports
    return list(list_ports.comports())


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--manifest-sha256", help="Expected digest; otherwise use sibling manifest.sha256")
    parser.add_argument("--contents", type=Path, help="Default: SD-CONTENTS beside the manifest")
    parser.add_argument("--serial", required=True, help="Full stable USB serial / chip MAC; never a suffix")
    parser.add_argument("--port", help="Optional path restriction in addition to --serial")
    parser.add_argument("--firmware", help="Optional exact reported firmware version")
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--execute", action="store_true")
    modes.add_argument("--inspect", action="store_true", help="Read-only USB identity and destination hashes")
    modes.add_argument("--dry-run", action="store_true", help="Default: local validation only, no USB open")
    parser.add_argument("--report", type=Path, help="New private JSON report; existing paths are refused")
    args = parser.parse_args(argv)
    report, connection, output = {}, None, None
    try:
        expected_mac = mac(args.serial)
        assets, manifest_digest = load_pack(args.manifest,
                                           args.contents or args.manifest.parent / "SD-CONTENTS",
                                           args.manifest_sha256)
        report.update(expectedMac=expected_mac, manifestSha256=manifest_digest,
                      files=len(assets), bytes=sum(x.size for x in assets), execute=args.execute)
        if args.report:
            descriptor = os.open(args.report, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            output = os.fdopen(descriptor, "w")
        if not (args.execute or args.inspect):
            report["result"] = "LOCAL_DRY_RUN_PASS"
            print(f"DRY RUN: {len(assets)} files / {report['bytes']} bytes verified locally for {expected_mac}.")
            print("No USB port opened. --inspect checks remote hashes; --execute permits missing-file transfers.")
            return 0
        port = select_port(available_ports(), expected_mac, args.port)
        report["port"] = port
        connection = open_serial(port)
        client = SerialProtocol(connection)
        run_device(client, assets, expected_mac, args.execute, args.firmware, report)
        print(f"{report['result']}: {report.get('finalVerifiedFiles', 0)} final hashes verified; no unknown files changed.")
        return 0
    except (Invalid, TransferError, OSError, ImportError, UnicodeError, json.JSONDecodeError) as exc:
        report.update(result="FAILED", error=str(exc))
        print(f"STOP: {exc}. No deliberate reset, reconnect or overwrite attempted; partial files are preserved.", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        report.update(result="INTERRUPTED", error="Interrupted; owned partial is preserved")
        print("STOP: interrupted; partial preserved, no automatic reconnect/reset.", file=sys.stderr)
        return 130
    finally:
        if connection is not None:
            connection.close()
        if output is not None:
            with output:
                json.dump(report, output, indent=2)
                output.write("\n")


if __name__ == "__main__":
    sys.exit(main())
