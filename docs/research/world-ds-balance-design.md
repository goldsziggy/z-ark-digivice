# Digimon World DS roster: authored balance design

Status: **implemented catalog schema and generated native profiles**. Baseline: `916c0d8`. The reviewed mapping yields 276 forms: the exact preserved 66 plus 210 additions, representing all 255 individually named DS entries. Numeric stats and skill labels are authored game content, not official franchise stats. Native catalog checks and target object measurements are recorded in verification evidence (historical local evidence omitted); they are not physical device tests or a claim of completed combat balance.

## Scope and identity

The [inventory](world-ds-inventory.json) identifies **255 individually named Digimon sheets**: Fresh 4, In-Training 7, Rookie 49, Champion 60, Ultimate 58, Mega 68, Armor 8, and No Level 1 (Calumon). The separate `NPC Digimon` group is unresolved content, not one additional species or proof that the complete game roster is 256. Character, background, and miscellaneous sheets are outside this species count. These are source-page categories; official canonical stages require separate verification.

Keep each named sheet's inventory identity, including color, mode, Dot, and naming variants. Do not silently merge sheets because two names may be aliases. Inventory identity, canonical species identity, authored combat form, and an available appearance asset are separate mappings.

Existing form IDs **1–66 are immutable**. An exact existing-form match reuses that form and its complete profile. A distinct visual/mode entry keeps its own inventory identity; sharing a combat profile must be an explicit reviewed binding, not automatic name matching. Assign new form IDs only after the inventory mapping is reviewed, append IDs rather than renumbering, and use `uint16_t` for form/lineage identifiers. The conservative union bound is 66 + 255 = 321 forms before verified overlaps; current eight-bit fields are insufficient. Do not infer evolution edges or lineage from sheet order, names, or stage.

## Compatibility decision

**Grandfather all existing 66 profiles indefinitely unless an explicit future rules migration changes them.** Preserve their names, type, five-stat curves at every legal level, existing skill labels, and evolution mappings. Do not normalize their numbers as part of this roster expansion. An overlapping DS entry can reuse these profiles immediately; variation belongs to a genuinely new authored profile with its own stable identity.

The current shared resolver and Dokapon-inspired Practice counter matrix stay unchanged in this proposal. New roster metadata must not alter an already-started duel or a saved Auto replay. If later tuning changes an old profile, freeze the old table/resolver under its existing rules epoch before enabling the replacement. A catalog revision alone is insufficient: saved state needs to identify the applicable combat rules. Legacy decoders must retain their original ID bounds, even after the current catalog grows.

The previous native audit found 100% wins for a shallow public-hint Practice policy versus 83.5% for phase-only Auto across the sampled equal-level Rookies. That demonstrates the value of public information against the sampled rivals; it is not a target to force either policy toward 50%. Likewise, capture success is distinct from winning a fight to zero enemy HP. Keep those objectives and their resource costs separate.

## Five stats, display stage, and combat tier

Use exactly **HP, Attack, Defense, Magic, Resistance**. No Speed, accuracy, critical hits, status engine, per-species AI, or new counter system is implied. Creature type remains an explicitly authored choice among the current `grove`, `tide`, `ember`, and `neutral`; canonical Vaccine/Data/Virus attributes are not interchangeable with those mechanics.

Preserve `sourceStage` exactly. Keep `canonicalStage` nullable until verified. Every playable authored profile separately declares a `combatTier`; the generator never guesses power from a species name or an unusual display stage.

Proposed anchors for **new profiles only**:

| Combat tier | Anchor level | HP | Attack | Defense | Magic | Resistance | Weighted budget |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Fresh | 1 | 64 | 8 | 6 | 8 | 6 | 44 |
| In-Training | 1 | 80 | 12 | 9 | 12 | 9 | 62 |
| Rookie | 1 | 104 | 18 | 14 | 18 | 13 | 89 |
| Champion | 5 | 136 | 28 | 24 | 29 | 25 | 140 |
| Ultimate | 10 | 176 | 44 | 38 | 45 | 39 | 210 |
| Mega | 15 | 216 | 60 | 54 | 62 | 56 | 286 |

