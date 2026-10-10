# Individual collection and journal

> **Current roster (rules 19, schema 27):** 250 carried Digimon, a 6,860-byte snapshot, and 465 form IDs (451 playable). Existing saves receive three dungeon keys when they migrate. A newly created game starts with none. Rules 13–18 keep the smaller historical rosters described below. See [ROSTER_RULES19.md](ROSTER_RULES19.md) and [co-op expeditions](COOP_EXPEDITIONS.md).

The shared native core is **schema 27, rules 19**. Browser and firmware execute the same transitions. [Gameplay review](GAMEPLAY_REVIEW.md) · [Roster and provenance](WORLD_DS_ROSTER.md) · [HTTP contract](../service/API.md).

## At 250 Digimon

Two hundred and fifty is the carried roster limit; there is no reserve storage box. A successful capture into the last free slot adds the wild Digimon with a new stable ID. Existing members are retained. When all 250 slots are filled, capture actions reject before spending an attempt, drawing RNG, awarding rewards or advancing the save. An exact-form duplicate still merges bonus XP into the oldest copy and does not need a free slot.

| Action at 250/250 | Current behavior |
| --- | --- |
| Tactical capture | Disabled; a direct or stale request is also rejected without changing the save. |
| Auto battle | Continues fighting to victory or retreat without pausing for capture. Make room before starting if you want to capture. |
| Browser Make room | Review and confirm release of a non-partner while the current encounter waits. Back keeps the Digimon. Current encounters retain opponent, HP, RNG, attempts, cards and mode. |
| Native release | Home → PARTNERS → select a non-partner → STATS + EVOLVE → RELEASE DIGIMON → confirm. The native battle screen explains that the roster is full and says FINISH BATTLE TO RELEASE. It currently has no Make room route; finish the encounter first. |
| Partner selection or evolution | Uses an existing slot; roster count stays 250. |
| Starter selection | Single-use at hatching; another starter cannot be added to a completed save. |
| Native Nearby trade | One-for-one exchange, allowed at 250. Both players confirm exact offers; another playable Digimon must remain. An offered active partner is replaced as partner by a retained member. |

Release removes that individual’s care and XP, while its discovered form remains in the journal and lifetime captures remain recorded. It is never automatic. A confirmed release changes 250/250 to 249/250; a later successful capture can fill the free slot. Malformed saves claiming more than 250 members are rejected rather than truncated.

The expanded roster migrates old eight-member saves without adding or replacing any individual. Release policy is unchanged. The new firmware is prepared locally; both physical playtest units remain on `8be26c6`. No hardware access is part of this update.

## Individual Digimon

- Carry at most **250 Digimon**, each with its own HP, energy, fullness, mood, bond, XP, level, form and capture sequence. Inactive members do not receive the active member's care or combat changes. An exact-form duplicate merges bonus XP into the oldest copy.
- Member IDs are stable unsigned integers, starting at 1 and allocated monotonically through `nextMemberId`. An ID is never an array index and is never reused after release.
- `select <memberId>` is accepted at Home. The active member may be selected again. Unknown IDs or selection during an encounter reject transactionally.
- `release <memberId>` removes an inactive companion at Home or while a rules-10 through rules-14 wild encounter waits for input. Encounter release preserves opponent, HP, RNG, turn, capture attempts, cards and the active partner. Older encounters retain the Home-only restriction. The browser binds review/confirmation to the exact identity and save revision; ESP serial requires `release <id> confirm`. The active companion cannot be released. An active or uncertain practice duel blocks selection, evolution and release.
- The founder may be released after another companion is selected. Historical `starterId` stays unchanged. Release is not death, punishment or a reset.
- A capture retains the **actual wild form and level**, XP at that level's threshold, full derived HP, energy 80, fullness 70 and mood 80. Bond starts at the captured form's entry requirement, representing that wild form's existing maturity. The active companion receives the normal capture reward; capture does not switch partners.
- A full collection rejects capture before spending an attempt or drawing RNG. No companion is overwritten. Battles remain available, and an explicit confirmed release can free a slot while preserving a new encounter.

A fixed **512-bit obtained-form journal** records successful hatch, capture and evolution. It survives release. Migration marks currently held forms; it does not invent a complete historical collection from aggregate counters. JSON exposes the bounded `journal:{capacity:512,obtainedFormIds:[...]}` projection. Reserve storage and nicknames are outside this slice. Native Nearby trading is implemented as a confirmed one-for-one exchange; the browser service does not accept native trade receipts. See [trading and recovery](SOUND_TRADE_AUTO_RELEASE.md).

