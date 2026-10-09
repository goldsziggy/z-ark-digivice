#!/usr/bin/env python3
"""Acquire named sheets through visible Chrome controls, never HTTP/CDP/JS.

Run only while the approved Chrome window is on a known World DS asset page.
Sources are copied privately; acquisition does not create a frame map or an
integrated art pack. Original Downloads files are never moved or deleted.
"""
from __future__ import annotations

import argparse
import datetime as dt
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import stat
import struct
import subprocess
import sys
import time
import zlib

ROOT = Path(__file__).resolve().parents[1]
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
MAX_PNG_BYTES = 32 * 1024 * 1024
ASSET_URL = re.compile(r"https://(?:www\.)?spriters-resource\.com/ds_dsi/dgmnworldds/asset/([0-9]+)/?\Z")


class StopAcquisition(RuntimeError):
    pass


class PageNotReady(StopAcquisition):
    pass


def now():
    return dt.datetime.now(dt.timezone.utc).isoformat()


def progress(event, **fields):
    print(json.dumps({"event": event, **fields}, ensure_ascii=False), flush=True)


def decode_command_json(text):
    # Orca may print a platform diagnostic before its JSON. Never retain it.
    start = text.find("{")
    if start < 0:
        raise StopAcquisition("Orca returned no JSON object")
    try:
        value, _ = json.JSONDecoder().raw_decode(text[start:])
    except ValueError as error:
        raise StopAcquisition("Orca returned malformed JSON") from error
    if value.get("ok") is not True:
        error = value.get("error", {})
        code = error.get("code", "unknown") if isinstance(error, dict) else "unknown"
        raise StopAcquisition(f"Orca action failed ({code}); inspect the selected window manually")
    return value


