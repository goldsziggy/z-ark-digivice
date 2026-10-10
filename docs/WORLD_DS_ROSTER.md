# Digimon World DS roster expansion

**Historical measurement scope:** the profile/migration and numerical verification sections below record earlier catalog milestones. Current care12/rules9/catalog5/practice7 and the two Original Ultimate exceptions are documented in the [final tuning report](FINAL_TUNING.md) and [verification](VERIFICATION.md); source identities and artwork coverage are unchanged.

The [source page](https://www.spriters-resource.com/ds_dsi/dgmnworldds/) lists **255 named Digimon sheets** among 320 assets. This is a sheet/variant count, not a verified count of unique canonical species. Hero, NPC Characters and NPC Digimon are grouped sheets; the NPC Digimon contents remain uninspected and are not silently counted as another species. The remaining assets are 56 backgrounds and 6 miscellaneous sheets.

| Source category | Named sheets |
| --- | ---: |
| Fresh | 4 |
| In-Training | 7 |
| Rookie | 49 |
| Champion | 60 |
| Ultimate | 58 |
| Mega | 68 |
| Armor | 8 |
| No Level | 1 |

The catalog preserves each listed identity, including Dot, color and mode variants. **45 entries reuse existing forms;210 append new IDs, producing 276 native forms** including the ten original creatures and 11 previously supported franchise forms outside this source list. Existing IDs 1–66, stat curves, skills and evolution edges are preserved. The original Egg→eight Rookie choice remains the onboarding path.

## Data and authorship

The complete stats and moves CSV (historical local evidence omitted), [stable ID ledger](../data/world-ds-ids.json), [assembled catalog](../data/world-ds-catalog.json) and [explicit evolution links](../data/world-ds-evolutions.json) are reviewable source data. The [inventory](research/world-ds-inventory.json) records 133 verified sheet IDs/credits and 122 unresolved detail URLs. No URL was guessed to work around blocked access.

Official encyclopedia facts are stored separately from game design: names, displayed stage, creature Type, Attribute and available Special Move labels have cited official references. Source-game stage and current official stage may differ. Early metadata covers 120 source entries; late metadata covers 135, including multiple candidates where an uninspected sheet leaves a mode uncertain. Dot references describe the base species, not an invented canonical Dot species. Daemon/Creepymon share an official entity but retain distinct source identities. Kumamon/Bearmon remains a qualified historical alias; it is not merged with the distinct official Hybrid Kumamon.

**Numeric stats, four battle types, combat roles and new move names are our prototype design.** Each of the 255 entries has distinct Physical, Heavy and Magic labels; the assembled catalog has 765 unique labels. These select the shared combat mechanics, not 765 bespoke skill implementations. Verified official moves remain separate reference fields. Five late sheet variants remain unresolved after the identity audit: Antylamon, Yatagaramon, Belphemon, BlackImperialdramon and Justimon. The existing visual review resolves orange Vaccine MetalGreymon. Source names and authored profiles remain playable without making an unverified canonical mode claim.

The [identity audit](research/world-ds-identity-audit.md) distinguishes shared canonical references from visual variants. The [content audit](research/world-ds-content-audit.md) reviews all255 entries and765 labels, including three corrected anatomy/weapon labels. The 210 additions deliberately use **28 reviewed numeric curves and 78 curve/type combinations**. Shared stage/role families keep balance inspectable; species-specific stat offsets are not part of this implementation. Shared roles have tested tactical tradeoffs, but distinct names alone do not imply distinct combat mechanics. The [role/identity review](research/world-ds-role-identity-review.md) records the per-entry role choices and their limits.

## Balance rules

New profiles start from the following integer anchors. Preserved profiles retain their original formulas instead of being normalized into this table.

| Combat tier | Entry level | HP | Attack | Defense | Magic | Resistance |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Fresh | 1 | 64 | 8 | 6 | 8 | 6 |
| In-Training | 1 | 80 | 12 | 9 | 12 | 9 |
| Rookie | 1 | 104 | 18 | 14 | 18 | 13 |
| Champion | 5 | 136 | 28 | 24 | 29 | 25 |
| Ultimate | 10 | 176 | 44 | 38 | 45 | 39 |
| Mega | 15 | 216 | 60 | 54 | 62 | 56 |

Each profile adds one explicit role allocation: Balanced, Striker, Mystic, Bulwark or Warden. Physical and magical walls have reciprocal defensive weaknesses. Armor uses authored Champion strength while retaining its Armor display stage. Calumon retains the source No Level/official Unknown distinction and uses an explicitly authored Rookie-tier Warden profile. The [balance design](research/world-ds-balance-design.md) gives allocations and growth. All new per-level growth has a weighted authoring budget of 7 (`HP/4 + Attack + Defense + Magic + Resistance`); this is an authoring constraint, not a second combat equation or a win-rate guarantee.

The existing shared damage formula and type multipliers remain. New wild encounters expose Brace, Ward or Counter, so the target's current defense matters. Heavy still costs 6 energy: Counter can reflect it, while favorable openings reward its extra power. The policy uses the same Counter meaning as Practice. Capture success and defeat-based victory are reported separately; no tuning target forces either mode toward 50%.

## Collection and routes

Keep eight carried companions with full individual care/progression state. A fixed 512-bit obtained-form journal remembers forms after release; IDs are stable, monotonically allocated identities rather than array positions. Releasing an inactive companion is an explicit Home action with browser review/confirmation. It does not kill a creature or erase its journal entry. The active partner and companions in unresolved battles cannot be released.

All **276 forms**, including the 255 source entries, are eligible for encounters and capture under the stage/level rules. This does not make every Mega available to a level-one Rookie. A captured evolved form retains its actual encounter form and level. Independent encounter access is separate from having an evolution route.

The current graph has **163 directed edges**, preserving all 147 previous edges/gates and adding 16 reviewed DigiGame choices. It is acyclic, with at most two outgoing choices and potentially several incoming routes. Stable profile families, IDs, stats and skill identities do not change when an edge joins two families. The original eight Rookie starters and their two initial routes remain; later forms can gain a reviewed second branch.

| Current native coverage | Forms | Meaning |
| --- | ---: | --- |
| Progression | 143 | At least one implemented outgoing evolution choice |
| Terminal | 60 | Reached by a route, with no implemented outgoing choice |
| Independent | 73 | No incoming or outgoing route; obtainable through encounters |

There are **133 leaves** in total, including 50 Rookie/Champion/Ultimate leaves. Of the 73 independent forms, 72 are source entries and one is original Flicker. These counts make the incomplete route coverage explicit; an early-stage leaf is not silently described as a completed species lineage. Every leaf carries a reviewed reason in the [assembled catalog](../data/world-ds-catalog.json). All 11 Fresh/In-Training entries now have an outgoing route.

Baby routes use level 1/bond 10 into In-Training and level 1/bond 20 into Rookie; stat anchors stay at level 1. Typical later gates remain Champion 5/20, Ultimate 10/50 and Mega 15/80. Requirements are edge-specific. Examples include Poyomon→Tokomon→Patamon, Koromon→Agumon or Agumon (Black), Guilmon→Growlmon→WarGrowlmon→Gallantmon, and Greymon→MetalGreymon or SkullGreymon. Chibomon→Veemon and Kuramon→Keramon explicitly skip intermediate forms absent from this catalog. These are authored game routes, not a claim to reproduce World DS mechanics or every canonical requirement.

Special mechanics remain explicit exceptions. Armor forms stay independently capturable without invented Digimentals; fusion and mode-conversion systems are not implemented. Kumamon→Grizzmon is held for source-identity confirmation, and Rapidmon→BlackRapidmon is held for its unimplemented conversion condition. Missing intermediate forms, unresolved variants and weakly supported proposals are explained rather than filled with arbitrary links. [Early review](research/world-ds-evolution-proposals-early.json) · [Ultimate review](research/world-ds-evolution-proposals-ultimate.json) · [Late/Armor review](research/world-ds-evolution-proposals-late.json).

**Evolution graph** now pages the connected graph component through `GET /api/evolution-graph?formId=18&offset=0&limit=8`. The native/service page limit is 16 nodes and the browser uses eight, with **Next page** and **Previous page**. Inspect a form, then **Connected routes** to see its incoming/outgoing links and edge-specific requirements. Incoming links are context; they do not authorize evolving backward. The largest current component has 21 forms. The older lineage endpoint remains a stable profile-family view; it is not the complete cross-family graph.

Care uses **schema 10/rules 7**, a 576-byte canonical snapshot and a measured 552-byte host State. Service container 9 archives old histories. Exact rules-5 and rules-6 replay freezes the prior graphs and profiles as well as transitions; earlier frozen engines remain. Migrated active rules-4/5/6 encounters keep their original behavior. Practice 4 retains 120-byte snapshots and strict old 2/3 dispatch. Old pending commands cannot be relabeled as new rules. The service's idempotent receipts preserve their original epoch and exact result.

## Artwork and microSD

The user selected the pinned DigiGame repository and stopped website acquisition. **241 of 255 source identities now have private packs** in index version 23: all 87 earlier verified packs plus 154 independently reviewed imports. The original 87 rows, versions and 348 artifact files remain byte-for-byte unchanged. Four mode/era identities, Daemon's potentially distinct appearance and nine absent species remain explicit gaps. [Integration and exact gaps](DIGIGAME_IMPORT.md) · Mixed-origin audit (historical local evidence omitted).

The importer reads pinned PNG/JSON Git blobs with our own code. Its durable activation intent allows interrupted index/batch publication to resume only when the recorded hashes match. Independent reviews bind source identity, selected poses, anchors and converted output. Field poses provide explicit idle fallbacks for unavailable battle/care actions. Dedicated authentic combat animations are not implied.

All source art, converted packs and private contact images remain under ignored local directories. The source character notices identify proprietary artwork; its availability and unrelated terrain/UI licenses do not grant a character-art license. The original artwork fallback remains available in distributable source bundles. [Source provenance and scoped licenses](research/digigame-source-review.json).

| Current 241 private packs | Measured bytes |
| --- | ---: |
| Browser packs, 64 px frames | 11,986,839 |
| Device DVA1 packs, 32 px frames | 2,242,928 |
| Largest device pack | 9,328 |
| Two decoded device RGB565 frames | 4,096 |
| Two opacity masks | 256 |

The batch importer requires exact rectangles, source hashes, anchors/transparency and an inspection record. Device downscaling is explicit nearest-neighbor. It emits per-form objects; the service reads only requested art and the browser holds at most two selected actor packs. Device files use the fixed 8.3 names `DSF00018.DVA`, `DSF00019.DVA`, `DSF00020.DVA`. A 255-entry metadata index is never passed into the small public device manifest or decoded into sprite RAM.

```sh
python3 scripts/assemble-world-ds-catalog.py --check
python3 scripts/prepare-world-ds-sd.py
python3 scripts/audit-world-ds-assets.py --output docs/evidence/world-ds-asset-status.json
```

The second command stages files and attribution under ignored `.personal-assets/world-ds/sd-card/`; it does not mount, format or write a physical card. Copying to hardware and bench verification remain separate. Private SD artwork is an optional capability; CRC detects damaged files and does not establish authorship or production authenticity. The LCD driver remains unimplemented, so serial decoding is not evidence of physical rendering.

The importer caps each compact device pack at 24,688 bytes. At that maximum,255 packs would occupy 6,295,440 bytes on microSD; this is a planning bound, not a measurement of unavailable artwork. No full-roster sprite set is embedded in firmware flash. Native form tables are immutable flash data, and game-state RAM does not grow with the number of source sprite frames.

[Site terms](https://www.spriters-resource.com/page/tou/) limit use and do not grant underlying franchise rights. Source sheets, private converted packs and private screenshots stay out of Git, distributable source bundles and Library uploads. Complete art integration needs accessible, lawfully obtained local copies and per-sheet review. No asset purchase or public deployment is part of this work.

## Verification boundaries

The [DigiGame integration](DIGIGAME_IMPORT.md) links current evidence: 36,373 sanitized core checks, 700 natural-event progression playthroughs covering all 163 routes, nine CMake tests and three warning-free ESP builds. This proves the tested behavior and resource bounds; play pacing and physical-device behavior still need evaluation.

The frozen 66 export contains 849 legal-level profiles for exact regression comparison. Analytical growth checks found no five-stat dominance over the preserved 56 franchise profiles in 920 same-stage comparisons; this does not replace native battle simulations. Native roster coverage, guard/move comparisons, capture-versus-victory outcomes, browser retry flows and ESP linker budgets are recorded with the final build evidence. Device memory peaks, frame rate, battery life, real motion/NFC, physical SD recovery and screen behavior remain unmeasured until hardware is available.

Recorded rules-7 native evidence covers **36,373 sanitizer checks**, all 163 evolution edges, all 276 migrated form payloads and 72 rules-6 Auto continuations. All 16 new routes reject under frozen rules 6. The widest tested state JSON is 6,550 of 8,192 bytes. Actual native output (historical local evidence omitted) · Progression simulation (historical local evidence omitted) · 233,920-fight balance audit (historical local evidence omitted). These are host proofs; physical-device results remain open.

## Recorded rules-5 baseline and current verification boundary

The following measurements belong to the recorded **rules-5 catalog milestone**, before the rules-6 graph expansion. The unchanged profiles make them useful balance/regression evidence; they do not prove a later source revision compiled or its new graph passed. Current checks belong in [VERIFICATION.md](VERIFICATION.md) and [ESP_BUILD.md](ESP_BUILD.md).

- All 276 forms passed actual native Walk→attack→capture→select→save/restore;26,782 core checks passed under sanitizers. Practice 18,502 checks and 25,738 frozen-rule checks passed.
- Native catalog checks preserve 66 profiles at 849 legal levels exactly; every generated native detail matches host metadata. Maximum detail/page responses are 2,168/2,777 bytes.
- The rules-5 native audit ran 233,920 fights plus 5,760 sanitizer fights. Results and limits (historical local evidence omitted) and matched old/new early-game comparison (historical local evidence omitted) distinguish capture, victory, difficulty strata and modes.
- Service and browser-module regression:180/180 passed, plus strict TypeScript. Real-browser tests cover high-ID capture, private art, release, journal, stable IDs, interrupted replies and round-screen geometry.
- ESP constant form metadata measures 46,565 bytes of object-level read-only data and zero mutable data; final linker merging may differ.

| ESP profile | App bytes | Static DIRAM bytes | Remaining app-slot bytes |
| --- | ---: | ---: | ---: |
| Generic serial | 1,146,704 | 114,219 | 1,999,024 |
| Waveshare + microSD | 1,155,408 | 115,299 | 1,990,320 |
| Waveshare development | 1,167,904 | 137,187 | 1,977,824 |

Those three recorded builds used the existing unmodified ESP-IDF 5.3.6 toolchain and compiled with zero warnings. Source-hashed build evidence (historical local evidence omitted). The development profile compiles optional local SD artwork inspection. `art status` / `art preview` validate the selected form’s 32 px file and decode a bounded frame; they do not draw a physical display. Main-task stack is configured 16 KiB, with actual high-water and heap peaks still unmeasured. No hardware was flashed.
