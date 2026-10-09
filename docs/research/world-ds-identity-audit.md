# World DS identity audit

**255 means individually named source sheets, not 255 verified unique canonical species.** All 255 inventory keys and display names match the assembled catalog and stable ID map. They have 255 distinct gameplay form IDs within the 276-form runtime. This audit does not merge IDs, change profiles, or download artwork.

The [JSON audit](world-ds-identity-audit.json) records every entry, source hashes, qualified mappings, and reference links. It compares the [inventory](world-ds-inventory.json), [early official metadata](world-ds-official-early.json), [late official metadata](world-ds-official-late.json), and [local visual reviews](../../data/world-ds-source-reviews.json). The original audit used baseline `a352335`; revision 2 records the subsequently applied Antylamon metadata correction and refreshes the input hashes.

| What is counted | Result | Limit |
| --- | ---: | --- |
| Named source entries / distinct display names | 255 / 255 | Source identities, not canonical species |
| Resolved sheet IDs | 133 | No duplicate IDs among these; 122 IDs remain unresolved |
| Rows with a primary reference after the MetalGreymon review | 250 | Some references are explicitly candidates or base-species references; Antylamon now leaves its primary identity unselected |
| Distinct primary reference URLs in those rows | 247 | Reference-page count, not a species count |
| Shared primary-reference groups | 3 | Detailed below; no automatic artwork or ID merge |
| Explicitly qualified identity records | 19 | Includes the Antylamon ambiguity now flagged upstream; not a claim that every other sheet was visually verified |
| Locally reviewed named sheets | 3 | Agumon, Greymon, MetalGreymon; the other 252 have no local visual review |
| Deduplicated canonical species | **Undetermined** | Do not publish 255, 254, or 252 as a verified species total |

The grouped **NPC Digimon** sheet is outside the 255. Its contents were not inspected and supply no additional species count.

## Shared references are not interchangeable sprites

| Source entries | Established reference relationship | Qualification |
| --- | --- | --- |
| Daemon, Creepymon | Both metadata records use the [Creepymon / DEMON reference](https://digimon.net/reference_en/detail.php?directory_name=demon) | A shared canonical reference does not prove the two source sheets show identical appearances. Keep both source entries and authored profiles. |
| Agumon, DotAgumon | Both use [Agumon](https://digimon.net/reference_en/detail.php?directory_name=agumon) metadata | DotAgumon has only a base-species reference; its distinct sheet is not a separately verified official Dot species. |
| Falcomon, DotFalcomon | Both currently use [Falcomon](https://digimon.net/reference_en/detail.php?directory_name=falcomon) metadata | The [2006 anime version](https://digimon.net/reference_en/detail.php?directory_name=falcomon-2006) is a separate official reference. The source-era mapping, including DotFalcomon, remains unreviewed. |

Other renamed headings generally describe **one** source entry, not duplicate source rows: Goburimon → Goblimon, Armagemon → Armageddemon, Crossmon → Eaglemon, Tyilinmon → Chirinmon, GuardiAngemon → SlashAngemon. The [Goblimon page](https://digimon.net/reference_en/detail.php?directory_name=goburimon) directly supplies the GOBURIMON banner and English heading. The JSON preserves all observed name differences and their individual evidence; it does not infer that every localization is independently confirmed by a sprite comparison.

## Qualified variants and outstanding identity work

There are **19 explicitly qualified records** after applying the existing local review:

| Reason | Entries |
| --- | --- |
| Source/official name review (9) | BlackAgumon; PawnChessmon Black; PawnChessmon White; Penguinmon; ShadowToyAgumon; Grizzmon; Gwappamon; Reptiledramon; Sorcerymon |
| Dot base-reference only (2) | DotAgumon; DotFalcomon |
| Era variant not visually checked (2) | Falcomon; Kudamon |
| Historical game-name candidate (1) | Kumamon → Bearmon; do not confuse it with the separately named official Hybrid Kumamon |
| Unselected mode/version (4) | Yatagaramon: Crowmon versus 2006 version; Belphemon: Rage versus Sleep; BlackImperialdramon: Dragon versus Fighter; Justimon: Accel/Blitz/Critical Arm |
| Ambiguity found here, now flagged upstream (1) | Antylamon: unqualified Virus reference versus Deva/Data reference |

**Antylamon's missing flag has been corrected; its appearance remains unresolved.** The original audit found that the prior file noted an unreviewed alternative but left `sheetVariantReviewRequired` false. The official [Antylamon](https://digimon.net/reference_en/detail.php?directory_name=andiramon_2) page is Ultimate/Virus; [Antylamon (Deva)](https://digimon.net/reference_en/detail.php?directory_name=andiramon) is Ultimate/Data. The official metadata and assembled catalog now flag both candidates, retain their observed facts, and leave the source's canonical name, URL, attribute, and signature unselected. No visual comparison has settled the choice. The outstanding count stays 19: this was already included as an additional audit finding and is now represented in the upstream count.

**MetalGreymon is already resolved locally.** The older metadata-only file retains Vaccine/Virus candidates. The later visual-review record identifies the orange source sheet as [MetalGreymon (Vaccine)](https://digimon.net/reference_en/detail.php?directory_name=metalgreymon-v), and the assembled catalog already applies this documented overlay. It is not one of the current 19 outstanding records.

Explicit color/mode entries remain separate. The JSON lists 16 explanatory families covering Agumon, Falcomon, ToyAgumon, PawnChessmon, Tyrannomon, Rapidmon, WarGrowlmon, WereGarurumon, MegaKabuterimon, WarGreymon, MegaGargomon, Cherubimon, Chronomon, Ghoulmon, Imperialdramon, and Gallantmon counterparts. These group related labels for review; **they are not canonical equivalence classes or evolution routes**. Black/White, Red/Blue, Dragon/Fighter/Paladin, and Destroy/Holy references must not be collapsed by stripping qualifiers.

Two other distinctions remain explicit:

- **Cherubimon Virus** is the source name. Its verified official **Cherubimon (Black)** reference currently lists **Vaccine**; preserve that source/reference disagreement rather than deriving the attribute from the source suffix.
- Source **Fresh / In-Training** correspond to official **In-Training Ⅰ / Ⅱ** labels. Calumon's source **No Level** and official **Unknown** remain separate from its authored Rookie combat tier. Armor's authored Champion-equivalent balance does not change its official stage.

The remaining work is targeted source-form review, especially the 19 qualified records and unresolved sheet IDs. Official reference pages establish their own names and classifications; they do not establish every historical game alias or source appearance. This audit accessed no new source artwork and made no browser, blocked-site, or gameplay changes. Any separately acquired sheets need a recorded visual review before these review counts change.