def parse_page(document, inventory):
    tree = document.get("result", {}).get("snapshot", {}).get("treeText")
    if not isinstance(tree, str) or len(tree) > 1024 * 1024:
        raise PageNotReady("Missing or oversized accessibility tree")
    if re.search(r"(?:www\.)?spriters-resource\.com wants to:\s*Download multiple files", tree, re.I):
        raise StopAcquisition("Chrome requires site permission to download multiple files; no permission changed")
    if re.search(r"^\s*\d+\s+(?:dialog|sheet|alert)\b", tree, re.M):
        raise StopAcquisition("A dialog or permission prompt requires manual review")
    if re.search(r"verify (?:that )?you are human|checking your browser|access denied|captcha challenge|security verification", tree, re.I):
        raise StopAcquisition("A challenge or blocked page requires manual review")
    addresses = re.findall(r"^\s*\d+ text field[^\n]*Address and search bar, Value: ([^\n]+)", tree, re.M)
    if len(addresses) != 1:
        raise PageNotReady("Could not identify the selected window's address field")
    address = addresses[0].split(", Placeholder:", 1)[0].strip()
    if address.startswith(("spriters-resource.com/", "www.spriters-resource.com/")):
        address = "https://" + address
    match = ASSET_URL.fullmatch(address)
    if not match:
        raise StopAcquisition("The selected window is not a World DS asset page")
    url = f"https://www.spriters-resource.com/ds_dsi/dgmnworldds/asset/{match[1]}/"
    rows = {}
    for label in ["Name", "Game", "Section", "Uploaded By", "Contributors", "Size", "Format", "Credits", "Credit"]:
        values = re.findall(r"^\s*\d+ row " + re.escape(label) + r" ([^\n]+)$", tree, re.M)
        if len(values) > 1:
            raise StopAcquisition(f"Ambiguous {label} metadata")
        if values:
            rows[label] = values[0].strip()
    if not all(key in rows for key in ["Name", "Game", "Section", "Size", "Format"]) or not (rows.get("Uploaded By") or rows.get("Contributors")):
        raise PageNotReady("Asset metadata is still incomplete")
    if rows["Game"] != "Digimon World DS" or rows["Format"] != "PNG (image/png)":
        raise StopAcquisition("Wrong game or non-PNG asset")
    entry = inventory.get((rows["Name"], rows["Section"]))
    if entry is None:
        raise StopAcquisition(f"Unlisted asset: {rows['Name']} / {rows['Section']}")
    titles = re.findall(r"^([\t ]*)\d+ HTML content ([^\n]+)$", tree, re.M)
    # Advertising frames can have their own named HTML roots. Verify the
    # outermost page title, never a nested advertisement's title.
    depth = min((len(indent.expandtabs()) for indent, _ in titles), default=-1)
    title = [text for indent, text in titles if len(indent.expandtabs()) == depth]
    if len(title) != 1 or not title[0].startswith(f"{rows['Name']} - Digimon World DS - DS / DSi -"):
        raise PageNotReady("Asset name and current page title do not yet agree")
    size = re.fullmatch(r"([0-9]+(?:\.[0-9]+)?) (B|KB|MB) \(([0-9]+)\s*[x×]\s*([0-9]+)\)", rows["Size"])
    if not size:
        raise StopAcquisition("Unrecognized source size/dimensions metadata")
    width, height = int(size[3]), int(size[4])
    if not (1 <= width <= 32768 and 1 <= height <= 32768 and width * height <= 64 * 1024 * 1024):
        raise StopAcquisition("Source dimensions exceed acquisition bounds")
    links = {}
    for label in ["download", "arrow_forward", "arrow_back"]:
        matches = re.findall(r"^\s*([0-9]+) link " + label + r"\s*$", tree, re.M)
        if len(matches) > 1:
            raise StopAcquisition(f"Ambiguous {label} control")
        links[label] = int(matches[0]) if matches else None
    if links["download"] is None:
        raise PageNotReady("No actual Download link is currently present")
    return {"entryKey": entry["entryKey"], "formId": entry["formId"], "url": url,
            "name": rows["Name"], "section": rows["Section"], "uploadedBy": rows.get("Uploaded By"),
            "contributors": rows.get("Contributors"),
            "credit": rows.get("Credits", rows.get("Credit")), "sizeLabel": rows["Size"],
            "sizeValue": size[1], "sizeUnit": size[2], "width": width, "height": height, "links": links}


def png_info(path, page):
    before = path.lstat()
    if not stat.S_ISREG(before.st_mode) or before.st_size > MAX_PNG_BYTES or before.st_size < 57:
        raise StopAcquisition("Downloaded PNG is not a bounded regular file")
    with path.open("rb") as handle:
        data = handle.read(MAX_PNG_BYTES + 1)
    after = path.lstat()
    if (before.st_ino, before.st_size, before.st_mtime_ns) != (after.st_ino, after.st_size, after.st_mtime_ns):
        raise PageNotReady("PNG is still changing")
    if not data.startswith(PNG_SIGNATURE) or len(data) > MAX_PNG_BYTES:
        raise StopAcquisition("Downloaded file has no valid PNG signature")
    offset, dimensions, ended, has_image = 8, None, False, False
    while offset + 12 <= len(data):
        length = struct.unpack_from(">I", data, offset)[0]
        if length > MAX_PNG_BYTES or offset + 12 + length > len(data):
            raise PageNotReady("PNG is incomplete")
        tag = data[offset + 4:offset + 8]
        payload = data[offset + 8:offset + 8 + length]
        crc = struct.unpack_from(">I", data, offset + 8 + length)[0]
        if zlib.crc32(tag + payload) & 0xffffffff != crc:
            raise StopAcquisition("PNG chunk checksum mismatch")
        if offset == 8:
            if tag != b"IHDR" or length != 13:
                raise StopAcquisition("PNG has no valid first IHDR")
            dimensions = struct.unpack_from(">II", payload)
        if tag == b"IDAT":
            has_image = True
        offset += length + 12
        if tag == b"IEND":
            ended = length == 0 and offset == len(data)
            break
    if not ended or not has_image:
        raise PageNotReady("PNG has no complete image ending")
    if dimensions != (page["width"], page["height"]):
        raise StopAcquisition("PNG dimensions differ from the visible asset metadata")
    unit = {"B": 1, "KB": 1024, "MB": 1024 * 1024}[page["sizeUnit"]]
    decimals = len(page["sizeValue"].split(".", 1)[1]) if "." in page["sizeValue"] else 0
    tolerance = max(1, unit * 0.5 * 10 ** -decimals + 1)
    if abs(len(data) - float(page["sizeValue"]) * unit) > tolerance:
        raise StopAcquisition("PNG byte count differs from the displayed rounded size")
    return {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(),
            "width": dimensions[0], "height": dimensions[1]}, data


