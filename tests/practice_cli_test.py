"""Exercise the actual practice executable; no game rules implemented in Python."""
import base64
import json
import struct
import subprocess
import sys
import zlib
from pathlib import Path

binary = sys.argv[1]


def run(*args, success=True, automatic=False):
    result = subprocess.run([binary, *args], capture_output=True, check=False)
    if not success:
        assert result.returncode != 0 and result.stdout == b"" and result.stderr
        return
    assert result.returncode == 0 and result.stderr == b"", result.stderr
    assert len(result.stdout.splitlines()) == 1
    envelope = json.loads(result.stdout)
    expected_keys = {"state", "snapshotBase64"}
    if automatic:
        expected_keys |= {"mode", "initialSnapshotBase64", "trace"}
        assert envelope["mode"] == "auto"
    assert set(envelope) == expected_keys
    state = envelope["state"]
    assert not ({"rngState", "seed", "enemyChoice", "excludedChoice", "snapshotBase64"} & state.keys())
    return envelope


fixtures = json.loads((Path(__file__).parent / "fixtures/practice-v2.json").read_text())
fixtures += json.loads((Path(__file__).parent / "fixtures/practice-v3.json").read_text())
fixtures += json.loads((Path(__file__).parent / "fixtures/practice-v4.json").read_text())
fixtures += json.loads((Path(__file__).parent / "fixtures/practice-v5.json").read_text())
fixtures += json.loads((Path(__file__).parent / "fixtures/practice-v6.json").read_text())
# Captured by the pre-RPG executable. Reading, applying and replaying these
# snapshots must preserve every public field and byte, not just final HP.
for fixture in fixtures:
    assert run("--practice-read", fixture["start"]["snapshotBase64"]) == fixture["start"]
    assert run("--practice-act", fixture["start"]["snapshotBase64"], "card", "2") == fixture["card"]
    assert run("--practice-act", fixture["card"]["snapshotBase64"], "heavy") == fixture["turn"]
    assert run("--practice-auto", fixture["start"]["snapshotBase64"], automatic=True) == fixture["automatic"]
    if "magic" in fixture:
        assert run("--practice-act", fixture["start"]["snapshotBase64"], "magic") == fixture["magic"]
        assert run("--practice-act", fixture["start"]["snapshotBase64"], "card", "1") == fixture["spark"]
    assert run("--practice-read", fixture["automatic"]["snapshotBase64"]) == {key: fixture["automatic"][key] for key in ["state", "snapshotBase64"]}
start = run("--practice-read", fixtures[0]["start"]["snapshotBase64"])
assert start["state"]["enemyHint"] == ["brace", "ward"]
assert start["state"]["playerHp"] == 100 and start["state"]["enemyHp"] == 88
assert start["state"]["schemaVersion"] == start["state"]["rulesVersion"] == 2
assert start["state"]["playerCombat"] == {"maxHp": 100, "attack": 18, "defense": 14, "magic": 16,
    "resistance": 16, "type": "grove", "skills": {"physical": "Twig Tap", "heavy": "Root Ram", "magic": "Seed Spark"}}
assert start["state"]["enemySpecies"] == "flicker" and start["state"]["enemyCombat"]["type"] == "neutral"
assert run("--practice-read", start["snapshotBase64"]) == start
card = run("--practice-act", start["snapshotBase64"], "card", "1")
assert card["state"]["enemyHint"] == start["state"]["enemyHint"]
assert card["state"]["exchanges"] == 0 and card["state"]["sequence"] == 1
run("--practice-act", card["snapshotBase64"], "card", "2", success=False)
hit = run("--practice-act", card["snapshotBase64"], "physical")
assert hit["state"]["phase"] == "defend"
assert hit["state"]["lastTurn"] == {"phase": "attack", "playerChoice": "physical",
    "enemyChoice": "ward", "playerDamage": 0, "enemyDamage": 19, "reflected": False}
assert hit == run("--practice-act", card["snapshotBase64"], "physical")
run("--practice-act", hit["snapshotBase64"], "physical", success=False)
ended = run("--practice-act", hit["snapshotBase64"], "retreat")
assert ended["state"]["status"] == "retreated" and ended["state"]["enemyHint"] == []
run("--practice-act", ended["snapshotBase64"], "brace", success=False)
for action, value in [("capture", "0"), ("card", "0"), ("card", "3"), ("physical", "1"), ("magic", "-1")]:
    run("--practice-act", start["snapshotBase64"], action, value, success=False)
for value in ["-1", "4294967296", "1.5", ""]:
    run("--practice-start", value, "mote", "1", "flicker", "1", success=False)
