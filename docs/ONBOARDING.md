# Choose an egg, meet a Rookie

Current onboarding uses **care schema 15 / rules 12 / 636-byte snapshots / service container 14**. Each unhatched identity has eight fixed starters plus three distinct saved Rookie offers. A durable `starter-offer-seed` event fixes the offers before display; `hatch` accepts fixed slots 1–8 and saved slots 9–11. Back, retry and restart do not reroll them; migration leaves existing companions intact. The native carousel and browser both use the saved choices. Both units passed the saved-choice/reboot checkpoint; see device evidence (historical local evidence omitted). [Current starter contract](STARTER_OFFERS.md) · [Release and verification](CARE_CAPTURE_RELEASE.md) · [Care/capture rules](CARE_CAPTURE_RULES12.md).

The starter flow gives each new game a deliberate first companion: choose one of eleven choices, confirm, and hatch directly into the chosen Rookie. **Immediate egg → Rookie is this prototype's onboarding rule**, not a claim about the franchise's canonical hatching stages. Rookie is a Digivolution stage; the game's numeric level is a separate concept.

## Historical eight-starter reference

The sections below preserve the original eight-starter onboarding design and implementation record. Old version/size, art-availability, browser-flow and unfinished-renderer statements refer to that milestone; they do not describe the current release. The fixed eight-name roster remains part of the eleven-choice flow.

## Roster and source provenance

The official English Digimon Encyclopedia lists all eight below as **Rookie**. Names and listed moves were checked on 2026-10-06. These links establish species facts, not permission to redistribute the site's illustrations or a claim that matching game sprites are installed.