def matching_downloads(directory, page):
    stem = f"DS _ DSi - Digimon World DS - {page['section']} - {page['name']}"
    pattern = re.compile(re.escape(stem) + r"(?: \([0-9]+\))?\.png\Z")
    return sorted((path for path in directory.iterdir() if pattern.fullmatch(path.name)), key=lambda path: path.name)


def fingerprint(path):
    value = path.lstat()
    return value.st_ino, value.st_size, value.st_mtime_ns


def atomic_json(path, value):
    temporary = path.with_name(path.name + f".tmp-{os.getpid()}")
    with temporary.open("x", encoding="utf-8") as handle:
        json.dump(value, handle, ensure_ascii=False, indent=2)
        handle.write("\n"); handle.flush(); os.fsync(handle.fileno())
    os.replace(temporary, path)


class Browser:
    def __init__(self, executable, window_id, inventory, pause=3.0):
        self.command = shlex.split(executable)
        if not self.command:
            raise StopAcquisition("No Orca executable selected")
        self.window_id = window_id
        self.inventory = inventory
        self.pause = pause

    def call(self, operation, *arguments):
        command = [*self.command, "computer", operation, "--app", "com.google.Chrome", "--window-id", str(self.window_id), *arguments, "--no-screenshot", "--json"]
        try:
            result = subprocess.run(command, capture_output=True, text=True, timeout=45, check=False)
        except subprocess.TimeoutExpired as error:
            raise StopAcquisition("Orca operation exceeded45seconds; inspect Chrome before restarting") from error
        if result.returncode:
            raise StopAcquisition(f"Orca {operation} exited with status {result.returncode}; no automatic retry")
        return decode_command_json(result.stdout)

    def snapshot(self):
        return parse_page(self.call("get-app-state"), self.inventory)

    def ready_snapshot(self):
        deadline = time.monotonic() + 45
        while True:
            try:
                return self.snapshot()
            except PageNotReady:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(self.pause)

    def click(self, page, label):
        # Each input is selected from an immediately fresh snapshot, never an
        # index saved before a download, delay, or navigation.
        fresh = self.ready_snapshot()
        if (fresh["url"], fresh["entryKey"]) != (page["url"], page["entryKey"]):
            raise StopAcquisition("Page changed before input; no click sent")
        index = fresh["links"][label]
        if index is None:
            return False
        self.call("click", "--element-index", str(index))
        return True


