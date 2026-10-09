# Round-screen navigation

The browser puts the main interface inside a round display. The default touch preview has a **412 × 412 CSS-pixel content area** on a wide viewport; the original 480 × 480 artwork canvas scales within it. Open **http://127.0.0.1:8787** after `npm run dev`. On first use, tap **Set up device → Create pairing code → Claim device**, choose an egg and confirm the hatch. The service and shared C++ core still own game results; the menu is a browser presentation layer, not compiled ESP display firmware.

In **Touch screen** mode, tap the displayed choice, browse with on-screen **Prev / Next**, and return with **Back**. Swipe long details; browsing to another choice restores the panel to its top. Select **Two buttons** above the shell to use **left tap Next**, **left hold Back**, and **right tap Confirm**. The matching keyboard keys are **A / D**; hold A for Back. Arrow keys and Enter also select and confirm. These simulate device controls without implying physical GPIO assignments. Returning to a menu preserves its highlighted choice. [Touch checks and device limitations](TOUCH_READINESS.md).

| Screen | What it does |
| --- | --- |
| Home | Shows the active companion, level, steps and bond; opens the main menu. |
| Menu | Care, Companions, Explore, Cards, Settings, Battle, Stats, Progression, Type chart, World DS roster and Discovery journal. |
| Care | Feed, Play, Rest once, or **Recover fully → Confirm recovery**. Recovery batches the native count of ordinary rests, with no new healing rule or XP award. Back before confirmation changes nothing. |
| Companions | Shows individual member numbers, portraits and the **PARTNER** badge. Open a member for details, combat stats and **Set Partner** at home after any practice duel ends. The eight-member collection uses three-member pages. |
| Explore | Shows native remaining steps or queued encounters. **Explore +100** explicitly adds 100 simulated steps. Queued encounters do not start automatically; a later real step or simulated walk triggers one. Choose Tactical or Auto before walking. |
| Wild battle | Native rarity, health and guard are visible. Use physical, heavy or magic, one card and the native capture percentage. A full team in a new rules10 encounter offers **Make room**; release only a reviewed non-partner, then return to the unchanged encounter. Old encounters keep their earlier release restriction. |
| Capture | Touch mode arms an original orb: drag and flick upward toward the ring. **Cancel** exits before release without spending an attempt. Short taps and cancelled gestures send nothing; a valid miss spends one attempt, and a hit uses the native capture chance. Two-button mode retains its cancelable preparation. After submission the save must resolve; navigation cannot undo it. |
| Cards | Spark boosts the next hit; Shelter absorbs 12 incoming damage in total, retaining unused protection across hits. Only one card may be used per wild encounter. Cards are simulated NFC inputs. |
| Settings | Opens Sound, Artwork, Connection and Saved playtests. |
| Battle mode | Choose Tactical attack/defence or explicitly confirmed Auto. Practice is separate, gives no capture/progression rewards, and retains its own saved session. Auto playback has no combat controls. |
| Progression | Native XP, level and legal branch requirements; review next-form stats and moves before confirming. The paged evolution graph is reference information, not automatic progression. |
| Roster and journal | Browse eight records per page or filter by initial letter/stage. Exact-form art loads on demand. Authored rarity is distinct from capture chance. The journal keeps discovered forms after release. |
| Stats and Moves | Inspect max HP, Attack, Defence, Magic and Resistance, then open the companion's three named moves. Member details and practice offer links to their own profiles. |
| Type chart | Shows Grove → Tide → Ember → Grove advantages; Neutral has no advantage or weakness. The current chart comes from the shared core catalog. |

**Sound** provides explicit enable/mute, optional original music and volume controls. Audio starts after a user gesture and pauses while the page is hidden. The demo video is silent; it does not capture browser sound. The selected 1.46 board has a fitted speaker, DAC and amplifier, but no audio driver is compiled in our firmware. [Sound evidence and hardware boundary](MOTION_AND_SOUND.md).

**Artwork** switches an already imported personal appearance or opens the browser's pack/import tools. **Connection** shows the local service and pairing state; it is not physical Wi-Fi provisioning. A failed service request shows an unavailable/attention status. A paired browser with no loaded save shows **Save not loaded → Restore saved game**, retaining the identity. **Saved playtests** loads retained games or confirms a fresh game while keeping the current one. These screens do not erase a save simply because an asset or connection fails.

The collapsed **Playtest tools** section holds the full asset library, file importer, detailed stats, journal and diagnostic controls. It is useful for inspecting the prototype without crowding the round device. [Local playtest instructions](PLAYTEST_GUIDE.md) describe those controls and recovery checks.

[Companions and Set Partner](PARTNER_ROSTER.md) explains individual IDs, full-roster behavior and confirmed selection. Browsing a creature's stats does not switch partners; selecting it restores that member's own saved care values.

## First-run eggs

A newly paired identity enters the eight-egg carousel. Browse with Prev / Next, tap an egg to review and confirm to hatch the named Rookie. Back returns to selection before sending. Two-button mode maps these same actions to left tap, right confirmation and left hold. Pending hatches use the existing saving/retry screen, and completed identities—including unused older Mote saves—go to Home. [Starter flow](ONBOARDING.md).