| Starter / official reference | Official type | Official attribute | Listed special moves |
| --- | --- | --- | --- |
| [Impmon](https://digimon.net/reference_en/detail.php?directory_name=impmon) | Little Devil | Virus | Summon; Night of Fire |
| [Agumon](https://digimon.net/reference_en/detail.php?directory_name=agumon) | Reptile | Vaccine | Pepper Breath |
| [Gabumon](https://digimon.net/reference_en/detail.php?directory_name=gabumon) | Reptile | Data | Blue Blaster |
| [Patamon](https://digimon.net/reference_en/detail.php?directory_name=patamon) | Mammal | Data | Air Shot; Wing Slap |
| [Tentomon](https://digimon.net/reference_en/detail.php?directory_name=tentomon) | Insectoid | Vaccine | Super Shocker |
| [Palmon](https://digimon.net/reference_en/detail.php?directory_name=palmon) | Vegetation | Data | Poison Ivy |
| [Gomamon](https://digimon.net/reference_en/detail.php?directory_name=gomamon) | Sea Beast | Vaccine | Marching Fishes |
| [Renamon](https://digimon.net/reference_en/detail.php?directory_name=renamon) | Beast Man | Data | Kohenkyo; Diamond Storm |

The prototype's Grove/Tide/Ember/Neutral combat categories, numerical stats, three move slots and damage formulas are project balance choices. They are not official Digimon classifications or canonical power ratings. A sourced move name does not make its assigned numerical damage canonical. The ten existing original creature/stage profiles remain separate from these eight starters.

## Prototype profiles

The shared native [form table](../core/forms.cpp) defines these level-1 Rookie values. Hatch IDs are deliberately different from internal species IDs: hatch `1` selects Impmon, whose species enum is `5`; callers use the starter catalog instead of inventing either mapping.

| Hatch ID | Starter | Game type | HP / Attack / Defence / Magic / Resistance | Physical / Heavy / Magic moves |
| --- | --- | --- | --- | --- |
| 1 | Impmon | Ember | 92 / 16 / 12 / 24 / 14 | Prank Jab / Imp Rush / Night of Fire |
| 2 | Agumon | Ember | 104 / 24 / 14 / 14 / 11 | Claw Jab / Dino Charge / Pepper Breath |
| 3 | Gabumon | Tide | 104 / 20 / 18 / 13 / 12 | Horn Jab / Horn Rush / Blue Blaster |
| 4 | Patamon | Neutral | 96 / 14 / 13 / 18 / 20 | Wing Slap / Air Tackle / Air Shot |
| 5 | Tentomon | Grove | 108 / 16 / 23 / 12 / 11 | Shell Tap / Beetle Bash / Super Shocker |
| 6 | Palmon | Grove | 104 / 13 / 16 / 22 / 12 | Vine Lash / Root Slam / Poison Ivy |
| 7 | Gomamon | Tide | 112 / 16 / 19 / 12 / 14 | Flipper Slap / Iceberg Rush / Marching Fishes |
| 8 | Renamon | Neutral | 92 / 23 / 12 / 18 / 13 | Palm Strike / Fox Rush / Diamond Storm |

Physical and heavy labels are prototype-authored except Patamon's sourced Wing Slap. The magic-slot labels come from the official references above. These slots still use the existing generic physical/heavy/magic damage resolver: they do not implement every special effect described in franchise lore. See [combat formulas](BATTLE_STATS.md).

Each starter begins at full HP, energy 80, fullness 70, mood 80, bond 0, XP 0 and numeric level 1. Numeric level follows total XP: level `L` requires `20 × (L − 1) × L` XP, up to level 20 / 7,600 XP. Wild victory or capture awards the active partner `20 + 6 × enemy level` XP; care and practice award none. Bond remains a separate care/relationship value.

All eight starters now have **two curated branches through Champion → Ultimate → Mega**. Digivolution requires an explicit confirmed choice at Home: Champion needs level 5/bond 20, Ultimate level 10/bond 50 and Mega level 15/bond 80. Numeric level-up alone retains the current form. Open **Menu → Progression → Digivolution choices**, inspect the branch’s stats/moves and requirements, then review and confirm. A read-only lineage tree shows all seven forms. These selected routes and balance values are our game design, not claims of exclusive canonical evolution paths. See [the complete branch roster](../README.md#levels-and-digivolution) and official name/stage research (historical local evidence omitted).

Changing form preserves member identity, XP, bond and capture history. HP preserves its proportion, rounded upward. For migration, the old Rookie training bonuses `(+16, +6, +5, +7, +6)` and `(+36, +14, +12, +16, +14)` are retained exactly at current levels 5 and 10, in the table’s stat order. These historical anchors no longer define automatic bond-based leveling or evolution.

## Browser flow and saved commands

Run `npm run dev:direct`, then open `http://127.0.0.1:8787`. A newly claimed local device enters **Choose your egg**. Browse the eight entries, open **Hatch [name]?**, and confirm **Hatch [name]**. Hold Back before confirmation to change the draft choice. After the saved result arrives, **Hello, [name]! → Meet your partner** opens Home. Merely browsing or reviewing an egg does not change native game state.

The routes are `starter-select`, `starter-review` and `starter-hatched`. The round UI uses Next/Back/Confirm and direct touch; a failed starter-catalog request shows a retry action. Care, exploration, capture, cards, partner selection and practice cannot begin before hatching. A submitted command enters the existing saving/recovery flow: Back cannot cancel a command that might already be committed.

| Interface | Contract |
| --- | --- |
| `GET /api/starters` | Native-derived `{formatVersion:1,rulesVersion:4,starters:[...]}` with exactly eight ordered entries: `id`, `species`, `name`, `stage`, `combat`. |
| Fresh paired state | Schema 7 / rules 4, `phase:"egg"`, `onboarding:{completed:false,starterId:null}`, empty collection, active ID 0 and null creature/species/combat. |
| Hatch command | Authenticated `POST /api/save-sync` with the usual revision and batch ID, and event `{type:"hatch",value:1..8}`. There is no separate hatch endpoint. |
| Successful hatch | Exactly one founder, member #1, selected as partner; Home phase, completed onboarding and the chosen starter ID. The event advances sequence once and consumes no encounter RNG or capture reward. |

The browser durably retains the exact request before sending. Retrying its existing batch ID and semantic body returns the original success result, including after reload/server restart; it does not hatch twice. Reusing an ID with different content returns `409 batch_mismatch`; a new stale-revision request returns `409 revision_conflict`. A second new hatch event on a completed game is invalid and leaves the save unchanged. New practice requests while unhatched return `409 onboarding_required`. Fetch and explicitly reconcile conflicts instead of quietly replacing a starter or replaying an old pending command as new. [Full service contract](../service/API.md)

## Existing saves and bounds

The explicit native initializer `newDevice()` creates the unhatched state. `newGame()` retains the established original Mote start for old saves and deterministic historical replay. The service stores each identity's immutable `initialMode` so **an existing zero-event save remains an existing pet**; zero captures, an empty event suffix or a failed asset load are not first-run signals.

The original onboarding migration moved service format 3/schema 4 to format 4/schema 5 under rules 3, marked existing devices `initialMode:"legacy"`, and archived the prior container as `store.schema-v4.json`. Current service **format 6/schema 7/rules 4** preserves identities, token hashes, seeds, revisions, historical events, migration baselines and receipt identities. Older rules-1/2/3 histories replay through their frozen executors before conversion; an old pending request is not silently reissued under new rules. Unknown or corrupt state fails closed instead of presenting a fresh egg over an unrecovered save.

Onboarding originally introduced the **412-byte format-5 snapshot**, including its two explicit onboarding fields and CRC. Current native snapshots are **500 bytes, format 7**. Recognized pre-onboarding formats migrate as completed; later eggs and starter choices remain intact. Old training tiers 1/2/3 become numeric levels 1/5/10 with matching historical stat anchors and threshold XP; original appearances map to explicit forms, while old named Rookie starters remain Rookies. Conversion never replays historical events with a different founder or new reward rules.

The collection limit stays **eight members including the starter**. Hatching creates the founder without consuming a capture slot beyond that first member; it does not grant all eight choices. The original encounter pool is unchanged, and these starter choices are not newly added wild captures. The original onboarding milestone measured its eight-starter catalog at **1,894 bytes** and then-current 34-profile catalog at **7,708 bytes**; those are historical serialized host measurements, not the current form-table size or ESP heap use. Current native progression has **66 forms**: ten original forms and seven forms for each of eight starters. Starter selection remains exactly eight entries; the lineage catalog is bounded to seven forms and immediate evolution choices to two. See the [current API bounds](../service/API.md) and [verification](VERIFICATION.md).

Existing sync limits remain 32 KiB/request, 100 events/batch, eight device identities and 10,000 lifetime events per identity. The browser needs its running local service for hatching; this is not a claim of fully offline browser gameplay.

## Artwork and storage

Starter identity must remain readable without an artwork download. An original egg graphic can show the selected species name before hatching; an explicit missing-art presentation must identify the actual Rookie afterward. An unrelated original creature must not silently stand in for a named Digimon.

The native `--starters` catalog defines these exact choices. **All eight currently lack species-specific artwork in both the bundled browser packs and the embedded device catalog.** The browser supplies original egg graphics and an explicitly identified original Rookie placeholder; these are not finished character sprites.

| Egg ID | Rookie |
| --- | --- |
| 1 | Impmon |
| 2 | Agumon |
| 3 | Gabumon |
| 4 | Patamon |
| 5 | Tentomon |
| 6 | Palmon |
| 7 | Gomamon |
| 8 | Renamon |

The ten available original creature appearances are **Mote, Glint, Lumen, Flicker, Rill, Brine, Pelagia, Cinder, Scoria and Pyrel**. They appear in the browser's [starter](../assets/packs/starter-v2.json), [tide](../assets/packs/tide-v1.json) and [ember](../assets/packs/ember-v1.json) packs and the [device asset index](../assets/device/index.json). The [native form table](../core/forms.cpp) remains authoritative for identity and stats; these original appearances do not establish canonical Digimon evolution lines. All **56 franchise forms have `artId:null`** until appropriate artwork is supplied. Numeric growth keeps the Rookie name until explicit Digivolution; unavailable evolved artwork uses an original question-mark plaque labeled **Artwork pending**, never a mislabeled Rookie sprite.

The existing private Agumon-family appearance pack remains under ignored `.personal-assets/` and the separate personal importer. The browser's explicit mapping in [app.js](../web/app.js) can use that pack's old `mote` art slot for Agumon when the user imports and enables it. This is **browser-only optional private artwork**, not bundled or signed device content; it does not supply the other seven starters or grant redistribution rights. No private files are copied into this guide or the public packs. The firmware [prefetch policy](../firmware/runtime/prefetch.cpp) skips unavailable form asset IDs, and the physical LCD renderer is still unimplemented. Missing artwork must not reset a save, substitute a species or prevent care and battle rules from running.

On the selected Waveshare 1.46, primary downloaded assets use microSD, while saves and resident fallback remain independent. Card absence or a failed asset download is not evidence of a missing game save. See [SD assets and recovery](SD_ASSETS.md) and [verified board wiring](PARK_HARDWARE.md).

## Hardware scope

The physical target has a **412 × 412** round display; the browser's **480 × 480 logical preview** is a separate presentation surface. The two-control convention is left tap for Next, left hold for Back and right tap for Confirm. This convention does not assign physical GPIOs: candidate button wiring and the LCD/touch port still require board work.

The firmware has a portable, heap-free **2-byte starter controller** and a serial adapter for that convention. It offers `starter status`, `starter next`, `starter confirm` and `starter back`; the last command simulates a held Back input. Confirm advances Egg → Selecting → Confirming → AwaitingCommit. Next wraps through eight choices; Back returns from confirmation to selection, then to Egg, preserving the draft choice. A final confirm applies the native Hatch event to a candidate state, checkpoints it, and only then replaces live state and marks onboarding complete. Direct `hatch 1..8` uses the same core/save path. No actual button or LCD input is implied by these serial controls.

Only `SaveStore::Empty`, meaning **both NVS slots explicitly missing**, creates and checkpoints a new egg. Corrupt, unreadable, unsupported or restored older saves do not trigger onboarding. If hatch persistence is uncertain, gameplay pauses for recovery; a reboot restores the committed egg or companion instead of awarding another founder. Before hatching, motion cannot create encounters and asset prefetch does not request invented egg/Rookie packs. Current native state JSON has a fixed 6,144-byte capacity; the original onboarding milestone used 4,096 bytes.

The original onboarding firmware host suite passed **349 starter-controller checks**, alongside save-policy cases for egg restore, uncertain hatch followed by reboot, and a frozen legacy 404-byte save. Those checks exercise host models of persistence and input. Browser/service tests and any post-onboarding ESP builds must be reported separately; they are not established by the controller test count.

The completed [ESP-IDF 5.3.6 build milestone](ESP_BUILD.md) precedes egg onboarding. Its three successful ESP32-S3 builds establish that milestone's compile/link results, not that the new flow has been displayed or hatched on hardware. No device was flashed, no SD card was accessed and no physical input, motion, radio or power-loss behavior was proven by that milestone. Keep new onboarding validation separate from those recorded binary sizes.

## Historical onboarding target builds

These pre-RPG onboarding measurements are retained as milestone evidence, not current binary sizes. [Current firmware builds](FIRMWARE.md) include the later changes.

The subsequent onboarding build passed in all three profiles with zero compiler warnings: generic **1,068,160 B**, Waveshare SD **1,076,912 B**, and Waveshare development-assets **1,088,080 B**. Compared with the SDK/SD milestone, each app grows by **4,928 B** and static DIRAM by **8 B**. The largest still leaves **2,057,648 B** in a 3 MiB app slot. At that milestone host State was 392 B and its canonical snapshot was 412 B; current values are 476 B and 500 B. Measured onboarding build evidence (historical local evidence omitted). Runtime heap and physical presentation remain unmeasured.