def run(args):
    catalog_path = ROOT / "data/world-ds-catalog.json"
    raw_catalog = catalog_path.read_bytes()
    catalog = json.loads(raw_catalog)
    inventory = {(entry["source"]["listedName"], entry["source"]["section"]): entry for entry in catalog["entries"]}
    if len(inventory) != 255 or len(catalog["entries"]) != 255:
        raise StopAcquisition("Expected255 distinct known source sheets")
    source = ROOT / ".personal-assets/world-ds/source"
    source.mkdir(parents=True, exist_ok=True)
    if source.is_symlink():
        raise StopAcquisition("Private source directory must not be a symlink")
    manifest_path = source / "acquisition.json"
    lock = (source / ".acquisition.lock").open("a+")
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError as error:
        raise StopAcquisition("Another acquisition runner already holds this source directory") from error
    manifest = json.loads(manifest_path.read_text()) if manifest_path.exists() else {
        "formatVersion": 1, "kind": "private-source-acquisition", "integrated": False, "entries": {}}
    if manifest.get("formatVersion") != 1 or manifest.get("kind") != "private-source-acquisition" or not isinstance(manifest.get("entries"), dict):
        raise StopAcquisition("Unsupported acquisition manifest; kept unchanged")
    manifest["catalogSha256"] = hashlib.sha256(raw_catalog).hexdigest()
    manifest["lastRun"] = {"startedAt": now(), "status": "running", "direction": args.direction, "visited": [], "acquired": 0}
    atomic_json(manifest_path, manifest)
    browser = Browser(args.orca, args.window_id, inventory, args.pause)
    downloads = args.downloads.expanduser().resolve()
    visited = set()
    current = None

    def save():
        manifest["updatedAt"] = now()
        atomic_json(manifest_path, manifest)

    def adopt(path, page):
        info, data = png_info(path, page)
        destination = source / f"{page['entryKey']}.png"
        if destination.exists():
            existing, _ = png_info(destination, page)
            if existing["sha256"] != info["sha256"]:
                raise StopAcquisition("Existing private source differs; no overwrite performed")
        else:
            # Exclusive creation prevents silently replacing a source acquired
            # by an earlier interrupted run. Original Download stays untouched.
            with destination.open("xb") as handle:
                handle.write(data); handle.flush(); os.fsync(handle.fileno())
        copied, _ = png_info(destination, page)
        if copied != info:
            raise StopAcquisition("Private source copy verification failed")
        record = {key: page[key] for key in ["entryKey", "formId", "url", "name", "section", "uploadedBy", "contributors", "credit", "sizeLabel"]}
        record.update(info, file=destination.name, acquiredAt=now(), status="acquired", integrated=False)
        manifest["entries"][page["entryKey"]] = record
        manifest["lastRun"]["acquired"] += 1
        save()
        progress("acquired", entryKey=page["entryKey"], bytes=info["bytes"], total=len(manifest["entries"]), integrated=False)

    try:
        current = browser.ready_snapshot()
        for _ in range(args.max_visits):
            if current["url"] in visited:
                manifest["lastRun"]["status"] = "stopped-visited-url"
                break
            visited.add(current["url"])
            manifest["lastRun"]["visited"].append(current["url"])
            record = manifest["entries"].get(current["entryKey"])
            if record:
                info, _ = png_info(source / f"{current['entryKey']}.png", current)
                if record.get("url") != current["url"] or any(record.get(key) != info[key] for key in ["bytes", "sha256", "width", "height"]):
                    raise StopAcquisition("Manifest/source/page mismatch; no source replaced")
                progress("already-acquired", entryKey=current["entryKey"], total=len(manifest["entries"]))
            else:
                paths = matching_downloads(downloads, current)
                # These three reviewed sources predate browser acquisition.
                # Reuse them after the same fresh page/PNG checks instead of
                # requesting another network download under a different name.
                if current["entryKey"] in {"agumon", "greymon", "metalgreymon"}:
                    prior = ROOT / ".personal-assets/source" / f"{current['entryKey']}.png"
                    if prior.exists():
                        paths.append(prior)
                # A completed exact-name existing file can be adopted after the
                # same metadata, PNG CRC, dimensions and copy-hash checks.
                if paths:
                    verified = [(path, png_info(path, current)[0]) for path in paths]
                    if len({info["sha256"] for _, info in verified}) != 1:
                        raise StopAcquisition("Conflicting exact-name downloads require manual review")
                    adopt(verified[0][0], current)
                else:
                    before = {path.name: fingerprint(path) for path in paths}
                    if not browser.click(current, "download"):
                        raise StopAcquisition("Fresh Download link disappeared")
                    progress("download-clicked", entryKey=current["entryKey"])
                    deadline = time.monotonic() + 45
                    downloaded = None
                    while time.monotonic() < deadline:
                        time.sleep(args.pause)
                        try:
                            state = browser.snapshot()
                        except PageNotReady:
                            # Chrome can temporarily omit the address/metadata
                            # while announcing a finished download. Wait within
                            # the same deadline; permission/challenge errors
                            # remain fatal and never trigger another click.
                            continue
                        if (state["url"], state["entryKey"]) != (current["url"], current["entryKey"]):
                            raise StopAcquisition("Download changed the selected page unexpectedly")
                        candidates = [path for path in matching_downloads(downloads, current) if before.get(path.name) != fingerprint(path)]
                        for candidate in candidates:
                            try:
                                png_info(candidate, current)
                            except PageNotReady:
                                continue
                            downloaded = candidate
                            break
                        if downloaded:
                            break
                    if downloaded is None:
                        raise StopAcquisition("No complete matching PNG arrived within45seconds")
                    adopt(downloaded, current)
            save()
            if len(manifest["entries"]) == 255:
                manifest["lastRun"]["status"] = "complete"
                break
            if manifest["lastRun"]["acquired"] >= args.max_sheets:
                manifest["lastRun"]["status"] = "stopped-sheet-limit"
                break
            if len(visited) >= args.max_visits:
                manifest["lastRun"]["status"] = "stopped-visit-limit"
                break
            time.sleep(args.pause)
            label = "arrow_forward" if args.direction == "forward" else "arrow_back"
            if not browser.click(current, label):
                manifest["lastRun"]["status"] = "stopped-no-navigation-link"
                break
            previous = current
            deadline = time.monotonic() + 45
            while time.monotonic() < deadline:
                time.sleep(args.pause)
                try:
                    candidate = browser.snapshot()
                except PageNotReady:
                    continue
                if candidate["url"] != previous["url"] and candidate["entryKey"] != previous["entryKey"]:
                    current = candidate
                    break
            else:
                raise StopAcquisition("Navigation did not reach a different verified sheet within45seconds")
        manifest["lastRun"]["finishedAt"] = now()
        save()
        progress("done", status=manifest["lastRun"]["status"], acquired=manifest["lastRun"]["acquired"], total=len(manifest["entries"]), manifest=str(manifest_path))
        return 0
    except (StopAcquisition, OSError, ValueError, KeyboardInterrupt) as error:
        manifest["lastRun"].update(status="stopped-error", finishedAt=now(), error=str(error) or "Interrupted",
                                   entryKey=current["entryKey"] if current else None)
        save()
        progress("stopped", reason=str(error) or "Interrupted", acquired=manifest["lastRun"]["acquired"], total=len(manifest["entries"]))
        return 1
    finally:
        lock.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    default_orca = os.environ.get("ORCA_CLI_COMMAND") or ("orca-dev" if os.environ.get("ORCA_DEV_REPO_ROOT") else "orca")
    parser.add_argument("--orca", default=default_orca)
    parser.add_argument("--window-id", type=int, default=10852)
    parser.add_argument("--downloads", type=Path, default=Path.home() / "Downloads")
    parser.add_argument("--max-sheets", type=int, default=255)
    parser.add_argument("--max-visits", type=int, default=320)
    parser.add_argument("--direction", choices=["forward", "back"], default="forward")
    parser.add_argument("--pause", type=float, default=3.0)
    args = parser.parse_args()
    if not (1 <= args.max_sheets <= 255 and 1 <= args.max_visits <= 320 and 3 <= args.pause <= 10 and args.window_id > 0):
        parser.error("Require1..255 sheets,1..320 visits,3..10second pause,and a positive window ID")
    try:
        return run(args)
    except (StopAcquisition, OSError, ValueError) as error:
        progress("stopped", reason=str(error))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