for player, level, enemy, enemy_level in [("mote", "0", "flicker", "1"), ("mote", "99", "flicker", "1"),
    ("mote", "51", "flicker", "1"), ("mote", "1", "flicker", "51"), ("unknown", "1", "rill", "1")]:
    run("--practice-start", "0", player, level, enemy, enemy_level, success=False)
run("--practice-start", "12345", "1", success=False)  # Old ambiguous argument contract is not reused.
evolved = run("--practice-read", fixtures[1]["start"]["snapshotBase64"])["state"]
assert evolved["playerHp"] == 136 and evolved["enemyHp"] == 88 and evolved["enemyLevel"] == 1
rival = run("--practice-start", "12345", "rill", "2", "cinder", "1")["state"]
assert rival["playerCombat"]["type"] == "tide" and rival["enemyCombat"]["type"] == "ember"
snapshot = base64.b64decode(start["snapshotBase64"], validate=True)
assert len(snapshot) == 112 and snapshot[:4] == b"DGBP"
assert struct.unpack_from("<HHI", snapshot, 4) == (2, 100, 2)
assert struct.unpack_from("<I", snapshot, 108)[0] == zlib.crc32(snapshot[:108])
for bad in ["", "!", start["snapshotBase64"][:-1], base64.b64encode(snapshot + b"\0").decode()]:
    run("--practice-read", bad, success=False)
# CRC-valid impossible/narrowing-overflow fields are rejected, not truncated.
for offset, value in [(8, 3), (28, 89), (40, 999), (44, 256), (56, 256), (60, 6), (92, 2), (96, 0), (100, 999), (104, 2)]:
    corrupt = bytearray(snapshot)
    struct.pack_into("<I", corrupt, offset, value)
    struct.pack_into("<I", corrupt, 108, zlib.crc32(corrupt[:108]))
    run("--practice-read", base64.b64encode(corrupt).decode(), success=False)

# Actual prior executable fixture: Lv.2, Spark, physical, brace; HP80/79.
legacy = "REdCUAEAWAABAAAAAwAAAGISuMMCAAAAUAAAAE8AAAACAAAAAAAAAAAAAAABAAAAAAAAAAAAAAAGAAAABAAAAAYAAAACAAAABAAAAAIAAAAUAAAAAAAAAAAAAAABAAAAb/kqqQ=="
run("--practice-read", legacy, success=False)
run("--practice-act", legacy, "magic", success=False)
migrated = run("--practice-migrate-v1", legacy, "mote", "2")
assert migrated["state"]["playerHp"] == 93 and migrated["state"]["enemyHp"] == 70
assert migrated["state"]["playerLevel"] == 2 and migrated["state"]["enemyLevel"] == 1
assert migrated["state"]["sequence"] == 3 and migrated["state"]["exchanges"] == 2 and migrated["state"]["cardUsed"]
assert migrated["state"]["lastTurn"]["playerDamage"] == 20
assert migrated["state"]["enemyHint"] == ["counter", "ward"]
old_bytes = base64.b64decode(legacy)
new_bytes = base64.b64decode(migrated["snapshotBase64"])
assert new_bytes[16:20] == old_bytes[16:20]  # Private RNG unchanged.
assert new_bytes[56:64] == old_bytes[56:64]  # Existing hidden commitment and hint unchanged.
assert run("--practice-read", migrated["snapshotBase64"]) == migrated
for species, level in [("mote", "1"), ("flicker", "2"), ("unknown", "2")]:
    run("--practice-migrate-v1", legacy, species, level, success=False)
run("--practice-migrate-v1", migrated["snapshotBase64"], "mote", "2", success=False)
for offset in [0, 4, 8, 16, 24, 56, 99]:
    corrupt = bytearray(old_bytes); corrupt[offset] ^= 1
    run("--practice-migrate-v1", base64.b64encode(corrupt).decode(), "mote", "2", success=False)

