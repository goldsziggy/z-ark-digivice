# Home panels and waiting encounters

The native 412×412 Home screen has four wrapping panels: **Care, Partners, Settings and Nearby**. Swipe across the partner or tap a side chevron to browse; tap the large button below the partner to open that panel. Returning from a menu preserves the selected panel. There is no Explore button. Encounter pacing and battle mode remain in Settings; collection, care and evolution remain in their existing menus.

The active partner is drawn at an integer scale within a 176-pixel area. The carousel uses the existing framebuffer and sprite loader. The preview tool renders the actual C++ controller and prepared local assets, including circular clipping; it does not simulate the physical panel, touch controller or JPEG decoder timing.

## Walking while using the device

After choosing a starter, fresh valid physical steps advance encounter progress in menus and during an existing wild, Nearby or practice battle. A threshold crossing stores **one chosen encounter**, including its form and level. It waits until Home is quiet: no held touch, pending action, battle animation, setup or active Nearby/practice session. Returning Home presents the stored result once, even without taking another step.

An existing battle is never replaced. While the waiting slot is full, the lifetime counter continues, but extra steps do not create additional fights or credit toward a later one. Finishing the current battle and returning Home can therefore reveal one earned encounter. Choosing a different partner, browsing repeatedly or restarting cannot reroll that stored result.

Explicitly setting encounters to Off remains supported. It stops new progress; it does not erase an encounter already earned. Onboarding, invalid/stale sensor data, storage recovery and explicit power/USB suspension are not gameplay menus and do not grant encounters. No encounters are created before the starter is chosen.

Normal pacing remains a randomized first gap of **40–80 accepted steps**, followed by **80–140**. Relaxed and Frequent retain their existing effort multipliers. There is no new per-step miss roll, pity rule, rarity change or time cooldown.

## Save boundary

The waiting encounter is part of the canonical game snapshot, rather than a separate queue or network job. Schema 16 adds 16 bytes to schema 15: a **652-byte** snapshot containing one `{formId, level, rules}` slot and a foreground-action sequence. The latter distinguishes background checkpoints from player actions, preserving saved capture playback and current Auto-battle decisions. Current schema 17 retains that layout. Rules 13 excludes test creatures; frozen rules 12 retains historical `Explore` events and outcomes. See [the production encounter and battle-art correction](BATTLE_ART_RELEASE.md). Existing 636-byte saves migrate with an empty waiting slot while retaining their partner, collection, active encounter, starter offers and capture record.

Firmware continues to copy, apply, save and verify before publishing state. A newly earned or presented encounter is checkpointed immediately. Partial progress is batched; orderly restart/shutdown flushes it. Sudden power loss can lose the small uncheckpointed tail, but cannot turn a verified waiting result into a different creature. Uncertain storage writes stop further mutations until recovery.

Background progress must not cancel a held gesture or confirmation, restart battle playback, play an encounter cue or wake a sleeping menu. Only presentation on Home produces the encounter transition. Screen idle keeps the step sampler running; Home can wake to show an encounter, while a sleeping menu retains its place and waiting result.

## The reported 200 steps

Installed firmware `6e058e1` already permitted encounters on Home, including while its backlight was off. Its counter displayed **lifetime physical steps**, including menu steps that did not then advance encounter progress. A frozen-source host reproduction reached its first encounter at step 56 on Home and at step 70 on sleeping Home; 200 Care-menu steps produced no encounter.

Thus 200 new, continuously eligible Normal steps on that installed build could not fail solely from bad odds. A displayed lifetime total of 200 does not establish that condition. The device's live screen, pacing setting, sensor freshness and storage state were not read for this diagnosis. The new behavior removes the menu-related gap; it does not establish the cause of the user's particular walk.

## Local verification and installation

Build with `bash scripts/build-esp.sh waveshare`. This uses the existing ESP-IDF toolchain and never flashes. The selected case profile applies the confirmed counterclockwise image rotation and inverse touch transform; see [Display orientation](DISPLAY_ORIENTATION.md).

Run `bash firmware/tests/test_walking_runtime.sh` and `bash firmware/tests/test_idle_runtime.sh` for the runtime walking and idle paths. The shared core, UI and SaveStore tests cover deterministic pending results, navigation, migration and restart. Preview generation is described by `python3 scripts/render-native-ui.py --help`.

These source changes are separate from installed firmware. No USB port, device save or device asset was accessed while preparing them. Physical upright-image, touch, walking and two-device radio acceptance still require an explicitly arranged hardware update and test.
