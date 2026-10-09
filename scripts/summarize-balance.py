#!/usr/bin/env python3
"""Aggregate native observations only. No gameplay or damage implementation."""
import collections
import hashlib
import json
import math
from pathlib import Path
import statistics
import subprocess
import sys

root = Path(__file__).resolve().parent.parent
raw = Path(sys.argv[1]).resolve()
destination = Path(sys.argv[2]).resolve()
rows = [json.loads(line) for line in raw.read_text().splitlines() if line]

def percentile(values, fraction):
    values = sorted(values)
    return values[max(0, math.ceil(len(values) * fraction) - 1)]

def aggregate(samples):
    result = {"runs": len(samples), "outcomes": dict(sorted(collections.Counter(r["outcome"] for r in samples).items())),
              "turnsMedian": statistics.median(r["turns"] for r in samples),
              "turnsP95": percentile([r["turns"] for r in samples], .95),
              "turnsMax": max(r["turns"] for r in samples),
              "meanRemainingHpFraction": round(statistics.mean(r["hp"] / r["initialHp"] for r in samples), 4),
              "choices": [sum(r["choices"][i] for r in samples) for i in range(len(samples[0]["choices"]))]}
    for key in ("energySpent", "recoveryRests", "xpAwarded"):
        if key in samples[0]:
            result["mean" + key[0].upper() + key[1:]] = round(statistics.mean(r[key] for r in samples), 3)
    return result

groups = collections.defaultdict(list)
overview = collections.defaultdict(list)
for row in rows:
    if row["kind"] not in ("practice", "wild"):
        continue
    key_fields = ("kind", "route", "lineage", "form", "level", "rival", "delta", "enemyLevel", "policy", "card", "full", "startEnergy")
    key = tuple((k, row[k]) for k in key_fields if k in row)
    groups[key].append(row)
    if row["delta"] == 0 and row["route"] == "rookie" and not row.get("card", 0) and not row.get("full", False) and row.get("startEnergy", 80) == 80:
        overview[(row["kind"], row["level"], row["policy"])].append(row)

source_files = ["scripts/balance-sim.cpp", "core/game.cpp", "core/game.hpp", "core/forms.cpp", "core/forms.hpp",
                "core/combat.cpp", "core/combat.hpp", "core/practice_battle.cpp", "core/practice_battle.hpp", "core/battle_trace.cpp"]
result = {
    "formatVersion": 1,
    "scope": "Native deterministic host simulation, not hardware, human-play, physical time, or statistical population estimates",
    "gitHeadAtRun": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
    "sourceSha256": {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in source_files},
    "rawSha256": hashlib.sha256(raw.read_bytes()).hexdigest(),
    "counts": dict(sorted(collections.Counter(r["kind"] for r in rows).items())),
    "policy": {
        "practiceAuto": "Native phase-only seeded policy; no cards, no hidden-intent access",
        "publicHint": "Greedy expected clipped HP swing over the two public possibilities using native resolveForms; fixed ties; no private choice or RNG",
        "wildGreedy": "Highest immediate native damage, prefer lower-energy move on ties; capture at first legal opportunity",
        "wildEconomy": "Best Physical/Magic native damage; no Heavy; capture at first legal opportunity",
        "fullCollection": "Validated full collection prevents native Auto capture; isolates defeat/recovery tradeoff",
        "progression": "Actual new-device hatch and native wins, recover with Rest to full HP and at least 80 energy, optional native evolution at first eligible branch"
    },
    "arrayOrder": {"damage": ["physical", "heavy", "magic"], "wildChoices": ["physical", "heavy", "magic", "capture"], "practiceChoices": ["physical", "heavy", "magic", "brace", "counter", "ward"]},
    "limitations": ["Sixteen deterministic seeds are a regression sample, not confidence intervals", "Fixture levels/forms are injected into validated native states for matchup isolation", "Original rival roots remain the rival form at every sampled level", "Tactical policy is reproducible, not optimal play", "Card policy uses the same greedy public-hint choice and does not optimize its prepared bonus", "No real seconds, GPS/IMU steps, hardware performance, network latency, or player engagement measured"],
    "overviewEqualLevelRookies": [dict(kind=k[0], level=k[1], policy=k[2], **aggregate(v)) for k, v in sorted(overview.items())],
    "matchups": [dict(key, **aggregate(value)) for key, value in sorted(groups.items())],
    "damageProbes": [r for r in rows if r["kind"] == "damage"],
    "profiles": [r for r in rows if r["kind"] == "profile"],
    "progression": [r for r in rows if r["kind"] == "progression"],
    "freeCare": [r for r in rows if r["kind"] == "free-care"],
}
destination.parent.mkdir(parents=True, exist_ok=True)
destination.write_text(json.dumps(result, indent=2) + "\n")
print(json.dumps({"result": "PASS", "counts": result["counts"], "report": str(destination.relative_to(root)), "bytes": destination.stat().st_size}, indent=2))
for row in result["overviewEqualLevelRookies"]:
    print(f"{row['kind']:8} L{row['level']:2} {row['policy']:11} n={row['runs']:3} {row['outcomes']} median/p95={row['turnsMedian']}/{row['turnsP95']} hp={row['meanRemainingHpFraction']:.3f}")
