"""Risk-focused USB installer checks. Fake serial + temporary files only."""

import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("install_sd_usb", ROOT / "scripts/install-sd-usb.py")
host = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = host
SPEC.loader.exec_module(host)
MAC = "aa:bb:cc:dd:ee:ff"


class SerialOpenTests(unittest.TestCase):
    """Control-line ordering and cleanup without importing or opening a real port."""

    class Port:
        def __init__(self, fail_at=None, failure=None, fail_close=False):
            self.events, self.writes = [], []
            self.is_open = False
            self.fail_at, self.failure = fail_at, failure
            self.fail_close = fail_close

        def __setattr__(self, name, value):
            if name in ("rts", "dtr", "port"):
                self.events.append((name, value))
                if value is False and self.fail_at == "release_" + name:
                    raise self.failure
                if name == "dtr" and value is True and self.is_open and self.fail_at == "reapply_dtr":
                    raise self.failure
            object.__setattr__(self, name, value)

        def open(self):
            self.events.append(("open",))
            if self.fail_at == "open_before":
                raise self.failure
            self.is_open = True
            if self.fail_at == "open_after":
                raise self.failure

        def close(self):
            self.events.append(("close",))
            if self.fail_close:
                raise OSError("injected cleanup failure")
            self.is_open = False

        def write(self, data):
            self.writes.append(data)
            raise AssertionError("Opening a port must not send serial data")

    def call_open(self, peer):
        constructor = Mock(return_value=peer)
        with patch.dict(sys.modules, {"serial": SimpleNamespace(Serial=constructor)}):
            try:
                return host.open_serial("/dev/not-a-real-device")
            finally:
                constructor.assert_called_once()
                self.assertIsNone(constructor.call_args.kwargs["port"])
                self.assertEqual(peer.writes, [])

    def test_cached_assertion_then_rts_first_release_without_reset_or_write(self):
        peer = self.Port()
        self.assertIs(self.call_open(peer), peer)
        self.assertEqual(peer.events, [
            ("rts", True), ("dtr", True), ("port", "/dev/not-a-real-device"),
            ("open",), ("rts", False), ("dtr", True), ("dtr", False),
        ])
        self.assertTrue(peer.is_open)

    def test_open_or_release_failure_closes_once_preserves_error_and_never_retries(self):
        for stage in ("open_before", "open_after", "release_rts", "reapply_dtr", "release_dtr"):
            for error_type in (OSError, KeyboardInterrupt):
                with self.subTest(stage=stage, error_type=error_type):
                    failure = error_type("injected " + stage)
                    peer = self.Port(stage, failure)
                    with self.assertRaises(error_type) as caught:
                        self.call_open(peer)
                    self.assertIs(caught.exception, failure)
                    self.assertFalse(peer.is_open)
                    self.assertEqual(peer.events.count(("open",)), 1)
                    self.assertEqual(peer.events.count(("close",)), 1)
                    self.assertEqual(peer.events[-1], ("close",))
                    if stage.startswith("open_"):
                        self.assertNotIn(("rts", False), peer.events)
                    if stage != "release_dtr":
                        self.assertNotIn(("dtr", False), peer.events)

    def test_cleanup_failure_does_not_mask_original_error_or_retry(self):
        for stage in ("open_after", "release_rts", "reapply_dtr", "release_dtr"):
            with self.subTest(stage=stage):
                failure = OSError("original " + stage)
                peer = self.Port(stage, failure, fail_close=True)
                with self.assertRaises(OSError) as caught:
                    self.call_open(peer)
                self.assertIs(caught.exception, failure)
                self.assertEqual(peer.events.count(("open",)), 1)
                self.assertEqual(peer.events.count(("close",)), 1)
                self.assertEqual(peer.events[-1], ("close",))


