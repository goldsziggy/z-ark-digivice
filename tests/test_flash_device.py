"""Risk checks using current reviewed build, temporary copies and mocked subprocesses.

Run: python3 -m unittest discover -s tests -p test_flash_device.py -v
No serial device is opened, even in the explicit-execution path tests.
"""

import contextlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("flash_device", ROOT / "scripts/flash-device.py")
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)


class FlashDeviceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = ROOT / "firmware/build-waveshare"
        if not (cls.build / "digivice.bin").is_file():
            raise unittest.SkipTest("Requires reviewed local build-waveshare artifacts; no download/build performed")
        cls.shared = tempfile.TemporaryDirectory(prefix="digivice-installer-tests-")
        cls.base = Path(cls.shared.name) / "baseline"
        with contextlib.redirect_stdout(io.StringIO()):
            helper.package(cls.build, cls.base)

    @classmethod
    def tearDownClass(cls):
        cls.shared.cleanup()

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="digivice-installer-case-")
        self.addCleanup(self.tmp.cleanup)
        self.directory = Path(self.tmp.name)
        self.package = self.directory / "package"
        shutil.copytree(self.base, self.package)

    def run_cli(self, extra=None):
        args = ["flash", "--package", str(self.package), "--port", "/dev/cu.TEST_ONLY"]
        output = io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            status = helper.main(args + (extra or []))
        return status, output.getvalue()

    def manifest(self, change):
        path = self.package / "manifest.json"
        data = json.loads(path.read_text())
        change(data)
        path.write_text(json.dumps(data))

    def test_package_is_allowlisted_and_offsets_are_from_build(self):
        contents = sorted(str(p.relative_to(self.package)) for p in self.package.rglob("*") if p.is_file())
        self.assertEqual(contents, sorted([*helper.FILES.values(), "manifest.json"]))
        images, offsets = helper.load_package(self.package)
        args = json.loads((self.build / "flasher_args.json").read_text())
        self.assertEqual(offsets, {role: int(args[role]["offset"], 0) for role in helper.FILES})
        self.assertEqual(len(images["app"]), 1547280)
        manifest = json.loads((self.package / "manifest.json").read_text())
        self.assertEqual(manifest["firmwareSource"], "6e058e18326b9a28f14b788a4e58c4c44bab5b75")
        self.assertEqual(manifest["embeddedProjectVersion"], "6e058e1")
        self.assertEqual(images["app"][48:80].rstrip(b"\0").decode(), manifest["embeddedProjectVersion"])

    def test_sector_erase_extents_preserve_this_release_nvs(self):
        images, offsets = helper.load_package(self.package)
        table = helper.partitions(images["partition-table"])
        nvs_start, nvs_size = table["nvs"][2:]
        for role, data in images.items():
            start = offsets[role]
            end = start + ((len(data) + 4095) // 4096) * 4096
            self.assertTrue(end <= nvs_start or start >= nvs_start + nvs_size, role)

    def test_default_dry_run_never_starts_any_process(self):
        with patch.object(helper.subprocess, "run") as run:
            status, output = self.run_cli()
        self.assertEqual(status, 0)
        run.assert_not_called()
        self.assertIn("DRY RUN", output)
        self.assertIn("0x20000", output)
        self.assertNotIn("bootloader.bin", output)
        self.assertNotIn("erase_flash", output)

    def test_initial_install_dry_run_has_only_four_generated_offsets(self):
        with patch.object(helper.subprocess, "run") as run:
            status, output = self.run_cli(["--initial-install", "--dry-run"])
        self.assertEqual(status, 0)
        run.assert_not_called()
        for offset in ("0x0 ", "0x8000 ", "0x19000 ", "0x20000 "):
            self.assertIn(offset, output)
        self.assertNotIn("0x9000 ", output)  # NVS is never a flash target.

    def test_execute_requires_matching_acknowledgment_before_any_process(self):
        for flags in (["--execute"], ["--execute", "--initial-install"],
                      ["--execute", "--initial-install", "--acknowledge-existing-layout"]):
            with self.subTest(flags=flags), patch.object(helper.subprocess, "run") as run:
                status, output = self.run_cli(flags)
                self.assertEqual(status, 2)
                self.assertIn("requires --acknowledge", output)
                run.assert_not_called()

    def test_explicit_execute_calls_pinned_official_tool_with_snapshot_only(self):
        def fake_run(cmd, **kwargs):
            self.assertEqual(cmd[:5], ["/trusted/python", "-I", "-m", "esptool", "version"]
                             if cmd[-1] == "version" else ["/trusted/python", "-I", "-m", "esptool", "--chip"])
            if cmd[-1] == "version":
                return subprocess.CompletedProcess(cmd, 0, "esptool.py v4.12.0\n4.12.0\n")
            self.assertIn("write_flash", cmd)
            self.assertNotIn("erase_flash", cmd)
            self.assertEqual(cmd[-2], "0x20000")
            self.assertNotEqual(Path(cmd[-1]), self.package / "digivice.bin")
            self.assertEqual(helper.sha(Path(cmd[-1]).read_bytes()), helper.HASHES["app"])
            return subprocess.CompletedProcess(cmd, 0)

        with patch.object(helper.subprocess, "run", side_effect=fake_run) as run:
            status, output = self.run_cli(["--execute", "--acknowledge-existing-layout", "--python", "/trusted/python"])
        self.assertEqual(status, 0, output)
        self.assertEqual(run.call_count, 2)

    def test_explicit_initial_install_has_no_nvs_image_or_full_erase(self):
        with patch.object(helper.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "4.12.0\n")) as run:
            status, output = self.run_cli(["--execute", "--initial-install", "--acknowledge-boot-selection-reset"])
        self.assertEqual(status, 0, output)
        cmd = run.call_args_list[-1].args[0]
        self.assertEqual(cmd[-8::2], ["0x0", "0x8000", "0x19000", "0x20000"])
        self.assertNotIn("erase_flash", cmd)

    def test_wrong_tool_version_prevents_write(self):
        with patch.object(helper.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "5.0.0\n")) as run:
            status, output = self.run_cli(["--execute", "--acknowledge-existing-layout"])
        self.assertEqual(status, 2)
        self.assertEqual(run.call_count, 1)
        self.assertIn("nothing was flashed", output)

    def test_wrong_chip_profile_offset_and_traversal_stop_before_subprocess(self):
        changes = [lambda x: x.update(chip="esp32"), lambda x: x.update(profile="generic"),
                   lambda x: x.update(embeddedProjectVersion="e787093-dirty"),
                   lambda x: x.update(firmwareSource="b0864aebb2c6032d6b449bca3cf518f6b9dc1eca"),
                   lambda x: x["images"]["app"].update(offset=0x9000),
                   lambda x: x["images"]["app"].update(offset=0),
                   lambda x: x["images"]["app"].update(file="../escape.bin"),
                   lambda x: x["images"]["app"].update(file="/tmp/escape.bin")]
        original = (self.package / "manifest.json").read_text()
        for change in changes:
            with self.subTest(change=change):
                (self.package / "manifest.json").write_text(original)
                self.manifest(change)
                with patch.object(helper.subprocess, "run") as run:
                    status, _ = self.run_cli(["--execute", "--acknowledge-existing-layout"])
                self.assertEqual(status, 2)
                run.assert_not_called()

    def test_changed_binary_rejected_even_when_manifest_hash_is_changed(self):
        path = self.package / "digivice.bin"
        data = bytearray(path.read_bytes())
        data[-1] ^= 1
        path.write_bytes(data)
        for rewrite_manifest in (False, True):
            if rewrite_manifest:
                self.manifest(lambda x: x["images"]["app"].update(sha256=helper.sha(data)))
            with patch.object(helper.subprocess, "run") as run:
                status, _ = self.run_cli()
            self.assertEqual(status, 2)
            run.assert_not_called()

    def test_symlinked_binary_and_missing_image_rejected(self):
        path = self.package / "digivice.bin"
        path.unlink()
        self.assertEqual(self.run_cli()[0], 2)
        path.symlink_to(self.base / "digivice.bin")
        self.assertEqual(self.run_cli()[0], 2)

    def test_malformed_manifest_fails_cleanly(self):
        for content in ("[]", "{broken", "{}"):
            with self.subTest(content=content):
                (self.package / "manifest.json").write_text(content)
                with patch.object(helper.subprocess, "run") as run:
                    status, output = self.run_cli()
                self.assertEqual(status, 2)
                self.assertIn("Stopped:", output)
                run.assert_not_called()

    def test_invalid_port_rejected_without_subprocess(self):
        with patch.object(helper.subprocess, "run") as run, contextlib.redirect_stderr(io.StringIO()):
            status = helper.main(["flash", "--package", str(self.package), "--port", "socket://example:80"])
        self.assertEqual(status, 2)
        run.assert_not_called()

    def test_ports_only_globs_never_opens_or_executes(self):
        with patch.object(helper.glob, "glob", return_value=["/dev/cu.TEST_ONLY"]), \
                patch.object(helper.subprocess, "run") as run, \
                patch("builtins.open", side_effect=AssertionError("ports must not open files")), \
                contextlib.redirect_stdout(io.StringIO()) as out:
            self.assertEqual(helper.main(["ports"]), 0)
        self.assertEqual(out.getvalue().strip(), "/dev/cu.TEST_ONLY")
        run.assert_not_called()

    def test_existing_output_is_not_overwritten(self):
        sentinel = self.package / "keep.txt"
        sentinel.write_text("keep")
        with self.assertRaises(FileExistsError):
            helper.package(self.build, self.package)
        self.assertEqual(sentinel.read_text(), "keep")

    def test_mutated_build_arguments_rejected_before_output_creation(self):
        build = self.directory / "build"
        build.mkdir()
        for name in [*helper.FILES.values(), "sdkconfig", "flasher_args.json"]:
            dest = build / name
            dest.parent.mkdir(exist_ok=True)
            shutil.copyfile(self.build / name, dest)
        path = build / "flasher_args.json"
        args = json.loads(path.read_text())
        args["app"]["offset"] = "0x9000"
        args["flash_files"]["0x9000"] = args["flash_files"].pop("0x20000")
        path.write_text(json.dumps(args))
        output = self.directory / "unmade"
        with self.assertRaises(helper.Invalid):
            helper.package(build, output)
        self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