# Old initial snapshots keep the exact frozen Tactical resolver and Auto trace.
automatic = run("--practice-auto", start["snapshotBase64"], automatic=True)
assert automatic == run("--practice-auto", start["snapshotBase64"], automatic=True)
assert automatic["initialSnapshotBase64"] == start["snapshotBase64"]
assert automatic == run("--practice-auto", automatic["initialSnapshotBase64"], automatic=True)
final = automatic["state"]
trace = automatic["trace"]
assert final["phase"] == "finished" and final["status"] in {"won", "lost", "draw"}
assert final["cardUsed"] is False and final["enemyHint"] == []
assert set(trace) == {"formatVersion", "mode", "kind", "startSequence", "endSequence", "outcome", "player", "enemy", "steps"}
assert trace["formatVersion"] == 1 and trace["mode"] == "auto" and trace["kind"] == "practice"
assert trace["outcome"] == final["status"] and trace["startSequence"] == 0
assert trace["endSequence"] == final["sequence"] == len(trace["steps"]) == final["exchanges"]
assert 1 <= len(trace["steps"]) <= 30
assert trace["player"] == {"species": "mote", "name": "Mote", "level": 1, "combat": start["state"]["playerCombat"]}
assert trace["enemy"] == {"species": "flicker", "name": "Flicker", "level": 1, "combat": start["state"]["enemyCombat"]}
hp = (100, 88)
for index, event in enumerate(trace["steps"]):
    assert set(event) == {"turn", "phase", "action", "opponentAction", "playerHpBefore", "playerHpAfter", "enemyHpBefore", "enemyHpAfter", "reflected", "captured"}
    assert event["turn"] == index + 1 and event["captured"] is False
    assert event["phase"] == ("attack" if index % 2 == 0 else "defend")
    assert (event["playerHpBefore"], event["enemyHpBefore"]) == hp
    hp = (event["playerHpAfter"], event["enemyHpAfter"])
    assert event["action"] in ({"physical", "heavy", "magic"} if index % 2 == 0 else {"brace", "counter", "ward"})
    assert event["opponentAction"] in ({"brace", "counter", "ward"} if index % 2 == 0 else {"physical", "heavy", "magic"})
assert hp == (final["playerHp"], final["enemyHp"])
assert len(json.dumps(trace).encode()) < 16384
assert run("--practice-read", automatic["snapshotBase64"]) == {"state": final, "snapshotBase64": automatic["snapshotBase64"]}
for snapshot in [card["snapshotBase64"], hit["snapshotBase64"], automatic["snapshotBase64"]]:
    run("--practice-auto", snapshot, success=False)
run("--practice-act", automatic["snapshotBase64"], "physical", success=False)
for mode in ["AUTO", "unknown", ""]:
    run("--practice-start", "12345", "mote", "1", "flicker", "1", mode, success=False)
# New battles freeze explicit forms and independent RPG levels.
current = run("--practice-start", "12345", "mote", "1", "flicker", "1")
assert current == run("--practice-start", "12345", "mote", "1", "flicker", "1", "tactical")
assert current["state"]["rulesVersion"] == current["state"]["schemaVersion"] == 7
assert len(base64.b64decode(current["snapshotBase64"])) == 120
assert current["state"]["maxExchanges"]==40
assert all(("maxExchanges" in f["start"]["state"]) == (f["start"]["state"]["rulesVersion"] >= 5) for f in fixtures)
assert current["state"]["playerFormId"] > 0 and current["state"]["playerFormName"] == "Mote"
current_auto = run("--practice-start", "12345", "mote", "1", "flicker", "1", "auto", automatic=True)
assert current_auto == run("--practice-auto", current["snapshotBase64"], automatic=True)
form = run("--practice-start-forms", "876", "renamon", "20", "66", "flicker", "1", "initial")
assert form["state"]["playerFormId"] == 66 and form["state"]["playerLevel"] == 20
assert form["state"]["enemyFormId"] > 0 and form["state"]["enemyLevel"] == 1
assert form["state"]["playerSpecies"] == "renamon" and form["state"]["playerFormName"] != "Renamon"
form_auto = run("--practice-auto", form["snapshotBase64"], automatic=True)
assert form_auto["trace"]["player"]["name"] == form["state"]["playerFormName"]
assert form_auto["trace"]["player"]["combat"] == form["state"]["playerCombat"]
assert form_auto["state"]["playerLevel"] == 20 and form_auto["state"]["playerFormId"] == 66
for player, level, form_id in [("agumon", "20", "66"), ("renamon", "1", "66"), ("renamon", "51", "66"), ("renamon", "20", "0"), ("renamon", "20", "67")]:
    run("--practice-start-forms", "1", player, level, form_id, "flicker", "1", "initial", success=False)
for offset, value in [(4, 8), (8, 2), (108, 66), (112, 0), (104, 51)]:
    corrupt = bytearray(base64.b64decode(current["snapshotBase64"]))
    struct.pack_into("<I", corrupt, offset, value)
    struct.pack_into("<I", corrupt, 116, zlib.crc32(corrupt[:116]))
    run("--practice-read", base64.b64encode(corrupt).decode(), success=False)
print("Practice CLI: frozen v2 fixtures/replay, explicit v1 migration, rules7 forms/levels, frozen v3/v4/v5/v6 replay, 112/120-byte snapshots, privacy, bounded Auto and malformed inputs passed")
