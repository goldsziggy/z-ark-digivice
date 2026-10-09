# World DS species, skills, and stat-content audit

Audit baseline: commit `a352335`, 2026-10-06. **All 255 named entries and all 765 displayed skill labels were reviewed.** This is a content-quality review of the shipped authored prototype, not a claim that its stats, move effects, types, or evolution routes reproduce Digimon World DS. No form table, rules, dependency, or artwork was changed by this audit.

The strongest concrete correction is Yatagaramon's three-wing attack name: both referenced variants have three legs and two wings. Several weapon and damage-label refinements would improve clarity. No additional firm combat-tier or authored-role error was established. The roster is broad but remains mechanically based on shared roles, with many deliberately independent encounter forms.

## Evidence and method

Reviewed [assembled metadata](../../data/world-ds-catalog.json), [native runtime profiles](../../data/world-ds-runtime.json), both [early](world-ds-authored-early.json) and [late](world-ds-authored-late.json) authored batches, the [early](world-ds-official-early.json) and [late](world-ds-official-late.json) official-reference records, and [reviewed route edges](../../data/world-ds-evolutions.json). The early official records use `officialType`/`officialSpecialMoves`; the late records use `officialSpeciesType`/`signatureSkillLabel`/`officialReferences`. Both schemas were read; late records were not mistaken for missing research.

Two independent slices covered 120 entries /360 labels and 135 entries /405 labels. The consolidated review also examined every compact roster row, all role/stat groups, and all 45 new evolution edges. Official descriptions were consulted for the strongest anatomy/equipment concerns. This audit did **not** visually inspect all 255 source sheets. A verified reference species does not resolve every sheet's mode or variant.

There are 45 preserved DS bindings /135 labels and 210 new authored forms /630 labels. All 765 labels are unique, but uniqueness alone was not accepted as evidence of species fit. The eight starter records contain nine exact labels matching the recorded official move fields; the other labels are explicitly authored or preserved prototype names. New names need not copy canonical names to be acceptable.

Physical and Heavy must plausibly describe a hit or committed impact. Magic must read as a damaging special attack. Neither a suggestive label nor an official lore effect creates healing, shielding, poison, concealment, summoning, time control, or enemy control in the shared resolver. Four authored combat types (`grove`, `tide`, `ember`, `neutral`) are distinct from official species classifications and Vaccine/Data/Virus/Free attributes.

## Specific correction proposals

No proposal below has been applied. The first three are the smallest useful correction batch; they affect names, not damage formulas. Root coordination is still needed because names are compiled into native profiles and appear in saved presentation/replay output.