No elapsed-time neglect, decay or death is implemented. Rest restores 25 HP and energy, bounded by the active form's maxima. Recover fully confirms one bounded batch of those same native events, with no XP or new healing rule. Defeat returns the partner home at `ceil(maxHP / 10)` without erasing progress.

## Onboarding, levels and evolution

`newGame()` / CLI `--replay` preserve the original Mote initializer. Only `newDevice()` / `--replay-onboarding` create an empty egg. The service stores the initializer, so an existing zero-event pet stays a pet.

`hatch <starterId>` accepts 1–8 once in the egg state: Impmon, Agumon, Gabumon, Patamon, Tentomon, Palmon, Gomamon or Renamon. It creates member 1 and journal credit without drawing RNG. Before hatching, ordinary care, walking, combat, capture and selection are unavailable. A mixed invalid batch leaves the egg unchanged; an acknowledged retry returns its original result.

Levels 1–20 use cumulative XP `20 × (L−1) × L`, capped at 7,600. Wild victory or capture grants `20 + 6 × wildLevel` XP and respectively 8 or 12 bond to the active member. Care and Practice grant no XP. Bond caps at 200, and care grants bond only when it improves the relevant need.

Evolution is an explicit Home choice along a reviewed outgoing edge. The graph has **172 edges: all 163 previous routes/gates plus nine reviewed ordinary DigiGame alternatives**. These are authored prototype choices backed by explicit source links, not canonical requirements. The graph allows multiple incoming routes and at most two outgoing choices per form. Edges are separate from immutable profile-family IDs and stat anchors: joining another family changes the member's current form/family, not that family's identity or historical stats.

The usual gates are In-Training level 1/bond 10, Rookie level 1/bond 20, Champion level 5/bond 20, Ultimate level 10/bond 50 and Mega level 15/bond 80. Exact requirements belong to the edge; reviewed shortcuts can omit an unavailable intermediate form. Thus baby companions can progress through care without first winning weak-form battles. The original eight-egg onboarding still hatches directly to Rookie. No general fusion, Digimental, item conversion or branch-reset mechanic is implemented.

Confirmation preserves member ID, XP, bond and capture history, and scales HP with `ceil(oldHP × newMaxHP / oldMaxHP)` rather than fully healing. Staying in a form is allowed; actual stat tradeoffs are shown before confirmation. Stable IDs, growth curves and every previous route/gate remain unchanged. Rules 9 adjusts only the base stats of Original Lumen and Pelagia to the established Ultimate tier budget. Catalog 5 retains the 30 reviewed combat-name bindings across 26 forms introduced in catalog 4, with 24 changed labels; other slots keep authored labels. Category placement and effects remain our three shared mechanics.

The **41 remaining Rookie/Champion/Ultimate leaves** are classified explicitly: two distinct Dot variants, 15 forms with no forward route in the selected source, 15 with unsupported roster destinations, three with missing intermediates, three unresolved identities, two fusion/missing-target cases and one deferred mode/family case. An empty source route is not proof that a species is canonically terminal. All remain playable and encounter-obtainable. Three reviewed stage-skip ideas remain deferred and are not among the 172 edges. [Per-form decisions and UUID evidence](research/digigame-gameplay-evolutions.json).

## Historical encounter and battle notes

The following battle and migration notes describe earlier milestones. Current encounter selection uses the independent world seed and excludes the original test-only forms; see [world seeds](CAPTURE_RING_WORLD_SEED.md), [manual Auto capture](AUTO_CAPTURE.md) and [graded capture](GRADED_CAPTURE.md).

The historical first encounter was a gentle Flicker introduction. Later encounters deterministically traverse eligible forms based on the selected partner's combat tier and current legal level. A Rookie encounter-pool floor remains for Fresh/In-Training companions; their new level-1 evolution gates also allow care-based progression. Every source form remains encounter-obtainable. Seed, encounter count and fixed catalog define selection; no GPS or network is required.

New encounters save exact `wildFormId`, `wildLevel` and `wildRules:8`. Visible Brace, Ward and Counter advance only on resolved attacks or failed captures, not on card reads, walking observations or reloads. Physical and Magic cost 2 energy, flooring at zero; Heavy retains power 16 and requires/spends 6 energy. Counter reflects Heavy using the shared resolver and replaces ordinary retaliation. Shelter absorbs received reflection; Spark remains prepared when a Heavy is reflected. Surviving normal hits and failed captures receive alternating Physical/Magic retaliation.

