# World DS role, budget and skill-identity review

**Retain the current numeric profiles.** This pass found no evidence strong enough to justify a per-species stat, role-growth or combat-tier correction. It did find worthwhile label clarifications and several role/type choices that should be described as deliberate game design rather than canonical facts. No gameplay data or skill labels were changed.

The [complete review matrix](world-ds-role-identity-review.json) covers all **255 source entries / 765 skill labels**, separating **210 authored additions** from **45 preserved bindings**. Every addition has an individual role/type rationale; every row includes actual entry-level stats, growth, budget, official-reference evidence, three slot assessments and concrete proposal references. The input hashes identify the reviewed files. These are source-entry identities, not a claim of 255 canonically distinct species.

The later rules-6/catalog-2 graph integration was checked against all 255 review rows: skill labels, types, combat tiers, runtime roles, entry-level stats, growth and stat models remain identical. The recorded input hashes intentionally identify the pre-graph review baseline; they are not hashes of the later graph-enriched files. No editorial proposal below has been applied.

## What the roles establish

All 210 new entry-level profiles conform to their declared stage budgets. Their 28 complete numeric curves and 78 curve/type combinations remain a reasonable shared-role design: Striker exchanges magic for physical offense, Mystic does the reverse, and Bulwark/Warden favor opposite defenses. Balanced keeps both offensive channels. All 255 variation vectors are zero; that is disclosed, not treated as a defect requiring arbitrary individual offsets. The [existing numerical design](world-ds-balance-design.md) and native balance evidence (historical local evidence omitted) remain the relevant arithmetic and matchup sources.

The matrix records 184 new-role choices as plausible motif fits, 26 as explicit design choices with weaker role-specific grounding, and 45 as preserved profiles. This is **not a canonical-role score or new win-rate measurement**. A shell or armored chassis is a stronger basis for Bulwark than a color variant alone; neither official taxonomy nor an attack name specifies an exact Defense or Resistance value.

Several choices are reasonable but discretionary:

- BlackRapidmon's Warden versus Rapidmon's Balanced, Ghoulmon Black's Warden versus Ghoulmon's Mystic, and Daemon's Warden versus Creepymon's Mystic are deliberate contrasts. The recorded evidence does not prove those resistance differences. They need honest authoring rationales, not forced numerical changes or silent alias merges.
- Numemon, Raremon and Sukamon use durable-slime Bulwark interpretations. Bulk makes HP plausible, but does not independently establish armor. Keeping them is defensible as game design.
- Armor uses an explicit Champion-tier policy. Calumon's Rookie-tier Warden and damaging actions are explicitly authored playable fiction; neither its source No Level category nor official Unknown label was turned into a claimed canonical Rookie rank.
- Grove includes authored bird/wind/earth associations; Tide sometimes follows color or family continuity rather than a recorded water attack. Those are not conversions from Vaccine/Data/Virus attributes.

For the 45 preserved bindings, use **Preserved** when describing their actual mechanical role, as the runtime/service already do. Their authoring metadata can otherwise mislead: Agumon's level-1 Attack/Magic is 24/14 despite a Balanced label, Palmon's is 13/22 with Balanced, and Renamon's is 23/18 with Mystic. Retain their original profiles and labels; do not force them into the new role formula.

## Small editorial proposals

These seven changes would make the existing **damage-only** Magic slot clearer. The confidence is high about the absence of the implied mechanic; the replacement names are still authored, optional wording. No new concealment, shielding, healing, control, poison or summoning effect is proposed.

| Entry | Current → proposed | Reason |
| --- | --- | --- |
| Kuramon | Kura Static Veil → **Kura Static Glare** | Avoid concealment; eye imagery is recorded. |
| Bakemon | Bake Lantern Shroud → **Bake Phantom Burst** | Avoid defensive/concealment wording. |
| Kurisarimon | Kurisari Bit Shroud → **Kurisari Bit Burst** | Make its digital special read as an attack. |
| Deramon | Fernlight Veil → **Fernlight Ray** | Avoid suggesting a protective veil. |
| SuperStarmon | Constellation Shieldray → **Constellation Ray** | Remove the unimplemented shield implication. |
| Babamon | Hearth Blessing → **Hearth Burst** | Avoid promising healing or a buff. |
| Parasimon | Spore Command → **Tendril Discharge** | Avoid control; the official profile supports electric tentacles rather than spores. |

Parasimon's tentacle attack and reliance on a host were checked directly in the [official reference](https://digimon.net/reference_en/detail.php?directory_name=parasimon). Its current solo Mega budget is a playable abstraction, not a faithful model of host-dependent strength. **Do not lower its tier or add a host system based on this review alone.** This proposal improves the label while retaining current mechanics.

Six lower-priority grounding improvements from the [previous content audit](world-ds-content-audit.md) remain optional: Hookmon's anchor → hook claw; Salamon's bell → puppy body tackle; Apemon's bamboo → unspecified staff; Mummymon's sarcophagus → inspected Obelisk equipment; and Dorumon/Dorugamon's quartz → recorded metal motifs. Absence of a prop from a reference is weaker evidence than an explicit anatomy contradiction. Exact alternatives, slots and evidence URLs are in the matrix.

Do not blanket-rewrite every Halo, Mist, Seal or Lantern label. They can describe damaging energy. MarineAngemon, Piedmon, Dragomon and Arukenimon have additional presentation caveats recorded per row; those are weaker than the seven concrete clarity proposals. In particular, [MarineAngemon's reference](https://digimon.net/reference_en/detail.php?directory_name=marinangemon) describes overcoming willingness to fight, while this prototype abstracts its special as damage. That limitation should be explicit; it does not require a new control mechanic.

## Identity decisions before tuning

**Belphemon remains the clearest unresolved wording concern:** Drowsy Claw and Slumbering Colossus choose Sleep imagery while the source-sheet mode remains unresolved. Determine the actual sheet variant first; do not claim a resolved mode or retune stats from a name. BlackImperialdramon, Justimon and Antylamon also retain their existing mode/variant qualifications. Early Dot, era and alias qualifications remain intact. This pass did not inspect artwork or resolve any mapping.

One optional elemental review has stronger direct motif evidence: [Biyomon's official special](https://digimon.net/reference_en/detail.php?directory_name=piyomon) uses spectral flame, while the current authored identity uses Grove and Petal Draft. Ember would be more directly tied to that particular move. Keeping Grove is still an explicit reinterpretation; a type change affects matchups and requires a separately approved, measured compatibility change. No numeric stat or Striker-role change is recommended.

The earlier high-confidence corrections are **already present** in the reviewed catalog: Yatagaramon's Threeclaw Plunge, GuardiAngemon's Sanctified Bladefall and Valkyrimon's Battlemaiden Swordfall. They are marked resolved, not re-proposed.

## Scope and next decision

The matrix's overall ratings are: 166 retain-authored-profile, 13 editorial-proposal, 30 review-design-choice, 45 preserve-existing-profile, and one identity-review-before-label-change. The 30 design reviews include unresolved mapping caveats as well as deliberate role choices; they are not 30 proven defects.

The narrow next step is to accept or decline the seven damage-label changes, clarify preserved-role metadata, and leave numeric profiles unchanged. Any accepted label edit still needs coordinated native/generated-content and frozen-replay handling; presentation names occur in deterministic outputs. Source research, inferred authoring rationale and measured stats remain distinct. This review performed no new fight simulation, source-sheet visual audit, physical-device test or remote mutation.
