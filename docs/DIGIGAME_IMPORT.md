# DigiGame source integration

The selected source is [EwertonMendes/DigiGame](https://github.com/EwertonMendes/DigiGame), pinned to commit `753ff677e127a1d33405102d677a7b733a5c8dd2`. The local source repository is under ignored `.personal-assets/digigame/source-git`. Our importer reads pinned PNG and JSON blobs with Git; no upstream application, script, dependency, hook or filter was executed.

The earlier website acquisition workflow stopped at the user's request. Its 87 verified packs remain the immutable baseline. Do not restart that downloader as part of this integration.

## Coverage and provenance

The source contains 406 character folders, 405 usable field-image/metadata pairs and 406 portrait pairs. Its metadata claims 398 field strips as `official_ds`; seven community, project-original or project-supplied exceptions are excluded from this import. A source-kind label records upstream provenance, not an independent rights clearance.

The current roster retains 255 source identities and 276 native forms. **241 source identities now have active private artwork**: 87 preserved packs and 154 reviewed additions. All 154 additions passed actual browser loading and 2,772 native frame decodes. Five held identities and nine absent species remain explicit gaps. Release verification (historical local evidence omitted) · Asset execution evidence (historical local evidence omitted) · [Identity and alias review](research/digigame-import-review.json).

| Remaining identity or source gap | Reason |
| --- | --- |
| Antylamon, Yatagaramon, BlackImperialdramon, Justimon | Mode or era identity needs stronger evidence before assigning pixels |
| Daemon | One upstream Demon image also serves Creepymon; our two source appearances remain distinct |
| Azulongmon, Baihumon, Barbamon, Belphemon, Chronomon DM, Ebonwumon, Granlocomon, Leviamon, Zhuqiaomon | No matching upstream species/assets verified |

The source's character notices identify proprietary Digimon artwork. No blanket code/data license was found. Raw source, converted artwork, private contacts and the Git clone stay outside tracked files and distributable bundles. Separate terrain/UI/font licenses do not license the character art. [License and provenance audit](research/digigame-source-review.json).

## Conversion contract

`scripts/import-digigame-assets.py` accepts bounded, reviewed plans with exact existing form IDs, source slugs, pinned commit, PNG/metadata hashes and selected direction. It stages private output through our existing sprite and DVA encoders. Source metadata and our conversion records keep separate hashes; an upstream original-source hash is a claim unless those original bytes were independently checked.

The imported field strips contain four directions with three poses each. A reviewed direction supplies idle/step poses for the existing animation interface. Unavailable care/battle actions explicitly fall back to those poses. These are field animations; the import does not invent dedicated attack or hurt frames. Frame anchoring, 64-pixel browser output, 32-pixel device output, nearest-neighbor resizing, alpha threshold and palette reduction are reviewable conversion choices. Original source pixels are not repainted.

Each form remains an independently loaded pack. The browser retains at most two actor packs. DVA decoding uses a bounded reader and one 32×32 RGB565 output frame; the full roster is never decoded into device RAM. The 241 DVA files total 2,242,928 bytes; their local microSD staging folder, including its index and attribution, totals 4,207,868 bytes. Browser packs total 11,986,839 encoded bytes. MicroSD preparation uses the existing `DSFnnnnn.DVA` names under `.personal-assets/world-ds/sd-card`; no physical card was touched.

## Skills, stats and evolution

The source has 409 species records and 1,573 technique records. Nine technique IDs are advertisement-loader strings and contaminate 20 learnset assignments; those entries are excluded from useful reference candidates. Official-reference text corroborates 114 move-label candidates across 113 current forms, with identity qualifications retained. The app's Field notes exposes bounded encyclopedia move references; Combat moves describes the three moves actually used by the battle engine.

The 255 roster entries have three named Physical, Heavy and Magic slots each. The gameplay follow-up applies 30 reviewed official-name bindings across 26 forms (24 actual renames); remaining slots keep authored names. They still use three shared mechanics, with damage-only adaptations. All 276 five-stat profiles, numeric growth curves and immutable IDs remain unchanged. Stage/role balance deliberately shares curves; the upstream level-99 tactical stats and its estimated intelligence values are retained as qualified comparison data. [Actual battle-name evidence](research/battle-skill-bindings.json) · [Database audit](research/digigame-database-review.json) · [Per-form references](research/digigame-identity-candidates.json).

The initial import added 16 reviewed forward routes, such as Gizamon→Raremon, Airdramon→Megadramon→Darkdramon and LadyDevimon→Lilithmon. The follow-up adds nine more, including Dragomon→Neptunemon and Parrotmon→Crossmon. The graph now has 172 edges, preserving all 163 earlier edges and gates, and remains acyclic with at most two outgoing choices. The 41 remaining Rookie/Champion/Ultimate leaves have explicit source, identity, unsupported-target or intentional-ending dispositions; they are not all missing routes. [Nine additions and all remaining leaves](research/digigame-gameplay-evolutions.json).

These are authored prototype routes supported by the pinned game's forward table. Their level/bond gates come from our target profiles. We retain XP and level on evolution, keep authored baby level/bond gates (rules12 useful Care plus encounter/recovery bond replaces the old full-mood Play/Rest loop), and leave armor, fusion and ambiguous mode conversions outside this addition. [All proposals, holds and source pointers](research/digigame-evolution-review.json).

Current care uses schema 12/rules 9, service container 11 and new practice rules 7. The final tuning changes only Original Lumen/Pelagia stat anchors and public-information practice Auto; the import and its 14 artwork gaps remain unchanged. Frozen rules 7 retain the earlier graph, labels and capture behavior for exact history replay; saved practice rules2–4 retain 30 exchanges and rules5 retains its original 40-exchange/floor4 behavior, while rules6 retains its original profiles and phase-only Auto. Frozen care rules8 and its completed Auto playback are preserved. The canonical snapshot remains 576 bytes and the measured host State remains 552 bytes. Old receipt IDs and pending commands preserve their original epoch; a new route cannot make an old command legal retroactively. [Current tuning and compatibility review](FINAL_TUNING.md).

## Run and verify

```sh
npm run dev:direct
# Open http://127.0.0.1:8787
python3 -m unittest discover -s tests -p test_world_ds_generator.py
# Sprite tests need a Python interpreter with Pillow installed:
DIGIVICE_SPRITE_PYTHON=python3 npm run test:digigame-import
python3 scripts/simulate-evolution-progression.py
python3 scripts/run-world-ds-balance.py
npm run build:esp:waveshare
```

Local private art is discovered from the existing ignored asset directory. Source bundles use original fallback artwork when those private files are absent. Browser and host checks exercise software behavior. ESP compilation proves that the selected profiles build; LCD, sensors, NFC, real SD removal, brownouts, battery life and runtime heap/stack still require the physical board.

The initial asset import passed all 11 importer tests and validated every added pack in browser and native decoders. The gameplay follow-up passed 23 generator tests, nine CMake tests, 752 evolution playthroughs and a fresh 233,920-fight audit. Updated two-button capture, partner, evolution, Tactical and Auto flows passed at 306, 201.6 and 140.16 CSS pixels; the smaller sizes are nominal inches at 96 CSS pixels per inch, not calibrated physical-size or LCD-legibility measurements. All three ESP profiles compile with zero warnings and unchanged static DIRAM. [Current verification](VERIFICATION.md) · [ESP sizes](ESP_BUILD.md) · [Current local preview](http://127.0.0.1:8787).