Budget accounting is `HP / 4 + Attack + Defense + Magic + Resistance`. It is an authoring constraint, **not an extra combat formula or a guarantee of equal win rates**. The existing eight Rookie starter anchors each have budget 89, providing a useful compatibility reference. Existing profiles outside this proposed budget remain unchanged.

Armor gets an explicit combat-tier assignment; use Champion as the initial authored default, subject to individual review. It remains labeled Armor. No Level gets **no inferred default strength**: author an explicit tier and role for Calumon, with a reason. A low-tier companion profile is a reasonable candidate, not a canonical claim. Neither category automatically becomes Mega-strength. The grouped NPC sheet remains outside playable metadata until its individual contents are identified.

Fresh/In-Training anchors are stat anchors, not a decision to replace the current eight-choice onboarding or invent evolution routes. The existing level cap remains 20. Evolution unlock levels/bond requirements belong to reviewed edges; the current 5/10/15 and 20/50/80 gates can remain for appropriate existing routes.

## Roles with explicit weaknesses

Apply one role allocation to a new stage anchor. In the next table HP is measured in **four-HP units**; every row sums to zero. Multiply the allocation by tier factor 1 for Fresh/In-Training/Rookie, 2 for Champion, 3 for Ultimate, or 4 for Mega.

| Role | HP units | Attack | Defense | Magic | Resistance | Intended distinction |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| Balanced | 0 | 0 | 0 | 0 | 0 | Two viable offensive stats, no special wall |
| Striker | 0 | +4 | 0 | −3 | −1 | Strong physical offense, weaker magic offense/resistance |
| Mystic | 0 | −3 | −1 | +4 | 0 | Strong magic offense, weaker physical offense/defense |
| Bulwark | +2 | −2 | +3 | −2 | −1 | Physical wall with a relative magic vulnerability |
| Warden | +2 | −2 | −1 | −2 | +3 | Magic wall with a relative physical vulnerability |

Unlike merely increasing both defensive stats together, Bulwark and Warden provide opposing Defense/Resistance profiles. Their success criterion is that some otherwise similar attackers change their preferred basic move between those opponents. Not every specialist must reverse preference; preserving strong species identity is also useful.

Proposed integer growth per level after the anchor:

| Role | HP | Attack | Defense | Magic | Resistance | Weighted growth |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Balanced | 4 | 2 | 1 | 2 | 1 | 7 |
| Striker | 4 | 3 | 1 | 1 | 1 | 7 |
| Mystic | 4 | 1 | 1 | 3 | 1 | 7 |
| Bulwark | 8 | 1 | 2 | 1 | 1 | 7 |
| Warden | 8 | 1 | 1 | 1 | 2 | 7 |

For new profiles, the native result is `anchor + roleAllocation + speciesVariation + growth * (level - anchorLevel)`. All values are integers and generated/validated before compilation. Invalid levels below an anchor reject; they do not silently clamp to a stronger form.

Allow a small authored `speciesVariation`: HP variation within ±1 four-HP unit, each other stat within ±2, and weighted sum exactly zero. Variation is chosen for the species, not randomized at boot/capture, and does not change its role growth. Exact repeated stats are acceptable for deliberate equivalent variants; avoid adding arbitrary noise merely to make every numeric row unique. Any larger exception must have an explicit rationale and a separately reviewed native matchup result.

These new budgets are candidates requiring simulation. Equal weighted budgets do not make HP and defensive stats strategically interchangeable: the actual subtractive resolver, type scaling, minimum damage, and integer rounding still determine fights. No old profile is changed to satisfy a budget.

## Per-species metadata contract

Author one reviewable record per inventory entry. A compact schema shape is:

```text
entryKey             stable key from verified inventory
formId               stable uint16; reuse only an explicitly matched existing form
displayName          exact source name, with reviewed display aliases separate
sourceStage          exact inventory category
canonicalStage       verified stage or null, with source reference when present
lineageId            stable uint16 assigned by reviewed gameplay mapping
canonicalSpeciesKey  optional alias/grouping metadata; does not merge inventory rows
profileBinding       preserved(formId, rulesEpoch)
                     OR authored(combatTier, role, type, speciesVariation)
skills               {physical: label, heavy: label, magic: label}
skillLabelOrigin     preserved | authored | verified-canonical
appearance           explicit inventory/asset reference or unavailable
review               source references, balance revision, author/reviewer status
```

The preserved and authored branches are mutually exclusive. Preserved bindings do not carry overriding growth, base stats, or renamed skills. New profile base/growth values are generated from the declared tier/role/variation rather than manually copied into hundreds of independent rows. Keep optional canonical/authoring provenance in the host metadata, not repeated inside every device save.

Each new species receives **three distinct, species-specific skill labels**, assigned to the existing Physical, Heavy, and Magic mechanics. These are presentation labels, not three custom combat implementations. New labels should be meaningfully authored from the species concept and avoid generic `Species Attack 1` filler. Treat them as authored fiction unless a canonical skill is actually verified. Heavy's label must still read as a committed physical move, and Magic's label as the magic-stat move.

Proposed limits: display name ≤64 UTF-8 bytes; each skill label ≤32 UTF-8 bytes; no empty strings, control characters, embedded NUL, or leading/trailing whitespace. Reject duplicate labels within a species. Detect case-folded duplicate new labels across the catalog and require a deliberate reuse justification; preserve existing duplicate labels in the frozen 66 rather than rewriting old content. Every authored row must explicitly supply all three labels before it becomes playable.

Evolution edges are separate records with source/rationale and unlock conditions. No mass generation of supposedly canonical routes from the roster list. If a role-changing or delayed evolution trades away a stat, the preview must show that change honestly; do not assume every form switch increases all five values.

## Generated C++ data and resource budget

The generator emits immutable `constexpr` metadata/profile rows. The compiler pools string literals; there is no per-species heap allocation, runtime JSON parsing, or mutable profile cache. The runtime retains one shared resolver and the existing counter matrix. Base/growth rows derive a level's five stats with bounded integer arithmetic. Expanded legal-level stats live only in the host runtime JSON for the service; they are not copied into the firmware table.

Measured generated data and bounds, **not complete ESP application linker usage**:

| Data | Conservative bound / estimate |
| --- | --- |
| Distinct forms after reviewed overlap mapping | 276; all 255 named inventory entries map once; unresolved NPC contents excluded |
| IDs | uint16; zero remains invalid; legacy IDs preserved |
| Form rows | ESP32-S3 object: 84 bytes ×276 =23,184 bytes; 64-bit host: 112 bytes each |
| Inventory mapping rows | ESP32-S3 object: 16 bytes ×255 =4,080 bytes; 64-bit host: 32 bytes each |
| Constant object data, including strings | 46,565 bytes in all `.rodata` sections; the compiler's general string section contributes 19,024 bytes |
| Mutable object data | `.data` 0 bytes; `.bss` 0 bytes; no heap allocation in the catalog module |
| Host runtime JSON | 883,776 bytes; includes every legal-level profile for service previews, never embedded in the device |
| Stats | Existing uint32 fields retained; generator rejects HP outside 1–512 or other stats outside 1–128 |
| Runtime workspace | No full catalog copy; one profile plus existing battle/save buffers; native catalog writers cap at 6,144 bytes |

The earlier ≤40-byte compact-row aspiration was not met: preserving the existing pointer-based `Form` structure keeps integration small, and its measured ESP row is 84 bytes. This is an explicit tradeoff, not an unmeasured claim of compression. The object was built with the installed ESP32-S3 GCC 13.2.0 toolchain using `-Os`; its code is 799 bytes plus 76 literal bytes. Final linker string merging, section removal, and application placement can change the resulting flash size. These measurements do not establish free heap or brownout behavior.