class FakeSerial:
    """A small independent filesystem peer; models lost replies after durable writes."""
    def __init__(self, directory):
        self.directory = directory
        self.commands, self.replies = [], []
        self.time = 0.0
        self.identity_mac, self.board = MAC, host.BOARD
        self.active = None
        self.partial = bytearray()
        self.drop = {}
        self.corrupt_prefix = False
        self.forward_ack = False
        self.reset_on_chunk = False
        self.finish_count = 0
        self.max_chunk = 0
        self.abort_count = 0
        self.protocol_version = "1"
        self.stale_input = ""
        self.game_commands = []
        self.discarded_lines = 0
        self.timeout_on_chunk = False
        self.stale_metadata = None
        self.session_active = False
        self.drop_abort_after = None

    def clock(self):
        return self.time

    def line(self, value, command):
        key = command.split()[1] if command.startswith("sdput ") else "identity"
        if self.drop.get(key, 0):
            self.drop[key] -= 1
        else:
            self.replies.append((value + "\n").encode())

    def identity(self, name, size, digest):
        return f"name={name} size={size} sha256={digest}"

    def write(self, raw):
        command = self.stale_input + raw.decode().rstrip("\n")
        self.stale_input = ""
        self.commands.append(command)
        if len(command) > host.MAX_COMMAND_BYTES:
            self.discarded_lines += 1
            return len(raw)  # Firmware readLine discards the entire overlong line.
        if command.startswith("hatch"):
            self.game_commands.append(command)
            return len(raw)
        words = command.split()
        if not command:
            return len(raw)
        if command == "device identity":
            self.line(f"device identity mac={self.identity_mac} board={self.board} firmware=test123 sdput={self.protocol_version}", command)
        elif words[:2] in (["sdput", "verify"], ["sdput", "begin"]):
            name, size, digest = words[2], int(words[3]), words[4]
            target = self.directory / name
            ident = self.identity(name, size, digest)
            if words[1] == "begin" and self.session_active and self.active != (name, size, digest):
                self.line("SDPUT ERROR code=BUSY", command)
                return len(raw)
            if target.exists():
                matching = target.is_file() and target.stat().st_size == size and host.sha(target.read_bytes()) == digest
                if words[1] == "begin" and matching and self.stale_metadata == (name, size, digest):
                    self.stale_metadata = None
                value = ("SDPUT VERIFIED " if words[1] == "verify" else "SDPUT EXISTS ") + ident
                self.line(value if matching else "SDPUT ERROR code=CONFLICT", command)
            elif words[1] == "verify":
                self.line("SDPUT ERROR code=MISSING", command)
            elif self.stale_metadata is not None:
                self.line("SDPUT ERROR code=PENDING_OTHER", command)
            else:
                if self.active is not None and self.active != (name, size, digest):
                    prior_name, prior_size, prior_sha = self.active
                    if (self.directory / prior_name).is_file() and len(self.partial) == prior_size and host.sha(self.partial) == prior_sha:
                        self.active = None
                        self.partial = bytearray()
                    else:
                        self.line("SDPUT ERROR code=BUSY", command)
                        return len(raw)
                self.active = (name, size, digest)
                self.session_active = True
                prefix = "0" * 64 if self.corrupt_prefix else host.sha(self.partial)
                self.line(f"SDPUT READY {ident} offset={len(self.partial)} prefixSha256={prefix}", command)
        elif words[:2] == ["sdput", "chunk"]:
            if self.timeout_on_chunk:
                self.session_active = False
                self.line("SDPUT TIMEOUT partial=retained", command)
                return len(raw)
            if self.reset_on_chunk:
                self.line("ESP-ROM:esp32s3-20210327", command)
                return len(raw)
            offset, payload = int(words[2]), bytes.fromhex(words[3])
            self.max_chunk = max(self.max_chunk, len(payload))
            assert 0 < len(payload) <= 512
            if self.active is None or not self.session_active:
                self.line("SDPUT ERROR code=IDLE", command)
            elif offset == len(self.partial):
                self.partial.extend(payload)
                self.line(f"SDPUT ACK offset={len(self.partial) + (512 if self.forward_ack else 0)}", command)
            elif self.partial[offset:offset + len(payload)] == payload and offset + len(payload) <= len(self.partial):
                self.line(f"SDPUT ACK offset={len(self.partial)}", command)
            else:
                self.line("SDPUT ERROR code=OFFSET", command)
        elif command == "sdput finish":
            if self.active is None:
                self.line("SDPUT ERROR code=IDLE", command)
            else:
                name, size, digest = self.active
                if len(self.partial) != size or host.sha(self.partial) != digest:
                    self.line("SDPUT ERROR code=HASH", command)
                else:
                    target = self.directory / name
                    if not target.exists():
                        target.write_bytes(self.partial)
                        self.finish_count += 1
                    self.line("SDPUT DONE " + self.identity(name, size, digest), command)
                    self.session_active = False
                    # Keep completed identity/data for an idempotent finish retry.
        elif command == "sdput abort":
            self.abort_count += 1
            self.session_active = False
            if self.drop_abort_after is None or self.abort_count <= self.drop_abort_after:
                self.line("SDPUT PAUSED", command)
        else:
            raise AssertionError(f"Unexpected command: {command}")
        return len(raw)

    def read_until(self, _delimiter, size):
        self.time += 0.01
        return self.replies.pop(0)[:size] if self.replies else b""

    def read(self, size):
        self.time += 0.1
        if not self.replies:
            return b""
        result = self.replies[0][:size]
        self.replies[0] = self.replies[0][size:]
        if not self.replies[0]:
            self.replies.pop(0)
        return result


class InstallerTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="digivice-sd-host-test-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.card = self.root / "fake-card"
        self.card.mkdir()
        self.peer = FakeSerial(self.card)
        self.client = host.SerialProtocol(self.peer, timeout=0.05, retries=2, clock=self.peer.clock)

    def asset(self, name="DSF00018.DVA", data=None):
        data = data if data is not None else bytes(range(256)) * 9 + b"tail"
        return host.Asset(name, data, host.sha(data))

    def run_peer(self, assets, execute=True):
        return host.run_device(self.client, assets, MAC, execute, "test123", emit=lambda _x: None,
                               clock=self.peer.clock)

    def pack(self):
        folder = self.root / "SD-CONTENTS"
        folder.mkdir()
        rows = []
        names = [f"DSF{i:05}.DVA" for i in range(1, 252)] + sorted(host.SCENES | host.METADATA)
        for name in names:
            data = ("original test fixture: " + name).encode()
            (folder / name).write_bytes(data)
            rows.append({"target": name, "bytes": len(data), "sha256": host.sha(data)})
        manifest = self.root / "pack-manifest.json"
        manifest.write_text(json.dumps({"formatVersion": 1, "files": rows}))
        self.pin(manifest)
        return manifest, folder, rows

    def pin(self, manifest):
        (self.root / "manifest.sha256").write_text(host.sha(manifest.read_bytes()) + "  " + manifest.name + "\n")

    def test_default_dry_run_hashes_complete_pack_without_serial_import_or_open(self):
        manifest, _, _ = self.pack()
        with patch.object(host, "available_ports") as ports, patch.object(host, "open_serial") as serial:
            with contextlib.redirect_stdout(io.StringIO()):
                result = host.main(["--manifest", str(manifest), "--serial", MAC])
        self.assertEqual(result, 0)
        ports.assert_not_called()
        serial.assert_not_called()

    def test_local_source_corruption_and_manifest_pin_failure_prevent_any_usb(self):
        manifest, folder, _ = self.pack()
        (folder / "DSF00001.DVA").write_bytes(b"changed")
        for change_manifest in (False, True):
            if change_manifest:
                manifest.write_text(manifest.read_text() + " ")
            with patch.object(host, "available_ports") as ports, contextlib.redirect_stderr(io.StringIO()):
                result = host.main(["--manifest", str(manifest), "--serial", MAC, "--execute"])
            self.assertEqual(result, 2)
            ports.assert_not_called()

    def test_allowlist_symlink_extra_and_truncated_manifest_are_rejected(self):
        manifest, folder, rows = self.pack()
        baseline = manifest.read_bytes()
        for target in ("../NVS", "DVXFER.TMP", "DVASSET1.CCH", "DSF00000.DVA", "dsf00001.dva"):
            changed = json.loads(baseline)
            changed["files"][0]["target"] = target
            manifest.write_text(json.dumps(changed)); self.pin(manifest)
            with self.assertRaises(host.Invalid):
                host.load_pack(manifest, folder)
        manifest.write_bytes(baseline); self.pin(manifest)
        first = folder / rows[0]["target"]
        data = first.read_bytes(); first.unlink(); first.symlink_to(folder / rows[1]["target"])
        with self.assertRaises(host.Invalid):
            host.load_pack(manifest, folder)
        first.unlink(); first.write_bytes(data)
        (folder / "unknown.txt").write_text("keep")
        with self.assertRaises(host.Invalid):
            host.load_pack(manifest, folder)
        changed = json.loads(baseline); changed["files"].pop()
        manifest.write_text(json.dumps(changed)); self.pin(manifest)
        with self.assertRaises(host.Invalid):
            host.load_pack(manifest, folder)

    def test_port_selection_needs_full_serial_and_unique_matching_port(self):
        ports = [SimpleNamespace(device="/dev/cu.unit1", serial_number=MAC),
                 SimpleNamespace(device="/dev/cu.unit2", serial_number="00:11:22:33:44:55")]
        self.assertEqual(host.select_port(ports, MAC), "/dev/cu.unit1")
        for selection in ("ee:ff", "/dev/cu.unit1", "bad"):
            with self.assertRaises(host.Invalid):
                host.mac(selection)
        with self.assertRaises(host.Invalid):
            host.select_port(ports, MAC, "/dev/cu.unit2")
        with self.assertRaises(host.Invalid):
            host.select_port(ports + [ports[0]], MAC)

    def test_reported_wrong_chip_or_board_stops_before_sd_commands(self):
        for attribute, value in (("identity_mac", "00:11:22:33:44:55"), ("board", "other-board")):
            setattr(self.peer, attribute, value)
            with self.assertRaises((host.TransferError, host.Invalid)):
                self.run_peer([self.asset()])
            self.assertTrue(all(c == "device identity" or len(c) > host.MAX_COMMAND_BYTES for c in self.peer.commands))
            setattr(self.peer, attribute, MAC if attribute == "identity_mac" else host.BOARD)

    def test_missing_protocol_capability_stops_before_sd_commands(self):
        self.peer.protocol_version = "0"
        with self.assertRaisesRegex(host.TransferError, "sdput=1"):
            self.run_peer([self.asset()])
        self.assertEqual(self.peer.commands, [host.SYNC_FENCE[:-1].decode(), "device identity"])

    def test_discard_fence_precedes_identity_and_cannot_submit_stale_game_input(self):
        self.peer.stale_input = "hatch 1"
        self.run_peer([self.asset()], execute=False)
        self.assertEqual(len(host.SYNC_FENCE), 1537)
        self.assertTrue(self.peer.commands[0].startswith("hatch 1"))
        self.assertEqual(self.peer.commands[1], "device identity")
        self.assertEqual(self.peer.discarded_lines, 1)
        self.assertEqual(self.peer.game_commands, [])
        response = host.parse_response(b"digivice> digivice> SDPUT ACK offset=512\r\n")
        self.assertEqual(response, ("ACK", {"offset": "512"}))
        with self.assertRaisesRegex(host.TransferError, "lease timed out"):
            host.parse_response(b"SDPUT TIMEOUT partial=retained\n")

    def test_initial_boot_bytes_are_drained_until_quiet_before_any_command(self):
        original_read = self.peer.read
        boot_parts = [(0.1, b"ESP-ROM:esp32s3\n"),
                      (0.5, b"rst:0x15 (USB_UART_CHIP_RESET)\n"),
                      (3.3, b"boot: Early entropy enabled\r\ndigivice>")]

        def startup_read(size):
            self.assertEqual(self.peer.commands, [])
            if boot_parts and self.peer.time + 0.1 >= boot_parts[0][0]:
                self.peer.replies.append(boot_parts.pop(0)[1])
            return original_read(size)

        self.peer.read = startup_read
        result = self.run_peer([self.asset()], execute=False)
        self.assertEqual(result["result"], "READ_ONLY_INSPECTION_PASS")
        self.assertEqual(boot_parts, [])
        self.assertGreaterEqual(self.peer.time, 4.3)
        self.assertLess(self.peer.time, 5)
        self.assertEqual(self.peer.commands[1], "device identity")
        self.assertEqual(self.client.pending, b"")

    def test_initial_startup_noise_is_bounded_by_bytes_and_time(self):
        for byte_limit in (True, False):
            with self.subTest(byte_limit=byte_limit):
                peer = FakeSerial(self.card)
                client = host.SerialProtocol(peer, timeout=0.05, retries=2, clock=peer.clock)

                def endless_read(size):
                    peer.time += 0.1
                    return b"x" * (size if byte_limit else 1)

                peer.read = endless_read
                with self.assertRaisesRegex(host.TransferError, "bounded settle budget|settle deadline"):
                    host.run_device(client, [self.asset()], MAC, True, emit=lambda _x: None)
                self.assertEqual(peer.commands, [])
                self.assertFalse(client.transport_safe)
                self.assertLessEqual(peer.time, host.STARTUP_SETTLE_SECONDS + 0.2)

    def test_quiet_stalled_boot_still_requires_identity_and_never_starts_sd(self):
        self.peer.replies.append(b"boot: Early entropy enabled\n")
        self.peer.drop["identity"] = 10
        with self.assertRaisesRegex(host.TransferError, "timed out"):
            self.run_peer([self.asset()])
        self.assertEqual(self.peer.commands[0], host.SYNC_FENCE[:-1].decode())
        self.assertEqual(self.peer.commands[1:], ["device identity"] * 3)
        self.assertFalse(self.client.lease_requested)
        self.assertEqual(list(self.card.iterdir()), [])

    def test_reset_after_startup_drain_during_identity_is_fatal(self):
        original = self.peer.write

        def boot_during_identity(raw):
            if raw == b"device identity\n":
                self.peer.replies.append(b"ESP-ROM:esp32s3\n")
            return original(raw)

        self.peer.write = boot_during_identity
        with self.assertRaisesRegex(host.TransferError, "reset/boot detected"):
            self.run_peer([self.asset()])
        self.assertFalse(self.client.transport_safe)
        self.assertFalse(self.client.lease_requested)
        self.assertEqual(len(self.peer.commands), 2)

    def test_inspect_is_read_only_and_never_begins_missing_file(self):
        result = self.run_peer([self.asset()], execute=False)
        self.assertEqual(result["result"], "READ_ONLY_INSPECTION_PASS")
        self.assertEqual(list(self.card.iterdir()), [])
        self.assertFalse(any(c.startswith("sdput begin") for c in self.peer.commands))
        self.assertTrue(result["leaseReleased"])
        self.assertEqual(self.peer.abort_count, 1)

    def test_preflight_conflict_stops_before_any_write_and_preserves_unknowns(self):
        first, second = self.asset(), self.asset("DSF00019.DVA")
        (self.card / second.name).write_bytes(b"old different user file")
        (self.card / "unknown.bin").write_bytes(b"keep")
        with self.assertRaisesRegex(host.TransferError, "Conflicting"):
            self.run_peer([first, second])
        self.assertFalse(any(c.startswith("sdput begin") for c in self.peer.commands))
        self.assertEqual((self.card / second.name).read_bytes(), b"old different user file")
        self.assertEqual((self.card / "unknown.bin").read_bytes(), b"keep")

    def test_identical_file_is_skipped_but_verified_again_at_completion(self):
        asset = self.asset(); (self.card / asset.name).write_bytes(asset.data)
        result = self.run_peer([asset])
        self.assertEqual(result["result"], "PASS")
        self.assertEqual(result["payloadBytesAcknowledged"], 0)
        self.assertEqual(result["finalVerifiedFiles"], 1)
        self.assertEqual(sum(c.startswith("sdput begin") for c in self.peer.commands), 1)
        self.assertFalse(any(c.startswith("sdput chunk") for c in self.peer.commands))
        self.assertEqual(sum(c.startswith("sdput verify") for c in self.peer.commands), 3)

    def test_completed_rename_metadata_is_recovered_before_next_missing_file(self):
        first, second = self.asset(), self.asset("DSF00019.DVA")
        (self.card / first.name).write_bytes(first.data)
        self.peer.stale_metadata = (first.name, first.size, first.digest)
        result = self.run_peer([first, second])
        self.assertEqual(result["result"], "PASS")
        self.assertIsNone(self.peer.stale_metadata)
        self.assertEqual(result["transfers"][0]["result"], "SKIPPED_IDENTICAL")
        self.assertEqual(result["payloadBytesAcknowledged"], second.size)
        self.assertEqual((self.card / second.name).read_bytes(), second.data)

    def test_lost_chunk_and_finish_ack_retry_identically_without_duplicate_write(self):
        asset = self.asset(); self.peer.drop = {"chunk": 1, "finish": 1}
        result = self.run_peer([asset])
        self.assertEqual((self.card / asset.name).read_bytes(), asset.data)
        chunks = [c for c in self.peer.commands if c.startswith("sdput chunk")]
        self.assertEqual(chunks[0], chunks[1])
        self.assertLessEqual(max(len(c) for c in self.peer.commands if c.startswith("sdput")), 1535)
        self.assertEqual(self.peer.max_chunk, 512)
        self.assertEqual(self.peer.finish_count, 1)
        self.assertEqual(result["commandRetries"], 2)
        self.assertEqual(result["payloadBytesAcknowledged"], asset.size)

    def test_same_file_resume_checks_prefix_and_sends_only_remaining_bytes(self):
        asset = self.asset(); self.peer.active = (asset.name, asset.size, asset.digest)
        self.peer.partial.extend(asset.data[:700])
        result = self.run_peer([asset])
        self.assertEqual(result["transfers"][0]["resumedAt"], 700)
        self.assertEqual(result["payloadBytesAcknowledged"], asset.size - 700)
        self.assertEqual((self.card / asset.name).read_bytes(), asset.data)

    def test_bad_resume_prefix_and_unknown_partial_stop_without_chunk_or_cleanup(self):
        asset = self.asset(); self.peer.corrupt_prefix = True
        with self.assertRaisesRegex(host.TransferError, "Resume prefix"):
            self.run_peer([asset])
        self.assertFalse(any(c.startswith("sdput chunk") for c in self.peer.commands))
        self.assertEqual(self.peer.abort_count, 2)
        self.peer.corrupt_prefix = False
        self.peer.active = ("DSF00512.DVA", 3, host.sha(b"old"))
        self.peer.partial = bytearray(b"old")
        with self.assertRaisesRegex(host.TransferError, "BUSY"):
            self.run_peer([asset])
        self.assertEqual(self.peer.partial, b"old")

    def test_forward_ack_and_boot_reset_stop_without_finish_or_reconnect(self):
        for setting in ("forward_ack", "reset_on_chunk"):
            peer = FakeSerial(self.card); setattr(peer, setting, True)
            client = host.SerialProtocol(peer, timeout=0.05, retries=2, clock=peer.clock)
            with self.assertRaises(host.TransferError):
                host.run_device(client, [self.asset()], MAC, execute=True, emit=lambda _x: None)
            self.assertNotIn("sdput finish", peer.commands)
            self.assertEqual(sum(c == "device identity" for c in peer.commands), 1)
            if setting == "reset_on_chunk":
                self.assertEqual(peer.abort_count, 1)  # Only the initial verified-session pause.

    def test_bounded_timeout_preserves_partial_and_never_advances_offset(self):
        self.peer.drop["chunk"] = 10
        with self.assertRaisesRegex(host.TransferError, "timed out"):
            self.run_peer([self.asset()])
        chunks = [c for c in self.peer.commands if c.startswith("sdput chunk")]
        self.assertEqual(len(chunks), 3)
        self.assertEqual(len(set(chunks)), 1)
        self.assertEqual(len(self.peer.partial), 512)
        self.assertNotIn("sdput finish", self.peer.commands)

    def test_large_attribution_uses_bounded_chunks_and_final_whole_hash(self):
        asset = self.asset("ATTRIB.TXT", b"original attribution fixture\n" * 68000)
        result = self.run_peer([asset])
        self.assertEqual(result["finalVerifiedFiles"], 1)
        self.assertEqual(host.sha((self.card / asset.name).read_bytes()), asset.digest)
        self.assertLessEqual(self.peer.max_chunk, 512)

    def test_complete_261_file_run_checks_each_final_hash_and_releases_once(self):
        manifest, folder, _ = self.pack()
        assets, _ = host.load_pack(manifest, folder)
        (self.card / "unknown.dat").write_bytes(b"preserve unknown user data")
        result = self.run_peer(assets)
        self.assertEqual(result["result"], "PASS")
        self.assertEqual(result["finalVerifiedFiles"], 261)
        self.assertEqual(self.peer.finish_count, 261)
        self.assertEqual(self.peer.abort_count, 2)
        self.assertEqual((self.card / "unknown.dat").read_bytes(), b"preserve unknown user data")
        for asset in assets:
            self.assertEqual(host.sha((self.card / asset.name).read_bytes()), asset.digest)

    def test_lease_release_timeout_cannot_report_success(self):
        asset = self.asset(); (self.card / asset.name).write_bytes(asset.data)
        self.peer.drop_abort_after = 1
        report = {}
        with self.assertRaisesRegex(host.TransferError, "lease release"):
            host.run_device(self.client, [asset], MAC, execute=True, report=report,
                            emit=lambda _x: None, clock=self.peer.clock)
        self.assertFalse(report["leaseReleased"])
        self.assertEqual(report["finalVerifiedFiles"], 1)
        self.assertEqual(self.peer.abort_count, 4)
        self.assertEqual(report["result"], "FAILED")

    def test_partial_usb_write_does_not_append_abort_to_uncertain_line(self):
        original = self.peer.write

        def truncated(raw):
            if raw.startswith(b"sdput chunk"):
                self.peer.commands.append("partial chunk")
                return 7
            return original(raw)

        self.peer.write = truncated
        with self.assertRaisesRegex(host.TransferError, "Partial USB write"):
            self.run_peer([self.asset()])
        self.assertEqual(self.peer.abort_count, 1)
        self.assertFalse(self.client.transport_safe)

    def test_lease_timeout_requires_new_fence_and_identity_before_resume(self):
        asset = self.asset(); self.peer.timeout_on_chunk = True
        with self.assertRaisesRegex(host.TransferError, "lease timed out"):
            self.run_peer([asset])
        self.assertFalse(self.client.transport_safe)
        self.assertEqual(self.peer.abort_count, 1)
        self.peer.timeout_on_chunk = False
        self.client = host.SerialProtocol(self.peer, timeout=0.05, retries=2, clock=self.peer.clock)
        result = self.run_peer([asset])
        self.assertEqual(result["result"], "PASS")
        self.assertEqual(self.peer.commands.count("device identity"), 2)
        self.assertEqual(self.peer.discarded_lines, 2)

    def test_interrupted_write_never_appends_abort_to_uncertain_line(self):
        for during_fence in (True, False):
            with self.subTest(during_fence=during_fence):
                peer = FakeSerial(self.card)
                client = host.SerialProtocol(peer, timeout=0.05, retries=2, clock=peer.clock)
                original = peer.write

                def interrupted(raw):
                    interrupt_here = raw == host.SYNC_FENCE if during_fence else raw.startswith(b"sdput chunk")
                    if interrupt_here:
                        peer.commands.append("interrupted partial write")
                        raise KeyboardInterrupt()
                    return original(raw)

                peer.write = interrupted
                with self.assertRaises(KeyboardInterrupt):
                    host.run_device(client, [self.asset()], MAC, True, "test123",
                                    emit=lambda _x: None, clock=peer.clock)
                self.assertFalse(client.transport_safe)
                self.assertEqual(peer.abort_count, 0 if during_fence else 1)
                self.assertEqual(peer.commands[-1], "interrupted partial write")

    def test_live_later_partial_resumes_before_previous_lease_timeout(self):
        first, second = self.asset(), self.asset("DSF00019.DVA")
        (self.card / first.name).write_bytes(first.data)
        self.peer.active = (second.name, second.size, second.digest)
        self.peer.partial = bytearray(second.data[:512])
        self.peer.session_active = True
        result = self.run_peer([first, second])
        self.assertEqual(result["result"], "PASS")
        self.assertTrue(result["previousLeasePaused"])
        self.assertEqual(result["transfers"][1]["resumedAt"], 512)
        self.assertEqual(result["payloadBytesAcknowledged"], second.size - 512)
        self.assertEqual(self.peer.commands[2], "sdput abort")


if __name__ == "__main__":
    unittest.main()