## Practice Battle

Open **Menu → Battle → Start practice → Tactical**. Read the rival's partial two-option hint, then choose your companion's physical, heavy or magic skill. Mote offers **Twig Tap**, **Root Ram** and **Seed Spark**; later evolutions have their own names and stats. The next screen reveals both committed choices and the actual damage. Choose **Next exchange** to defend with **Brace**, **Reversal** or **Rune Ward**. A hint narrows possibilities; it does not reveal the answer or guarantee a counter. One field card is available separately from the wild-encounter card allowance. [Stats, types and damage examples](BATTLE_STATS.md) explain the shared rules.

Practice has its own health and saved session. It starts with full practice health and leaves pet health, care, bond, steps, captures and collection unchanged. It is not a wild capture encounter or multiplayer battle. Back from a choice screen returns to the practice lobby, where **Continue battle** resumes and **Leave practice** records a retreat through the core. A sent command still needs confirmation or retry; Back cannot undo its result.

## Saving and limits

An action awaiting confirmation opens a dedicated saving screen. Retry uses the same saved command; Back cannot silently discard it or submit a second action. A conflict opens the explicit recovery flow. Ordinary menu navigation changes no care, RNG, capture or progression state.

The menu does not make phone hosting or physical hardware ready. The current endpoint is Mac-only loopback. Sensors, NFC, GPS, touch hardware and physical display performance remain unverified. Optional personal appearances retain their source restrictions and replace artwork only; the underlying companion records and rules remain unchanged.

## Navigation evidence

Touch navigation milestone evidence (exact source hashes are recorded): navigation, eight eggs, scrolling, narrow layouts and asset pause/resume (historical local evidence omitted), combat, capture, roster and evolution (historical local evidence omitted). These use actual touchscreen events and isolated saves, with no keyboard or simulated physical-button presses. Auto replay also offers pause/resume and a jump to its saved result without submitting any game command.

Flick-capture evidence (historical local evidence omitted) adds actual drag/release gestures, native aim misses and capture, cancellation and exact retry. Audio evidence (historical local evidence omitted) covers real PCM rendering and explicit activation.

Current milestone checks: encounters, Make room, rarity and saved step credit (historical local evidence omitted), connection and atomic recovery (historical local evidence omitted). These use actual two-button input and isolated temporary saves. Browser walking is simulated; nominal CSS size checks do not establish physical LCD readability.

The following screenshots and clips are historical UI evidence; their older combat values are not current balancing fixtures.

Current companion roster: full collection, first page (historical local evidence omitted), member detail (historical local evidence omitted), and confirmed Set Partner (historical local evidence omitted). The isolated native-backed playtest selected Rill #03 from eight members, preserved every member's care values and restored that partner after reload. [Roster guide and recorded evidence](PARTNER_ROSTER.md#actual-browser-evidence).

Current stats update, captured from the running browser with original artwork:

- Mote's combat stats (historical local evidence omitted)
- Mote's named moves (historical local evidence omitted)
- A resolved practice hit (historical local evidence omitted)

The three 480 × 480 screenshots total **484,737 bytes**. The actual turn was Twig Tap against Flicker's Reversal: **14 rival damage**, changing Flicker from 88 to 74 HP while Mote stayed at 100. Reversal only reflects heavy attacks. The isolated playtest then retreated; the ordinary companion save was checked unchanged and no browser script errors occurred. All three original packs were loaded through the local UI. Recorded responses and screenshot details (historical local evidence omitted) contain only public battle state, with no device credentials or hidden enemy intent.

### Earlier navigation recording

The original navigation screenshots below show the earlier six-choice menu, before the stat screens and two-button gestures were added:

- Home (historical local evidence omitted)
- Main menu (historical local evidence omitted)
- Care (historical local evidence omitted)
- Settings (historical local evidence omitted)
- Practice lobby (historical local evidence omitted)

The **historical pre-stats** private recording is under ignored `.personal-assets/navigation/navigation-agumon.mp4`, with local evidence JSON beside it. It contains actual browser paint frames from a separate temporary save: Home, Menu, Care, Companions, Explore, Cards, a wild battle/capture, Practice Battle and Settings/Sound/Artwork. It is a silent **26.20-second, 546,305-byte H.264** recording normalized to **480 × 480 at 15 video frames/second**. That frame rate is an export setting, not a hardware benchmark. Its older move names and flat HP/damage values do not describe the current combat rules.

The recorded wild result is 100 steps, one capture and two members. Practice then starts at 100/100 health, resolves Power Crush against Rune Ward for 20 rival damage, resolves Reversal against Spark Bolt for 16 player damage, and records a deliberate retreat. The ordinary pet save was checked unchanged after those practice commands. There were zero browser script errors. Original screenshots are reusable project evidence; private appearance frames and video remain ignored. No user save, CAD file or hardware was changed.