The generated roster's actual legal-level maxima are HP 288 / Attack 91 / Defense 76 / Magic 93 / Resistance 78. The permitted variation envelope would keep HP ≤292 and the other stats ≤95. The independent growth audit (historical local evidence omitted) found no new form dominating a preserved franchise form in all five stats across 920 same-stage comparisons, including 216 role-matched comparisons. Uniform weighted growth 7 is retained. This comparison does not prove battle balance; encounter results remain separate native simulation evidence.

Do not enlarge every JSON/stack buffer to serialize the complete roster. Keep bounded per-profile output and use a generated service catalog or small pages for browsing. Keep the device's existing asset working set/cache bounds; 255 appearance references do not imply loading 255 sprites into RAM or prefetching the full catalog. Art availability and permission remain separate from whether a form has playable metadata.

## Authoring batches and acceptance checks

Before splitting work: freeze the verified inventory keys and old-form mapping; reserve stable IDs; approve this numeric schema; and resolve the Armor/No Level combat-tier choices. Authors then receive disjoint entry IDs and produce metadata only. A central generator validates and emits the tables; authors do not each write C++ resolvers or adjust old profiles.

Required lightweight checks:

1. Every named inventory entry is represented exactly once or explicitly unresolved. Distinct variants remain traceable; NPC contents are not fabricated.
2. Old 66 profiles match baseline outputs at every legal level, with existing snapshots/Auto receipts unchanged.
3. Authored budgets and zero-sum variations validate; all five stats remain within bounds and nondecreasing with level. Check each proposed evolution at unlock and at delayed levels, showing any tradeoff.
4. Three valid labels exist per playable entry; IDs, names, bindings, and provenance references are unique/consistent where required.
5. Run the actual native resolver on role pairs, stage anchors, type matchups, and ±2-level cases. Count strict Physical/Magic preference reversals, viable Heavy opportunities, card benefit, and failure/turn/HP/recovery distributions. No target requires NPCs or players to win 100%, or requires a 50% aggregate win rate.
6. For whole fights, separate capture eligibility/success from defeat-based wins and separate Auto from the public-information Tactical policy. Use deterministic per-matchup seed strata shared between candidates; the previous repeated sixteen-seed sample is a regression baseline, not independent probability evidence.
7. Verify new capture/XP/evolution commits and old-save migration once across lost acknowledgement, restart, and retry. Full catalog data must not weaken existing identity, resource, or exactly-once boundaries.

Guard/rival-form behavior is a separate rules change from these profile tables. The accompanying rules5 core exposes wild Brace/Ward/Counter and exact-form rivals while preserving old-rule replay. Its native fight audit must distinguish those policy changes from the new roster's stat differences; changing the profile budget requires a separate reviewed regeneration.

## Reproduce catalog verification

Run `python3 scripts/generate-world-ds-profiles.py --check` and `python3 tests/test_world_ds_generator.py` for deterministic generated files and nine authoring validation tests. `python3 scripts/verify-world-ds-catalog.py` compiles the focused native test with AddressSanitizer and UndefinedBehaviorSanitizer, compares every native detail object to the generated runtime JSON, and compares the preserved 66 forms at all 849 legal levels with the committed baseline export. It writes `docs/evidence/world-ds-catalog-verification.json` with source hashes.

Pass `--esp-cxx /path/to/xtensa-esp32s3-elf-g++` to also reproduce the target object measurement with an already installed toolchain; the script installs nothing and does not flash a device. The normal CMake and direct host builds also provide `world-ds-catalog-test`. Current evidence records 3,877 native checks, no sanitizer failures, a largest detail of 2,168 bytes, and a largest sliding 16-form page of 2,777 bytes, both within their 6,144-byte bounds. The largest lineage remains seven forms with at most two children per node.