Capture requires wild HP at or below half and allows three attempts. Rules-8 and rules-9 encounters use `RNG % 100 < chance`, where `chance = 50 + floor(40 × (wildMaxHp − 2 × wildHp) / wildMaxHp)`: 50% at half HP, 70% at quarter HP, approaching 90% near zero. Further weakening therefore improves the chance but risks defeating the target. Active encounters through rules7 retain their recorded level-based chance; rules8 retains the HP-based curve. Capture RNG remains deterministic and replayable; a capture is distinct from defeating an opponent.

At Home, `mode 0` selects Tactical and `mode 1` selects Auto. The mode is fixed during an encounter. Auto waits for explicit confirmation, then resolves through one bounded native operation and one outer sequence increment. It tries legal captures first, otherwise uses public profile/guard information to choose damage, excludes reflected Heavy and prefers cheaper basics on ties. It does not peek at capture RNG or accept mid-battle manual input. The limit is 48 turns, ending in a gentle retreat if unresolved. [Mode and trace contract](BATTLE_MODES.md).

## Persistence and history

Native snapshots encode little-endian fields and CRC rather than struct padding; the current canonical care snapshot is 2,952 bytes (schema20 used 664 bytes; historical schema13 used 576 bytes). State JSON has a bounded buffer sized for the full sixty-member projection; [resource verification](ROSTER60.md) records its measured size. Graph pages use at most 16 nodes in a 16,384-byte host buffer. Current measurements and checks belong to the [gameplay review](GAMEPLAY_REVIEW.md); the earlier source-hashed measurements (historical local evidence omitted) describe their recorded revision. Invalid actions and invalid snapshot decoding leave live state unchanged.

The historical migration described below used **container 12** and archived exact older histories before storing the migrated baseline. Rules 1–9 replay through frozen executors, retaining their transitions, form graphs, numeric profiles and old move labels. Older training tiers map to their established RPG levels and XP; existing form, care, RNG, sequence and receipt identities remain preserved. Migrated active rules-4 through rules-9 encounters retain their stored `wildRules` and behavior until they finish. New encounters use rules10 and an explicit 70/25/5 common/uncommon/rare bucket table. Schema12→13 preserves stored gameplay fields and the 576-byte representation; it adds no rarity or recovery field to the snapshot. The schema11→12 converter scales affected Home/inactive member HP with `ceil(oldHP × newMaxHP / oldMaxHP)` while retaining identity, XP and bond. An active old encounter retains its old profiles/HP through combat, reward and retreat calculations, then converts once at terminal Home. Adding a route cannot retroactively make an old evolution command legal.

Committed retries return their original receipt and result even after later care, release, selection or practice. Old pending requests require reconciliation; changing an old body's `rulesVersion` cannot make it a new valid command. Archived batch IDs remain reserved. New stale revisions fail instead of applying to another companion or encounter.

Practice is a separate record: new schema/rules 7 uses store 8, 120-byte snapshots and 40 exchanges. Its raw damage minimum is `max(4, ceil(intended defender maxHP / 20))`, applied before type, guard and reflection; Counter still uses the originally intended defender’s maximum HP for this minimum. Wild combat keeps its raw minimum of 4. Older practice epochs 2–4 retain 30 exchanges, and epoch 5 retains 40 exchanges with a raw minimum of 4. Epoch6 keeps its old profiles and phase-only Auto choices; all older duels keep their frozen behavior. Practice7 Auto evaluates public profiles/HP over all three possible opponent actions and uses seeded ties without reading hidden commitments or Tactical hints. Practice cannot replace care HP, award XP or alter the collection. The current Waveshare 1.46-inch target has a native LCD/touch renderer and physical save/SD verification from earlier installed milestones. New source changes still need their own device acceptance; current prepared builds have not been flashed. External buttons remain optional and unimplemented. Host and cross-build evidence are distinct from hardware tests.

A 40-exchange duel gives each side 20 attack slots. Previously, a level-15 Cannondramon mirror had 248 HP but could deal at most 245 damage in those slots even with Spark, making defeat impossible within the limit. Rules 6 raises its raw minimum to 13, making completion possible; guards, reflection and actual choices can still produce a draw. This changes practice damage resolution, not stored numeric curves, IDs or the 172-route graph. [Measured gameplay review](GAMEPLAY_REVIEW.md).
