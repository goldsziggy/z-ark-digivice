# Focused MVP backlog

Current release blocker: distinguish Unit 2's lost USB visibility from an application startup stall, preserving its earlier verified save. The installer now uses Espressif's console-opening order and passes 30 host tests; physical recovery is unverified. Keep Unit 1 idle and observe Unit 2's screen before further USB access. [Diagnosis and bounded next step](USB_BOOT_RELIABILITY.md). Local Nearby is implemented independently of service account pairing/sync, but actual two-board RF checks await chosen partners. Current preflight (historical local evidence omitted).

Current target: **Waveshare ESP32-S3-Touch-LCD-1.46 standard glass (SKU 29565), 412 × 412, 16 MiB flash / 8 MiB PSRAM**. The owned Heltec V4 remains an unverified interim profile. Both physical Digivices retain their own identity, partner save and offline operation. Nearby friendly duels are implemented with explicit consent and no progression rewards.

Current care is **schema 15/rules 12/636-byte snapshots/service container 14**, with frozen rules 1–11 and separate practice 7/store 8. Firmware `6e058e1` passed its build and was installed app-only on both units. Native rendering, saved eleven-choice onboarding, care bonuses, capture playback and screen/backlight idle are implemented. Device checkpoint evidence (historical local evidence omitted) records completed save/reboot, preview, asset and screen-idle checks. Remaining release work is human interaction and resource/power qualification; native pairing/outbox/save sync and CPU deep sleep remain future work. [Current release/backlog](CARE_CAPTURE_RELEASE.md) · [Care/capture contract](CARE_CAPTURE_RULES12.md) · [Starter offers](STARTER_OFFERS.md).

## Historical foundation backlog

The table and hardware notes below preserve the original staged plan. “Port display”, “second device” and older schema/build statuses describe that earlier milestone, not outstanding current implementation. Use the current release checklist linked above for remaining work.

| Priority | Work | Acceptance evidence |
|---|---|---|
| Complete | Park-session rarity, confirmed full recovery, make room in new encounters, truthful connection recovery and durable motion delivery | [Milestones and timing limits](PARK_PLAYTEST.md); current native/browser/service and three-profile build evidence |
| Complete | Shared local game core, round browser menus, practice battles, care/capture/collection and original asset library | Host and browser evidence in the verification guide; no cloud dependency in firmware gameplay |
| Complete | Official task-local ESP-IDF 5.3.6 and primary microSD assets | Three successful target builds, bounded file/cache recovery tests, measured ELF/map/binary sizes in [ESP_BUILD.md](ESP_BUILD.md) |
| Complete | New-user egg selection and direct Rookie hatch | Eight starters including Impmon; one durable hatch; retries/reloads preserve it; existing identities skip unchanged |
| Complete | ESP practice serial adapter and separate durable records | Shared parser host proof, exact last-command retries and Auto replay; physical menu/NVS checks remain open. [Guide](ESP_PRACTICE.md) |
| Complete | Tactical and strict Auto for wild encounters and practice | Explicit mode/start confirmation, bounded native results and exact retries; earlier Tactical compatibility proof retained through frozen legacy rules. [Mode evidence](BATTLE_MODES.md#firmware-serial-flow-and-verification) |
| Complete baseline | Catalog, collection and RPG | 276 playable forms represent 255 named source entries plus retained forms; eight Rookie starters, distinct XP/bond, eight carried members, permanent journal and explicit release. Shared curve/skill limits are documented in [WORLD_DS_ROSTER.md](WORLD_DS_ROSTER.md). |
| Implemented; final verification recorded separately | Expanded explicit evolution graph | 147 routes preserve all 99 previous choices/gates and add 48; baby care gates, joins between stable profile families, and named terminal/independent exceptions. Care schema 9/rules 6 preserves exact frozen rules-5 replay. [Current verification](VERIFICATION.md), not prior build counts, determines release status. |
| 1 | Confirm physical board revision, battery and button wiring | Match actual PCB/connectors to official schematic; establish safe charging and power hold |
| 2 | Bring up SD and NVS on the board | Missing/full/corrupt/removed card remains recoverable; no formatting; power interruption preserves acknowledged saves |
| 3 | Port the pinned vendor SPD2010/LVGL display and touch adapter | Native 412² round display; bounded strip rendering; measured input latency, FPS, heap and stack use |
| 4 | Add durable event outbox and first physical-device save sync | Reboot through commit/send/ack; no duplicate hatch/capture or discarded acknowledged play |
| 5 | Qualify QMI8658 steps and sleep behavior | Walking/shaking/reset traces, step batching, awake/idle/sleep current measured |
| 6 | Integrate NFC and two buttons | Verified wiring, UID debounce, one card effect per encounter; directions optional |
| 7 | Phone browser setup | Device confirmation, credential handling and token rotation; no required native phone app |
| 8 | Offline end-to-end handheld playtest | Boot/choose egg/hatch/care/walk/card, earn battle XP, preview/confirm a branch, Tactical/Auto wild and practice, capture/save/reboot, then sync recovery |
| Later | Repeat on second device | Independent identities and saves; multiplayer remains separate work |

## Open hardware choices

- The historical 2.1-inch/P4/T-Watch wiring and 480² frame budgets do not apply to the current 1.46-inch board. See [verified pin sources](PARK_HARDWARE.md).
- Physical board revision, battery dimensions/polarity, charge-current compatibility and external button pins are unconfirmed. No battery or charger compatibility follows from compiling firmware.
- NFC module/cable orders are intentional for two units. Confirm exact PN532 revision, logic voltage, antenna placement, shared-bus behavior and clearance at the physical slot.
- GPS and nearby-player concepts remain under discussion. Step encounters work locally; no external GNSS purchase or native phone app is required by this build.
- SD uses existing FAT storage and the verified vendor pin map. Never format a card automatically or infer unused pins from the ESP32 chip pinout.

## Deferred work

XP belongs to wild victories/captures; care and practice are not XP sources. Balance still needs playtest feedback beyond deterministic simulation. The 210 added profiles deliberately share 28 reviewed numeric curves; 765 distinct source-entry skill labels use three shared attack mechanics. Species-specific effects and stat offsets are future design work, not completed content.

Catalog access does not imply complete evolution coverage: 95 forms are independent, and 147 have no outgoing route. Further routes need reviewed identity/relationship evidence and explicit game rules. Fusion, Digimentals, special mode conversions and branch resets remain deferred. All 276 forms remain encounter/capture eligible under stage/level constraints.

User-authorized source-sheet acquisition is underway in normal Chrome. Downloaded sheets still need identity checks, exact frame mappings, provenance and bounded browser/device conversion; only the three audited local packs are currently integrated. Missing art remains labeled. This does not authorize publication or imply franchise rights.

Cloud deployment, native phone apps, ranked competition, purchases, live AI battles, multiplayer, persistent OAuth, production OTA signing and complex content tools remain outside this slice. Future nearby duels require both players to agree the same immutable mode, profiles, rules and seed before starting; current battle modes do not implement discovery or peer transport. Private franchise artwork stays outside the source bundle. The earlier Garage upload plan is also separate; no remote storage write is authorized by the firmware work.