| Priority / confidence | Form / species | Slot: current → proposed | Reason and evidence |
| --- | --- | --- | --- |
| Clear factual mismatch / high | **213 Yatagaramon** | Heavy: **Threewing Plunge → Threeclaw Plunge** | Both [reference variant](https://digimon.net/reference_en/detail.php?directory_name=yatagaramon_axcel) and [anime variant](https://digimon.net/reference_en/detail.php?directory_name=yatagaramon) have three legs and two wings. The correction works without deciding which sheet variant is present. |
| Grounded equipment improvement / high | **265 Valkyrimon** | Heavy: **Battlemaiden Spearfall → Battlemaiden Swordfall** | Its [reference description](https://digimon.net/reference_en/detail.php?directory_name=valkyrimon) establishes a sword and bow. The spear is unsupported by the inspected reference; Swordfall retains the existing physical attack concept. This is a stronger grounding recommendation, not proof that any authored spear attack is impossible. |
| Grounded equipment improvement / medium-high | **242 GuardiAngemon** | Heavy: **Sanctified Shieldfall → Sanctified Bladefall** | The mapped [SlashAngemon](https://digimon.net/reference_en/detail.php?directory_name=slashangemon) uses arm blades, metal wings, and its cutting body. A shield is not established there and implies defense. Keep the existing authored Warden role. |
| Variant review required / high concern, medium replacement confidence | **219 Belphemon** | Physical: **Drowsy Claw → Chain Rattle**; Heavy: **Slumbering Colossus → Colossus Rumble**, **only if the variant remains unresolved** | Metadata leaves Sleep/Rage unresolved, but the current labels select a sleeping presentation. [Sleep Mode](https://digimon.net/reference_en/detail.php?directory_name=belphemonsleepmode) attacks through sleep-related sound and chain flames, whereas [Rage Mode](https://digimon.net/reference_en/detail.php?directory_name=belphemonragemode) supports direct physical aggression. Best action: identify the actual sheet, then name attacks accordingly. The alternatives avoid making a premature mode claim. |

These secondary refinements are editorial recommendations, not proven anatomy defects. Absence of a prop from an official description is weaker evidence than an explicit contradiction.

| Form / species | Slot: current → proposed | Reason |
| --- | --- | --- |
| **142 Hookmon** | Heavy: Hook Anchor Drop → **Hook Claw Slam** | Its [official description](https://digimon.net/reference_en/detail.php?directory_name=hookmon) directly supports a hook claw and cannon hand; the anchor adds unestablished equipment. |
| **110 Salamon** | Heavy: Sala Bell Tackle → **Sala Puppy Tackle** | Neither the supplied rationale nor [official profile](https://digimon.net/reference_en/detail.php?directory_name=plotmon) establishes a bell. Puppy Tackle retains the petlike body movement. |
| **122 Apemon** | Heavy: Ape Bamboo Hammer → **Ape Staff Hammer** | Removes an unsupported material choice. The [reference](https://digimon.net/reference_en/detail.php?directory_name=hanumon) does not settle the staff's material; this is not a claim that bamboo is canonically forbidden. |
| **200 Mummymon** | Heavy: Sarcophagus Slam → **Obelisk Slam** | The [official equipment](https://digimon.net/reference_en/detail.php?directory_name=mummymon) provides a named object for a heavy impact. A carried sarcophagus is not established by the inspected description. |
| **85 Dorumon** | Magic: Doru Quartz Pulse → **Doru Metal Pulse** | The recorded Dash Metal /Metal Cannon motif grounds metal more directly than quartz. Optional identity improvement. |
| **130 Dorugamon** | Magic: Doruga Quartz Gale → **Doruga Metal Gale** | The recorded Cannonball /Power Metal motif supports the same narrow improvement through this route. |

The following labels risk promising mechanics that do not exist. They remain usable as poetic names if the UI clearly identifies damage, but a small offensive-wording pass would be clearer than inventing bespoke effects.

| Form / species | Magic: current → proposed |
| --- | --- |
| **68 Kuramon** | Kura Static Veil → **Kura Static Glare** |
| **124 Bakemon** | Bake Lantern Shroud → **Bake Phantom Burst** |
| **146 Kurisarimon** | Kurisari Bit Shroud → **Kurisari Bit Burst** |
| **175 Deramon** | Fernlight Veil → **Fernlight Ray** |
| **209 SuperStarmon** | Constellation Shieldray → **Constellation Ray** |
| **216 Babamon** | Hearth Blessing → **Hearth Burst** |
| **260 Parasimon** | Spore Command → **Spore Discharge** |

“Veil”/“Shroud” suggest concealment or protection, “Blessing” suggests support, and “Command” suggests control. The current Magic action only damages. This concern does not justify rejecting every Halo, Mist, Seal, or Sanctuary label; many are understandable attack imagery.

## Findings deliberately not promoted to errors

- **Matadormon's Crimson Rapier is supported.** Its [official description](https://digimon.net/reference_en/detail.php?directory_name=matadrmon) establishes concealed rapiers. The Striker role and that weapon are coherent; the initial suspicion was dismissed.
- **Daemon's Cinder Scepter and ShogunGekomon's Toad Fan are weak prop concerns only.** The inspected references do not establish those props, but that absence alone does not prove a mismatch. If simplifying later, Cinder Palm and Toad Palm would avoid equipment assumptions. No immediate change is recommended.
- **Armor→Champion combat tier and Calumon→Rookie/Warden are explicit prototype decisions**, not mistaken official levels. The source and official stage fields remain separate. Calumon's damage-capable moves are authored fiction; its official record does not supply a signature move.
- **Official typing is not the game's elemental system.** Grovelike vegetation/earth motifs, Tide water/ice motifs, Ember heat motifs, and Neutral spiritual/machine motifs are authoring choices. A Vaccine/Free attribute or a Beast/Warrior species type alone cannot establish the correct prototype element.
- **Preserved labels are not silently corrected.** Garurumon's Frost Howl emphasizes ice while its recorded signature is Fox Fire; some original starter branches also inherit a prototype type that is not an official attribute. This is an existing authored-content distinction, not authorization to rewrite old profiles or old replay output.
- The canonical/variant audit is separate. At this baseline, Yatagaramon, Belphemon, BlackImperialdramon, and Justimon explicitly retain unresolved sheet variants. Parent research additionally flagged **Antylamon's Virus versus Deva/Data identity** for sheet review; this content audit does not settle that mapping or alter its stats.

## What the stat data actually differentiates

All **255 `speciesVariation` vectors are zero**. The 210 additions collapse to **28 distinct complete numeric curves**, or **78 numeric-curve-plus-type combinations**. The 765 skill names select three shared mechanics; they do not add 765 different effects. This is consistent with the intended shared-role design, but it should not be presented as individually tuned per-species stats.

For example, these thirteen forms have the same Champion/Striker/Grove stats, growth, type multiplier behavior, and three damage mechanics at the same level: Apemon, Aquilamon, Diatrymon, Grizzmon, Kokatorimon, Minotarumon, Ogremon, Peckmon, Reppamon, Roachmon, Stingmon, Prairiemon, and Shurimon. Their names, appearance references, collection identities, and possible progression routes differ. Similarly, Guardromon, Kurisarimon, Numemon, PlatinumSukamon, Raremon, Sukamon, and Kenkimon share the same Champion/Bulwark/Neutral combat profile.

The large native balance audit (historical local evidence omitted) and preserved-profile checks (historical local evidence omitted) establish bounded arithmetic and sampled fight behavior. They do not prove that every species feels distinct or that a chosen role is canonically correct. No new stat tuning follows from name review alone. If future per-species variation is desired, use a small reviewed zero-sum adjustment to the new profiles, measure matchups, and version/freeze old replay behavior before changing committed rules5 tables.

Preserved forms need a separate description: the assembled authoring metadata labels Agumon and Palmon Balanced even though their actual level1 Attack/Magic are24/14 and13/22; Renamon is labelled Mystic with23/18. Those are **descriptive metadata shortcomings, not changed native stats**. The service correctly exposes these profiles as `Preserved`. Prefer marking their host role rationale as a retained profile and showing actual stats rather than treating the authoring-role label as a numeric contract.

## Evolution coverage and stat tradeoffs

There are **147 completely unconnected entries** (no parent or child), including **104 below Mega**: 4 Fresh, 7 In-Training, 23 Rookie, 29 Champion, 32 Ultimate, 8 Armor, and Calumon. Across the 255 entries, 180 have no outgoing edge. The 210 additions include 18 reviewed families with45 new edges; 147 additions are independent encounter forms. All eleven Fresh/In-Training forms currently lack a route. Examples of recognizably incomplete existing-family coverage include Gotsumon /Icemon /Meteormon and the independent GeoGreymon /RizeGreymon entries. The audit does not infer or approve an edge from those names.

These are **content dead ends for that companion's evolution**, not missing catalog entries or a proven account-level softlock. They remain obtainable by encounters, and the core's encounter-tier floor lets Fresh/In-Training partners still meet Rookie forms. The existing “no reviewed route” messaging is important. Do not claim a full canonical evolution tree, automatically fill same-level routes, merge variants, or modify preserved branches to make the graph appear complete. The smallest improvement is to keep the missing-route statement visible before investing in a newly captured companion.

At the child's entry level, **21 of45 new route edges decrease at least one stat** because the role changes. This is not inherently a defect, but “evolution upgrades every stat” would be false. Notable examples from the actual runtime tables:

| Transition at unlock | Decreases |
| --- | --- |
| Garudamon → Phoenixmon, level15 | Attack −19 |
| Sangloupmon → Matadormon, level10 | Magic −16 |
| Peckmon → Yatagaramon, level10 | Attack −16 |
| MegaSeadramon → either Mega branch, level15 | Magic −18 |
| Kurisarimon → Infermon, level10 | HP −16, Defense −5 |

The current before/after preview is therefore necessary. Keep these as explicit role tradeoffs unless actual matchup evidence justifies a new rules revision. No canonical power ranking is implied by these authored numbers.

## Recommended next action

Apply only a coordinated small label batch first: Yatagaramon's anatomy correction, then the two grounded weapon refinements if accepted. Resolve Belphemon's actual sheet before choosing mode-specific wording. Treat the remaining props and damage-language proposals as optional editorial cleanup. Preserve all66 original profiles, their labels, and existing replay behavior. Shared-stat duplication and incomplete evolution families should remain disclosed; they require a separate design decision rather than a hidden “quality fix.”

## Complete entry review ledger

Each row below covers all three displayed labels. “Reviewed” means no additional specific correction was justified by this pass, not that the names are official, the artwork was inspected, or the species has bespoke mechanics. `P` denotes one of45 preserved bindings; `A` denotes one of210 authored additions. Route counts are the current parent /number of outgoing children. Named concerns refer to the proposals above.

<!-- Per-entry ledger and input hashes are appended from the reviewed immutable catalog below. -->

| ID /species | Kind; source stage /role /type | Physical ·Heavy ·Magic | Route parent /children | Review |
| --- | --- | --- | --- | --- |
| 67 Chibomon | A; Fresh /Balanced /tide | Chibo Nudge · Chibo Tumble · Dew Wink | 0 /0 | Reviewed |
| 68 Kuramon | A; Fresh /Mystic /neutral | Kura Blink Jab · Kura Eyeball Roll · Kura Static Veil | 0 /0 | Damage-label clarity |
| 69 Poyomon | A; Fresh /Warden /tide | Poyo Bob · Poyo Foam Press · Poyo Tide Halo | 0 /0 | Reviewed |
| 70 Puttimon | A; Fresh /Warden /neutral | Putti Wing Tap · Putti Cloud Drop · Putti Lantern Gleam | 0 /0 | Reviewed |
| 71 Dorimon | A; In-Training /Striker /neutral | Dori Horn Jab · Dori Spring Ram · Dori Silver Flicker | 0 /0 | Reviewed |
| 72 Kapurimon | A; In-Training /Bulwark /neutral | Kapu Shell Tap · Kapu Helmet Roll · Kapu Copper Glow | 0 /0 | Reviewed |
| 73 Koromon | A; In-Training /Balanced /neutral | Koro Hop · Koro Bounce Press · Koro Amber Bubble | 0 /0 | Reviewed |
| 74 Pagumon | A; In-Training /Mystic /neutral | Pagu Ear Flick · Pagu Somersault · Pagu Dusk Mist | 0 /0 | Reviewed |
| 75 Tanemon | A; In-Training /Warden /grove | Tane Root Tap · Tane Sprout Roll · Tane Dew Garland | 0 /0 | Reviewed |
| 76 Tokomon | A; In-Training /Striker /neutral | Toko Nip · Toko Leap Chomp · Toko Pearl Flash | 0 /0 | Reviewed |
| 77 Tsunomon | A; In-Training /Bulwark /grove | Tsuno Horn Nudge · Tsuno Bramble Roll · Tsuno Moss Glimmer | 0 /0 | Reviewed |
| 18 Agumon | P; Rookie /Balanced /ember | Claw Jab · Dino Charge · Pepper Breath | 0 /2 | Reviewed; preserved |
| 78 Armadillomon | A; Rookie /Bulwark /grove | Armadillo Paw · Armadillo Drum Roll · Armadillo Dust Halo | 0 /1 | Reviewed |
| 79 Aruraumon | A; Rookie /Mystic /grove | Arura Stem Slap · Arura Thorn Wheel · Arura Violet Haze | 0 /0 | Reviewed |
| 80 Betamon | A; Rookie /Balanced /tide | Beta Fin Flick · Beta Wave Lunge · Beta Ripple Arc | 0 /1 | Reviewed |
| 81 Biyomon | A; Rookie /Striker /grove | Biyo Feather Jab · Biyo Wing Rush · Biyo Petal Draft | 0 /1 | Reviewed |
| 82 BlackAgumon | A; Rookie /Striker /ember | Black Agu Rake · Black Agu Coal Ram · Black Agu Ash Swirl | 0 /0 | Reviewed |
| 83 Crabmon | A; Rookie /Bulwark /tide | Crab Pincer Clip · Crab Shell Hammer · Crab Brine Spiral | 0 /0 | Reviewed |
| 84 DemiDevimon | A; Rookie /Mystic /neutral | Demi Wing Nick · Demi Midnight Dive · Demi Violet Echo | 0 /0 | Reviewed |
| 85 Dorumon | A; Rookie /Balanced /neutral | Doru Paw Swipe · Doru Crest Rush · Doru Quartz Pulse | 0 /1 | Optional metal motif |
| 86 DotAgumon | A; Rookie /Balanced /ember | Dot Agu Bit Claw · Dot Agu Block Drop · Dot Agu Ember Pixel | 0 /0 | Reviewed |
| 87 DotFalcomon | A; Rookie /Striker /grove | Dot Falco Bit Peck · Dot Falco Tile Dive · Dot Falco Wind Pixel | 0 /0 | Reviewed |
| 88 Dracmon | A; Rookie /Mystic /neutral | Drac Talon Flick · Drac Mask Pounce · Drac Indigo Wink | 0 /1 | Reviewed |
| 89 Falcomon | A; Rookie /Striker /grove | Falco Hook Peck · Falco Crest Dive · Falco Wind Stitch | 0 /1 | Reviewed |
| 90 Floramon | A; Rookie /Warden /grove | Flora Stem Flick · Flora Root Twist · Flora Pollen Lantern | 0 /0 | Reviewed |
| 25 Gabumon | P; Rookie /Balanced /tide | Horn Jab · Horn Rush · Blue Blaster | 0 /2 | Reviewed; preserved |
| 91 Gaomon | A; Rookie /Striker /neutral | Gao Palm Snap · Gao Shoulder Rush · Gao Ring Spark | 0 /1 | Reviewed |
| 92 Gizamon | A; Rookie /Striker /tide | Giza Fin Hook · Giza Torrent Slam · Giza Brine Flash | 0 /0 | Reviewed |
| 93 Goburimon | A; Rookie /Striker /grove | Gobu Knuckle Tap · Gobu Stump Hammer · Gobu Leaf Rattle | 0 /0 | Reviewed |
| 53 Gomamon | P; Rookie /Balanced /tide | Flipper Slap · Iceberg Rush · Marching Fishes | 0 /2 | Reviewed; preserved |
| 94 Gotsumon | A; Rookie /Bulwark /grove | Gotsu Stone Jab · Gotsu Quarry Roll · Gotsu Flint Glint | 0 /0 | Reviewed |
| 95 Guilmon | A; Rookie /Striker /ember | Guil Talon Cut · Guil Tail Crush · Guil Furnace Wink | 0 /1 | Reviewed |
| 96 Hagurumon | A; Rookie /Bulwark /neutral | Haguru Cog Tap · Haguru Gear Press · Haguru Copper Arc | 0 /1 | Reviewed |
| 97 Hawkmon | A; Rookie /Striker /grove | Hawk Feather Cut · Hawk Wing Pummel · Hawk Horizon Gust | 0 /1 | Reviewed |
| 11 Impmon | P; Rookie /Mystic /ember | Prank Jab · Imp Rush · Night of Fire | 0 /2 | Reviewed; preserved |
| 98 Kamemon | A; Rookie /Bulwark /tide | Kame Shell Jab · Kame Basin Roll · Kame Ripple Beacon | 0 /1 | Reviewed |
| 99 Keramon | A; Rookie /Mystic /neutral | Kera Palm Stretch · Kera Coil Pounce · Kera Bit Lantern | 0 /1 | Reviewed |
| 100 Kotemon | A; Rookie /Warden /neutral | Kote Reed Cut · Kote Oak Cleave · Kote Spirit Ribbon | 0 /0 | Reviewed |
| 101 Kudamon | A; Rookie /Warden /neutral | Kuda Tail Flick · Kuda Ribbon Crush · Kuda Dawn Chime | 0 /1 | Reviewed |
| 102 Kumamon | A; Rookie /Striker /grove | Kuma Paw Hook · Kuma Cap Rush · Kuma Honey Spark | 0 /0 | Reviewed |
| 103 Lalamon | A; Rookie /Mystic /grove | Lala Leaf Tap · Lala Seed Press · Lala Orchard Chord | 0 /1 | Reviewed |
| 104 Lopmon | A; Rookie /Warden /grove | Lop Ear Sweep · Lop Clover Roll · Lop Meadow Chime | 0 /0 | Reviewed |
| 105 Muchomon | A; Rookie /Balanced /ember | Mucho Beak Snap · Mucho Sunset Dive · Mucho Coral Flare | 0 /0 | Reviewed |
| 106 Otamamon | A; Rookie /Mystic /tide | Otama Tail Tap · Otama Lily Lunge · Otama Ripple Chord | 0 /0 | Reviewed |
| 46 Palmon | P; Rookie /Balanced /grove | Vine Lash · Root Slam · Poison Ivy | 0 /2 | Reviewed; preserved |
| 32 Patamon | P; Rookie /Warden /neutral | Wing Slap · Air Tackle · Air Shot | 0 /2 | Reviewed; preserved |
| 107 PawnChessmon Black | A; Rookie /Bulwark /neutral | Black Pawn Spear · Black Pawn File Rush · Black Pawn Rune Grid | 0 /0 | Reviewed |
| 108 PawnChessmon White | A; Rookie /Warden /neutral | White Pawn Point · White Pawn Rank Ram · White Pawn Pearl Grid | 0 /0 | Reviewed |
| 109 Penguinmon | A; Rookie /Balanced /tide | Penguin Flipper Tap · Penguin Ice Slide · Penguin Frost Beads | 0 /0 | Reviewed |
| 60 Renamon | P; Rookie /Mystic /neutral | Palm Strike · Fox Rush · Diamond Storm | 0 /2 | Reviewed; preserved |
| 110 Salamon | A; Rookie /Warden /neutral | Sala Paw Bop · Sala Bell Tackle · Sala Dawn Thread | 0 /1 | Optional remove bell prop |
| 111 ShadowToyAgumon | A; Rookie /Mystic /neutral | Shadow Toy Peg Jab · Shadow Toy Block Ram · Shadow Toy Prism Hex | 0 /0 | Reviewed |
| 112 SnowAgumon | A; Rookie /Bulwark /tide | Snow Agu Claw · Snow Agu Drift Ram · Snow Agu Rime Stars | 0 /0 | Reviewed |
| 113 Tapirmon | A; Rookie /Warden /neutral | Tapir Trunk Nudge · Tapir Pillow Roll · Tapir Slumber Spark | 0 /0 | Reviewed |
| 39 Tentomon | P; Rookie /Bulwark /grove | Shell Tap · Beetle Bash · Super Shocker | 0 /2 | Reviewed; preserved |
| 114 Terriermon | A; Rookie /Balanced /grove | Terrier Ear Snap · Terrier Clover Dive · Terrier Breeze Beads | 0 /1 | Reviewed |
| 115 ToyAgumon | A; Rookie /Bulwark /neutral | Toy Agu Peg Tap · Toy Agu Brick Press · Toy Agu Prism Spray | 0 /0 | Reviewed |
| 116 Tsukaimon | A; Rookie /Mystic /neutral | Tsukai Wing Nick · Tsukai Night Barrel · Tsukai Plum Haze | 0 /0 | Reviewed |
| 117 Veemon | A; Rookie /Striker /tide | Vee Claw Dash · Vee Crest Smash · Vee Sapphire Arc | 0 /1 | Reviewed |
| 118 Wormmon | A; Rookie /Warden /grove | Worm Antenna Tap · Worm Cocoon Roll · Worm Silk Lantern | 0 /1 | Reviewed |
| 119 Airdramon | A; Champion /Mystic /grove | Air Dra Tail Lash · Air Dra Cyclone Ram · Air Dra Cloud Spiral | 0 /0 | Reviewed |
| 120 Akatorimon | A; Champion /Balanced /ember | Akatori Beak Jab · Akatori Crest Slam · Akatori Sunrise Flare | 0 /0 | Reviewed |
| 33 Angemon | P; Champion /Warden /neutral | Staff Strike · Halo Dive · Radiant Fist | 32 /1 | Reviewed; preserved |
| 121 Ankylomon | A; Champion /Bulwark /grove | Ankylo Horn Cut · Ankylo Tail Anvil · Ankylo Quarry Gleam | 78 /0 | Reviewed |
| 122 Apemon | A; Champion /Striker /grove | Ape Staff Sweep · Ape Bamboo Hammer · Ape Jade Shimmer | 0 /0 | Optional remove bamboo material |
| 123 Aquilamon | A; Champion /Striker /grove | Aquila Talon Cross · Aquila Gale Dive · Aquila Horizon Ring | 97 /1 | Reviewed |
| 124 Bakemon | A; Champion /Mystic /neutral | Bake Cloth Flick · Bake Phantom Drop · Bake Lantern Shroud | 0 /0 | Damage-label clarity |
| 125 Birdramon | A; Champion /Striker /ember | Birdra Talon Rake · Birdra Furnace Dive · Birdra Ember Garland | 81 /1 | Reviewed |
| 126 DarkLizardmon | A; Champion /Mystic /ember | Dark Lizard Rake · Dark Lizard Coal Ram · Dark Lizard Ash Halo | 0 /0 | Reviewed |
| 127 DarkTyrannomon | A; Champion /Striker /ember | Dark Tyranno Bite · Dark Tyranno Heel · Dark Tyranno Cinder | 0 /0 | Reviewed |
| 15 Devimon | P; Champion /Mystic /ember | Wing Rake · Shadow Lunge · Void Claw | 11 /1 | Reviewed; preserved |
| 128 Diatrymon | A; Champion /Striker /grove | Diatry Beak Cut · Diatry Boulder Dash · Diatry Dust Vortex | 0 /0 | Reviewed |
| 129 Dinohyumon | A; Champion /Striker /neutral | Dino Blade Cross · Dino Cleaver Rush · Dino Steel Gleam | 0 /0 | Reviewed |
| 130 Dorugamon | A; Champion /Balanced /neutral | Doruga Talon Sweep · Doruga Crest Break · Doruga Quartz Gale | 85 /1 | Optional metal motif |
| 131 ExVeemon | A; Champion /Striker /tide | ExVee Elbow Rush · ExVee Wing Driver · ExVee Azure Cross | 117 /1 | Reviewed |
| 132 Flarerizamon | A; Champion /Mystic /ember | Flare Lizard Claw · Flare Lizard Ram · Flare Lizard Torch | 0 /0 | Reviewed |
| 133 Gaogamon | A; Champion /Striker /neutral | Gaoga Paw Cross · Gaoga Shoulder Gale · Gaoga Ring Flurry | 91 /1 | Reviewed |
| 134 Gargomon | A; Champion /Striker /neutral | Gargo Knuckle Tap · Gargo Barrel Ram · Gargo Copper Spray | 114 /1 | Reviewed |
| 26 Garurumon | P; Champion /Balanced /tide | Fang Snap · Wolf Rush · Frost Howl | 25 /1 | Reviewed; preserved |
| 135 Gatomon | A; Champion /Warden /neutral | Gato Paw Cross · Gato Tail Vault · Gato Golden Thread | 110 /1 | Reviewed |
| 136 Gekomon | A; Champion /Mystic /tide | Geko Palm Slap · Geko Bass Tackle · Geko Reed Crescendo | 0 /0 | Reviewed |
| 137 GeoGreymon | A; Champion /Striker /ember | Geo Grey Horn Cut · Geo Grey Crest Rush · Geo Grey Furnace Arc | 0 /0 | Reviewed |
| 19 Greymon | P; Champion /Balanced /ember | Horn Sweep · Tyrant Charge · Blazing Breath | 18 /1 | Reviewed; preserved |
| 138 Grizzmon | A; Champion /Striker /grove | Grizz Paw Hook · Grizz Timber Press · Grizz Amber Roar | 0 /0 | Reviewed |
| 139 Growlmon | A; Champion /Striker /ember | Growl Talon Cut · Growl Blade Driver · Growl Cinder Pulse | 95 /1 | Reviewed |
| 140 Guardromon | A; Champion /Bulwark /neutral | Guardro Rivet Jab · Guardro Foundry Ram · Guardro Beacon Arc | 96 /1 | Reviewed |
| 141 Gwappamon | A; Champion /Mystic /tide | Gwappa Paddle Tap · Gwappa Drum Roll · Gwappa Lagoon Chord | 98 /0 | Reviewed |
| 142 Hookmon | A; Champion /Striker /tide | Hook Deck Cut · Hook Anchor Drop · Hook Brine Lantern | 0 /0 | Optional grounded hook weapon |
| 143 Icemon | A; Champion /Bulwark /tide | Ice Shard Jab · Ice Glacier Roll · Ice Polar Glimmer | 0 /0 | Reviewed |
| 54 Ikkakumon | P; Champion /Bulwark /tide | Tusk Jab · Icebreaker · Harpoon Burst | 53 /1 | Reviewed; preserved |
| 40 Kabuterimon | P; Champion /Bulwark /grove | Horn Thrust · Shell Break · Thunder Orb | 39 /1 | Reviewed; preserved |
| 144 Kiwimon | A; Champion /Balanced /grove | Kiwi Beak Tap · Kiwi Root Dash · Kiwi Orchard Puff | 0 /0 | Reviewed |
| 145 Kokatorimon | A; Champion /Striker /grove | Kokatori Beak Hook · Kokatori Crest Press · Kokatori Jade Wink | 0 /0 | Reviewed |
| 146 Kurisarimon | A; Champion /Bulwark /neutral | Kurisari Tendril · Kurisari Shell Press · Kurisari Bit Shroud | 99 /1 | Damage-label clarity |
| 43 Kuwagamon | P; Champion /Striker /grove | Pincer Cut · Scissor Rush · Sonic Edge | 39 /1 | Reviewed; preserved |
| 61 Kyubimon | P; Champion /Mystic /neutral | Fox Claw · Spiral Rush · Spirit Flame | 60 /1 | Reviewed; preserved |
| 29 Leomon | P; Champion /Balanced /tide | Lion Palm · Pride Rush · Roaring Fist | 25 /1 | Reviewed; preserved |
| 147 Minotarumon | A; Champion /Striker /grove | Minotaur Horn Jab · Minotaur Stone Crush · Minotaur Dust Halo | 0 /0 | Reviewed |
| 148 Numemon | A; Champion /Bulwark /neutral | Nume Slime Slap · Nume Muck Barrel · Nume Bog Bubbles | 0 /0 | Reviewed |
| 149 Ogremon | A; Champion /Striker /grove | Ogre Club Sweep · Ogre Boulder Split · Ogre Moss Rumble | 0 /0 | Reviewed |
| 150 Peckmon | A; Champion /Striker /grove | Peck Heel Snap · Peck Crest Driver · Peck Wind Ribbons | 89 /1 | Reviewed |
| 151 PlatinumSukamon | A; Champion /Bulwark /neutral | Platina Slime Tap · Platina Ingots Roll · Platina Mirror Haze | 0 /0 | Reviewed |
| 152 Raremon | A; Champion /Bulwark /neutral | Rare Ooze Swipe · Rare Muck Avalanche · Rare Violet Vapour | 0 /0 | Reviewed |
| 153 Reppamon | A; Champion /Striker /grove | Reppa Tail Cross · Reppa Branch Rush · Reppa Gale Lattice | 101 /1 | Reviewed |
| 154 Reptiledramon | A; Champion /Striker /neutral | Reptile Talon Cut · Reptile Alloy Ram · Reptile Chrome Arc | 0 /0 | Reviewed |
| 155 Roachmon | A; Champion /Striker /grove | Roach Leg Hook · Roach Shell Driver · Roach Copper Dust | 0 /0 | Reviewed |
| 156 Sangloupmon | A; Champion /Mystic /neutral | Sangloup Fang Cut · Sangloup Night Rush · Sangloup Crimson Haze | 88 /1 | Reviewed |
| 157 Seadramon | A; Champion /Balanced /tide | Sea Dra Tail Sweep · Sea Dra Reef Crush · Sea Dra Lagoon Coil | 80 /1 | Reviewed |
| 158 Seasarmon | A; Champion /Warden /neutral | Seasar Paw Cross · Seasar Bell Rush · Seasar Golden Mist | 0 /0 | Reviewed |
| 159 Sorcerymon | A; Champion /Mystic /tide | Sorcery Staff Tap · Sorcery Frost Hammer · Sorcery Rime Runes | 0 /0 | Reviewed |
| 160 Starmon | A; Champion /Warden /neutral | Star Point Jab · Star Meteor Press · Star Orbit Garland | 0 /0 | Reviewed |
| 161 Stingmon | A; Champion /Striker /grove | Sting Wrist Cut · Sting Thorn Driver · Sting Emerald Trail | 118 /1 | Reviewed |
| 162 Sukamon | A; Champion /Bulwark /neutral | Suka Slime Jab · Suka Muck Boulder · Suka Amber Haze | 0 /0 | Reviewed |
| 163 Sunflowmon | A; Champion /Mystic /grove | Sunflow Stem Swipe · Sunflow Root Wheel · Sunflow Pollen Corona | 103 /1 | Reviewed |
| 47 Togemon | P; Champion /Bulwark /grove | Cactus Jab · Needle Slam · Thorn Volley | 46 /1 | Reviewed; preserved |
| 22 Tyrannomon | P; Champion /Striker /ember | Tail Swipe · Dino Stomp · Ember Roar | 18 /1 | Reviewed; preserved |
| 36 Unimon | P; Champion /Warden /neutral | Horn Strike · Cloud Charge · Sky Bolt | 32 /1 | Reviewed; preserved |
| 164 Vegiemon | A; Champion /Mystic /grove | Vegi Tendril Tap · Vegi Root Knot · Vegi Orchard Fog | 0 /0 | Reviewed |
| 165 Vilemon | A; Champion /Mystic /neutral | Vile Wing Rake · Vile Dusk Pounce · Vile Midnight Chime | 0 /0 | Reviewed |
| 12 Wizardmon | P; Champion /Mystic /ember | Staff Tap · Rune Crash · Hex Spark | 11 /1 | Reviewed; preserved |
| 166 Andromon | A; Ultimate /Bulwark /neutral | Servo Knuckle · Piston Bodycheck · Circuit Lance | 140 /1 | Reviewed |
| 167 Angewomon | A; Ultimate /Warden /neutral | Feather Palm · Halo Descent · Sanctuary Arrow | 135 /0 | Reviewed |
| 168 Antylamon | A; Ultimate /Balanced /grove | Rabbit Palm · Moonbound Heel · Jade Spiral | 0 /0 | Separate identity review pending |
| 169 Arukenimon | A; Ultimate /Mystic /grove | Silken Hook · Webbed Pounce · Marionette Mist | 0 /0 | Reviewed |
| 170 BlackRapidmon | A; Ultimate /Warden /neutral | Onyx Cuff · Nightguard Ram · Obsidian Pulse | 0 /0 | Reviewed |
| 171 BlackWarGrowlmon | A; Ultimate /Bulwark /ember | Sootsteel Claw · Black Reactor Tackle · Charred Plasma | 0 /0 | Reviewed |
| 172 BlackWereGarurumon | A; Ultimate /Striker /tide | Midnight Wolf Kick · Shadow Crescent Heel · Frostbite Howl | 0 /0 | Reviewed |
| 173 Blossomon | A; Ultimate /Bulwark /grove | Bramble Rake · Bloom Wheel Crush · Pollen Lantern | 0 /0 | Reviewed |
| 174 Cyberdramon | A; Ultimate /Striker /neutral | Data Ripper · Cyber Talon Dive · Error Surge | 0 /0 | Reviewed |
| 175 Deramon | A; Ultimate /Warden /grove | Crested Peck · Garden Wing Press · Fernlight Veil | 0 /0 | Damage-label clarity |
| 176 Digitamamon | A; Ultimate /Bulwark /neutral | Eggshell Bump · Shellbound Slam · Dreamshell Spark | 0 /0 | Reviewed |
| 177 Dinobeemon | A; Ultimate /Striker /grove | Stinger Claw · Predator Wingrush · Venomlight Ray | 161 /0 | Reviewed |
| 178 Divermon | A; Ultimate /Balanced /tide | Harpoon Jab · Deepsea Shoulder · Pressure Ring | 0 /0 | Reviewed |
| 179 DoruGreymon | A; Ultimate /Striker /neutral | Iron Fur Rake · Doru Tailbreaker · Meteor Code | 130 /1 | Reviewed |
| 180 Dragomon | A; Ultimate /Mystic /tide | Abyss Tendril · Tentacle Undertow · Sunken Invocation | 0 /0 | Reviewed |
| 181 Etemon | A; Ultimate /Balanced /ember | Stage Elbow · Encore Bodyslam · Distortion Chorus | 0 /0 | Reviewed |
| 182 Garbagemon | A; Ultimate /Bulwark /grove | Scrap Lid Swipe · Trash Barrel Crush · Sludge Comet | 0 /0 | Reviewed |
| 183 Garudamon | A; Ultimate /Striker /ember | Eagle Talon · Sunwing Tackle · Crimson Updraft | 125 /1 | Reviewed |
| 184 Gigadramon | A; Ultimate /Bulwark /neutral | Giga Metal Claw · Armored Skycrash · Siege Cluster | 0 /0 | Reviewed |
| 185 Infermon | A; Ultimate /Mystic /neutral | Cable Fang · Virus Bodydrill · Firewall Rupture | 146 /1 | Reviewed |
| 186 Kimeramon | A; Ultimate /Balanced /neutral | Chimera Rake · Composite Stampede · Hybrid Ruinbeam | 0 /0 | Reviewed |
| 187 Kyukimon | A; Ultimate /Striker /grove | Sickle Sweep · Whirlwind Scissor · Petal Razor Gale | 0 /0 | Reviewed |
| 188 LadyDevimon | A; Ultimate /Mystic /neutral | Dusk Nail · Blackwing Lunge · Velvet Nightflame | 0 /0 | Reviewed |
| 189 Lilamon | A; Ultimate /Mystic /grove | Lilac Fan · Petal Pirouette · Nectar Starfall | 163 /0 | Reviewed |
| 48 Lillymon | P; Ultimate /Mystic /grove | Petal Strike · Bloom Rush · Floral Ray | 47 /1 | Reviewed; preserved |
| 190 Lucemon Chaos Mode | A; Ultimate /Mystic /neutral | Fallen Halo Fist · Judgment Knee · Dawnless Mandala | 0 /0 | Reviewed |
| 191 MachGaogamon | A; Ultimate /Striker /neutral | Jet Boxer Jab · Turbine Cross · Sonic Slipstream | 133 /0 | Reviewed |
| 34 MagnaAngemon | P; Ultimate /Warden /neutral | Holy Edge · Gate Rush · Radiant Seal | 33 /1 | Reviewed; preserved |
| 192 Mamemon | A; Ultimate /Balanced /neutral | Tiny Iron Jab · Orbital Knuckle · Pocket Burst | 0 /0 | Reviewed |
| 193 Matadormon | A; Ultimate /Striker /neutral | Crimson Rapier · Cape Vault Thrust · Nocturne Petals | 156 /0 | Rapier concern checked and dismissed |
| 194 Megadramon | A; Ultimate /Striker /ember | Missile Claw · Steel Dragon Rush · Payload Inferno | 0 /0 | Reviewed |
| 195 MegaKabuterimon Blue | A; Ultimate /Warden /tide | Cobalt Horn · Blue Carapace Ram · Azure Volt Ring | 0 /0 | Reviewed |
| 41 MegaKabuterimon Red | P; Ultimate /Bulwark /grove | Crimson Horn · Atlas Charge · Volt Cannon | 40 /1 | Reviewed; preserved |
| 196 MegaSeadramon | A; Ultimate /Mystic /tide | Thunderfin Lash · Stormcoil Crush · Ocean Voltage | 157 /2 | Reviewed |
| 20 MetalGreymon | P; Ultimate /Striker /ember | Steel Claw · Trident Crash · Missile Flare | 19 /1 | Reviewed; preserved |
| 197 MetalMamemon | A; Ultimate /Bulwark /neutral | Clamp Knuckle · Foundry Punch · Micro Cannon Arc | 0 /0 | Reviewed |
| 198 Meteormon | A; Ultimate /Bulwark /neutral | Crater Knuckle · Meteor Shoulderfall · Stardust Shards | 0 /0 | Reviewed |
| 199 Monzaemon | A; Ultimate /Warden /neutral | Plush Paw · Bearhug Roll · Heartlight Beacon | 0 /0 | Reviewed |
| 200 Mummymon | A; Ultimate /Warden /neutral | Bandage Hook · Sarcophagus Slam · Desert Hexbolt | 0 /0 | Optional established equipment |
| 16 Myotismon | P; Ultimate /Mystic /ember | Cape Slash · Midnight Crush · Bat Swarm | 15 /1 | Reviewed; preserved |
| 44 Okuwamon | P; Ultimate /Striker /grove | Obsidian Claw · Hollow Cleave · Void Cutter | 43 /1 | Reviewed; preserved |
| 201 Paildramon | A; Ultimate /Balanced /neutral | Dragon Gauntlet · Twinwing Collision · Desperado Starburst | 131 /1 | Reviewed |
| 202 Parrotmon | A; Ultimate /Mystic /grove | Emerald Beak · Jungle Wingbeat · Thunder Mimicry | 0 /0 | Reviewed |
| 203 Piximon | A; Ultimate /Mystic /grove | Pixie Spear · Fairy Vault · Bubble Rune Shower | 0 /0 | Reviewed |
| 204 Rapidmon | A; Ultimate /Balanced /grove | Emerald Cuff · Flashstep Ram · Guided Lightshot | 134 /1 | Reviewed |
| 205 RizeGreymon | A; Ultimate /Striker /ember | Barrel Claw · Revolver Shoulder · Rising Plasma | 0 /0 | Reviewed |
| 206 ShogunGekomon | A; Ultimate /Warden /tide | Toad Fan · Royal Bellypress · Resonant Reedcall | 0 /0 | Reviewed |
| 207 Silphymon | A; Ultimate /Balanced /grove | Sky Talon · Aerial Twinstrike · Cyclone Sigil | 123 /1 | Reviewed |
| 208 SkullGreymon | A; Ultimate /Striker /ember | Bone Ripper · Fossil Stampede · Gravefire Rocket | 0 /0 | Reviewed |
| 209 SuperStarmon | A; Ultimate /Warden /neutral | Astral Palm · Goldstar Bodyslam · Constellation Shieldray | 0 /0 | Damage-label clarity |
| 62 Taomon | P; Ultimate /Mystic /neutral | Talisman Swipe · Seal Rush · Radiant Script | 61 /1 | Reviewed; preserved |
| 210 Triceramon | A; Ultimate /Bulwark /grove | Triple Horn Jab · Ceratopsian Charge · Granite Hornwave | 0 /0 | Reviewed |
| 211 Tyilinmon | A; Ultimate /Warden /neutral | Cloudhoof Tap · Auspicious Bound · Luminous Qilin Seal | 153 /1 | Reviewed |
| 212 WarGrowlmon | A; Ultimate /Striker /ember | Assault Claw · Reactor Bodyram · Crimson Plasma Jet | 139 /1 | Reviewed |
| 27 WereGarurumon | P; Ultimate /Striker /tide | Wolf Kick · Crescent Rush · Moon Pulse | 26 /1 | Reviewed; preserved |
| 58 Whamon | P; Ultimate /Warden /tide | Fin Sweep · Deepwater Crush · Ocean Surge | 57 /1 | Reviewed; preserved |
| 213 Yatagaramon | A; Ultimate /Mystic /neutral | Raven Claw · Threewing Plunge · Omen Featherstorm | 150 /0 | Correct three-wing anatomy |
| 55 Zudomon | P; Ultimate /Bulwark /tide | Hammer Tap · Glacier Smash · Thunder Tide | 54 /1 | Reviewed; preserved |
| 214 Armagemon | A; Mega /Bulwark /neutral | Fortress Pincer · Armageddon Bodypress · Apocalypse Webbeam | 0 /0 | Reviewed |
| 215 Azulongmon | A; Mega /Warden /tide | Azure Whisker Lash · Celestial Dragon Coil · Eastern Thunderseal | 0 /0 | Reviewed |
| 216 Babamon | A; Mega /Warden /grove | Broom Handle Tap · Elder Sweepfall · Hearth Blessing | 0 /0 | Damage-label clarity |
| 217 Baihumon | A; Mega /Striker /neutral | White Tiger Rend · Western Fangrush · Steel Roar Halo | 0 /0 | Reviewed |
| 31 BanchoLeomon | P; Mega /Striker /tide | Brave Fist · King Rush · Justice Roar | 30 /0 | Reviewed; preserved |
| 218 Barbamon | A; Mega /Mystic /ember | Gilded Staffhook · Treasure Vault Crash · Avarice Emberseal | 0 /0 | Reviewed |
| 14 Beelzemon | P; Mega /Striker /ember | Claw Sweep · Demon Rush · Night Barrage | 13 /0 | Reviewed; preserved |
| 219 Belphemon | A; Mega /Bulwark /neutral | Drowsy Claw · Slumbering Colossus · Nightmare Resonance | 0 /0 | Resolve mode before label specificity |
| 220 BlackImperialdramon | A; Mega /Bulwark /neutral | Obsidian Dragon Rake · Black Citadel Charge · Eclipse Megaflare | 0 /0 | Reviewed |
| 221 BlackMegaGargomon | A; Mega /Bulwark /neutral | Nightsteel Gauntlet · Dark Arsenal Stampede · Onyx Missile Rain | 0 /0 | Reviewed |
| 222 BlackWarGreymon | A; Mega /Striker /neutral | Obsidian Dramon Claw · Black Tornado Drive · Dark Terra Sphere | 0 /0 | Reviewed |
| 223 Cannondramon | A; Mega /Bulwark /ember | Gunmetal Tail · Artillery Bodyslam · Twin Siege Salvo | 0 /0 | Reviewed |
| 224 ChaosGallantmon | A; Mega /Mystic /neutral | Chaos Lancehook · Dark Knight Charge · Ruinshield Eclipse | 0 /0 | Reviewed |
| 225 Cherubimon Vaccine | A; Mega /Warden /neutral | Merciful Palm · Sanctuary Bound · Golden Choir Halo | 0 /0 | Reviewed |
| 226 Cherubimon Virus | A; Mega /Mystic /neutral | Malice Talon · Nightmare Vault · Black Choir Storm | 0 /0 | Reviewed |
| 227 Chronomon DM | A; Mega /Striker /ember | Doomwing Talon · Dark Chrono Dive · Eventide Timeflare | 0 /0 | Reviewed |
| 228 Chronomon Holy Mode | A; Mega /Warden /neutral | Dawnwing Talon · Holy Chrono Descent · Timeless Aurora | 0 /0 | Reviewed |
| 229 Creepymon | A; Mega /Mystic /ember | Infernal Nail · Abyssal Wingcrash · Creeping Hellstar | 0 /0 | Reviewed |
| 230 Crossmon | A; Mega /Striker /neutral | Crossmetal Talon · Golden Wingbreaker · Skyforge Radiance | 0 /0 | Reviewed |
| 231 Daemon | A; Mega /Warden /ember | Cinder Scepter · Demon Mantle Rush · Dread Sovereign Sigil | 0 /0 | Reviewed |
| 232 Darkdramon | A; Mega /Striker /neutral | Dark Lance Jab · Dreadnought Thrust · Abyss Energy Lance | 0 /0 | Reviewed |
| 233 Diaboromon | A; Mega /Mystic /neutral | Network Claw · Cable Webcrush · Kernel Collapse | 185 /0 | Reviewed |
| 234 Dorugoramon | A; Mega /Striker /neutral | Primeval Doru Claw · Ironfur Cataclysm · Dragon Code Nova | 179 /0 | Reviewed |
| 235 Ebonwumon | A; Mega /Bulwark /grove | Ancient Rootpress · Northern Shellquake · Evergreen Twinlight | 0 /0 | Reviewed |
| 236 Gallantmon | A; Mega /Balanced /ember | Crimson Lance · Royal Shieldcharge · Radiant Javelin | 212 /0 | Reviewed |
| 237 Gallantmon Crimson Mode | A; Mega /Striker /ember | Crimson Wingblade · Scarlet Knightfall · Royal Star Ignition | 0 /0 | Reviewed |
| 238 Ghoulmon | A; Mega /Mystic /neutral | Spectral Grasp · Phantom Wingpress · Pale Eye Eclipse | 0 /0 | Reviewed |
| 239 Ghoulmon Black | A; Mega /Warden /neutral | Obsidian Grasp · Black Phantom Descent · Umbral Eye Lantern | 0 /0 | Reviewed |
| 240 GigaSeadramon | A; Mega /Bulwark /tide | Battleship Fin · Giga Hullcrash · Ocean Missile Array | 196 /0 | Reviewed |
| 45 GranKuwagamon | P; Mega /Striker /grove | Grand Pincer · Dimension Rush · Abyss Edge | 44 /0 | Reviewed; preserved |
| 241 Granlocomon | A; Mega /Bulwark /ember | Locomotive Ram · Grand Railbreaker · Furnace Railbeam | 0 /0 | Reviewed |
| 242 GuardiAngemon | A; Mega /Warden /neutral | Guardian Blade · Sanctified Shieldfall · Silver Gate Radiance | 0 /0 | Prefer established blades |
| 42 HerculesKabuterimon | P; Mega /Bulwark /grove | Titan Horn · Hercules Crush · Storm Nova | 41 /0 | Reviewed; preserved |
| 243 HiAndromon | A; Mega /Bulwark /neutral | Titanium Knuckle · Precision Piston Slam · Optimizer Beam | 166 /0 | Reviewed |
| 244 Imperialdramon Dragon Mode | A; Mega /Bulwark /ember | Imperial Dragon Claw · Dragon Citadel Dive · Positron Sunflare | 201 /0 | Reviewed |
| 245 Imperialdramon Fighter Mode | A; Mega /Balanced /ember | Imperial Fistblade · Fighter Mode Blitz · Positron Starburst | 0 /0 | Reviewed |
| 246 Imperialdramon Paladin Mode | A; Mega /Warden /neutral | Paladin Swordarc · White Knight Descent · Omega Dawnseal | 0 /0 | Reviewed |
| 247 Jijimon | A; Mega /Mystic /grove | Elder Staff Tap · Wisdom Cane Crush · Ancient Hearthstar | 0 /0 | Reviewed |
| 248 Justimon | A; Mega /Striker /neutral | Justice Knuckle · Heroic Rocketheel · Trinity Voltage | 0 /0 | Reviewed |
| 249 Kentaurosmon | A; Mega /Warden /tide | Aurora Hoofstrike · Sixleg Shieldrush · Northern Light Arrow | 211 /0 | Reviewed |
| 66 Kuzuhamon | P; Mega /Mystic /neutral | Sacred Staff · Twilight Descent · Veiled Mandala | 65 /0 | Reviewed; preserved |
| 250 Leviamon | A; Mega /Bulwark /tide | Abyss Crocodile Bite · Tsunami Jawpress · Envious Ocean Roar | 0 /0 | Reviewed |
| 251 Lilithmon | A; Mega /Mystic /neutral | Tempting Nail · Velvet Wingfall · Violet Sinmist | 0 /0 | Reviewed |
| 252 Machinedramon | A; Mega /Bulwark /ember | Foundry Claw · Iron Fortress Trample · Infinity Furnace | 0 /0 | Reviewed |
| 253 MaloMyotismon | A; Mega /Mystic /neutral | Crimson Doomclaw · Nightlord Wingcrush · Pandemonium Lantern | 0 /0 | Reviewed |
| 254 MarineAngemon | A; Mega /Warden /tide | Tiny Ocean Nudge · Bubble Halo Roll · Peaceful Tideheart | 0 /0 | Reviewed |
| 255 MegaGargomon | A; Mega /Bulwark /grove | Green Arsenal Fist · Heavy Rabbit Stampede · Emerald Rocket Bloom | 204 /0 | Reviewed |
| 256 MetalEtemon | A; Mega /Bulwark /neutral | Chrome Stage Fist · Platinum Encore Slam · Metal Feedback Wave | 0 /0 | Reviewed |
| 28 MetalGarurumon | P; Mega /Striker /tide | Steel Fang · Glacier Charge · Ice Barrage | 27 /0 | Reviewed; preserved |
| 257 MetalSeadramon | A; Mega /Bulwark /tide | Alloy Serpent Bite · Iron Tidal Coil · Deepsea Reactor Ray | 196 /0 | Reviewed |
| 258 Neptunemon | A; Mega /Balanced /tide | Trident Hook · Ocean King Thrust · Sovereign Whirlpool | 0 /0 | Reviewed |
| 259 Omnimon | A; Mega /Balanced /neutral | Twin Knight Slash · United Swordrush · Transcendent Cannon | 0 /0 | Reviewed |
| 260 Parasimon | A; Mega /Mystic /grove | Parasite Needle · Tendril Hostcrush · Spore Command | 0 /0 | Damage-label clarity |
| 261 Phoenixmon | A; Mega /Warden /ember | Phoenix Talon · Rebirth Wingpress · Sacred Cinder Halo | 183 /0 | Reviewed |
| 262 Piedmon | A; Mega /Mystic /neutral | Jester Rapier · Acrobat Bladefall · Masquerade Mirage | 0 /0 | Reviewed |
| 59 Plesiomon | P; Mega /Mystic /tide | Neck Sweep · Abyss Ram · Sorrow Tide | 58 /0 | Reviewed; preserved |
| 263 PrinceMamemon | A; Mega /Balanced /neutral | Royal Little Jab · Crown Knuckle Rush · Gilded Bubble Parade | 0 /0 | Reviewed |
| 52 Puppetmon | P; Mega /Bulwark /grove | Wooden Hammer · Marionette Slam · String Storm | 51 /0 | Reviewed; preserved |
| 49 Rosemon | P; Mega /Mystic /grove | Rose Lash · Thorn Waltz · Crimson Bloom | 48 /0 | Reviewed; preserved |
| 264 SaberLeomon | A; Mega /Striker /grove | Saber Fang Rake · Primeval Lion Pounce · Golden Mane Roar | 0 /0 | Reviewed |
| 63 Sakuyamon | P; Mega /Mystic /neutral | Ritual Staff · Spirit Descent · Celestial Seal | 62 /0 | Reviewed; preserved |
| 35 Seraphimon | P; Mega /Warden /neutral | Seraph Edge · Heavenfall · Sevenfold Ray | 34 /0 | Reviewed; preserved |
| 265 Valkyrimon | A; Mega /Striker /tide | Valkyrie Swordcut · Battlemaiden Spearfall · Runic Winter Arrow | 207 /0 | Prefer established sword |
| 266 Varodurumon | A; Mega /Warden /neutral | Radiant Feather Sweep · Sixwing Skypress · Purifying Dawnlight | 0 /0 | Reviewed |
| 17 VenomMyotismon | P; Mega /Bulwark /ember | Venom Rake · Abyss Stomp · Toxic Eclipse | 16 /0 | Reviewed; preserved |
| 56 Vikemon | P; Mega /Bulwark /tide | Mace Sweep · Viking Crash · Polar Tempest | 55 /0 | Reviewed; preserved |
| 21 WarGreymon | P; Mega /Striker /ember | Drill Claw · Brave Rush · Terra Blaze | 20 /0 | Reviewed; preserved |
| 267 Zhuqiaomon | A; Mega /Mystic /ember | Vermilion Beak · Southern Wingcrash · Sovereign Firewheel | 0 /0 | Reviewed |
| 268 Flamedramon | A; Armor /Striker /ember | Fire Gauntlet · Blazing Armor Rush · Flame Crest Spiral | 0 /0 | Reviewed |
| 269 Kenkimon | A; Armor /Bulwark /neutral | Excavator Hook · Digger Chassis Ram · Quarry Dustbeam | 0 /0 | Reviewed |
| 270 Kongoumon | A; Armor /Bulwark /grove | Golden Beetle Palm · Diamond Shellpress · Vajra Pollen Spark | 0 /0 | Reviewed |
| 271 Magnamon | A; Armor /Warden /neutral | Golden Guardfist · Miracle Armor Charge · Radiant Crestburst | 0 /0 | Reviewed |
| 272 Ponchomon | A; Armor /Mystic /grove | Maraca Tap · Poncho Spinpress · Cactus Festival Mist | 0 /0 | Reviewed |
| 273 Prairiemon | A; Armor /Striker /grove | Prairie Claw · Burrow Shouldercharge · Dustgrass Spiral | 0 /0 | Reviewed |
| 274 Seahomon | A; Armor /Warden /tide | Coral Tailflick · Reefcoil Tackle · Seahorse Crestlight | 0 /0 | Reviewed |
| 275 Shurimon | A; Armor /Striker /grove | Leaf Shuriken · Pinwheel Heelrush · Verdant Ninja Gust | 0 /0 | Reviewed |
| 276 Calumon | A; No Level /Warden /neutral | Playful Ear Tap · Tumbling Somersault · Gentle Crestglow | 0 /0 | Reviewed |

Audit input SHA-256 values:

| File | SHA-256 |
| --- | --- |
| `data/world-ds-catalog.json` | `b9b75cb3740137dab6f14044bd0fe86192b5fe0c11f32fa4f97eec61d50d5906` |
| `data/world-ds-runtime.json` | `04001b55c60f35d46e00f979c47464919a3dabeab2c3e2c84962aa4050fd1d46` |
| `data/world-ds-evolutions.json` | `32fcc22d57bd0b4827d2893dbd452f3f3f10ff696a4d2e342306520f70a646b8` |
| `docs/research/world-ds-official-early.json` | `d018d99e52ba08a28f0ad4ace2a2aa539dc2a7477fc2f8335772f1f3a97a4712` |
| `docs/research/world-ds-official-late.json` | `a72c112c3bbec05a367beac03c8f89c14c3241ad8afecb08cab4cd740c01ae68` |

## Applied corrections — follow-up, 2026-10-06

The audit and complete ledger above preserve the original `a352335` review state. The implementation owner has now applied exactly these three Heavy-label corrections and regenerated the catalog/runtime exports and full CSV:

| Form /species | Previous Heavy label | Current Heavy label |
| --- | --- | --- |
| 213 Yatagaramon | Threewing Plunge | **Threeclaw Plunge** |
| 242 GuardiAngemon | Sanctified Shieldfall | **Sanctified Bladefall** |
| 265 Valkyrimon | Battlemaiden Spearfall | **Battlemaiden Swordfall** |

Those three findings are **fixed**, not outstanding recommendations. Yatagaramon's corrected anatomy wording does not settle its still-unresolved sheet variant.

Antylamon (form168) is now explicitly marked `official-candidates-verified-sheet-variant-unresolved`, with `sheetVariantReviewRequired: true`. The Virus and Deva/Data references remain separate candidates; fields that would select one identity are null at the unresolved source-entry level. This fixes the prior overconfident metadata selection, **not** the underlying sheet identification. Its authored skills and stats are unchanged.

All stats, stable IDs, evolution edges, and rules versions remain unchanged. The implementation owner reports **3,877 native catalog checks and nine generator tests passed** after regeneration. This documentation-only follow-up did not rerun those tests or modify source data.

The remaining optional prop/motif and damage-wording suggestions, Belphemon mode question, shared-stat limits, incomplete routes, and evolution tradeoffs are still open as described above. No additional label or numeric change is implied. This content audit also does not attest to the separate source-sheet download workflow or to inspection of all artwork.

Current input SHA-256 values after these corrections; the original audit hashes above are intentionally retained:

| File | SHA-256 |
| --- | --- |
| `data/world-ds-catalog.json` | `e8204ee6d8ce8bbe9de2c24e9a0ed7e275b0ef498fce1c3721fc97b43c633f10` |
| `data/world-ds-runtime.json` | `e0037a31b4adcfa0999cffd8230116b66d875097710421c7d3b6d4d290510a51` |
| `data/world-ds-evolutions.json` | `32fcc22d57bd0b4827d2893dbd452f3f3f10ff696a4d2e342306520f70a646b8` |
| `docs/research/world-ds-official-early.json` | `d018d99e52ba08a28f0ad4ace2a2aa539dc2a7477fc2f8335772f1f3a97a4712` |
| `docs/research/world-ds-official-late.json` | `613e5999d32e7b3f381a8462fae1bdc22042f4c52201d3d7641b09794b575553` |
