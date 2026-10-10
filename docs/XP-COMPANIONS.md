# XP companions

Choose up to **three owned Digimon in addition to your active partner** to share wild battle XP. They stay in the same 60-member collection and do not attack, take hits, change battle odds or occupy extra collection slots.

Firmware `171cda7` is installed on both existing devices with exact own-save migration, settings/SD preservation and checkpoint/reboot verification. This guide describes verified host/browser behavior; the new companion controls and full 60-member collection still need physical playtesting. [Sanitized installation scope](../releases/firmware-171cda7/installation-summary.json).

## Choose your companions

In the browser, open **Companions**, open a nonactive member, then choose **Add XP companion**. **Remove XP companion** frees that slot while keeping the Digimon. On the handheld source UI, open the Home **PARTNERS** panel, press **PARTNERS**, browse with the arrows or a swipe, then use **ADD XP COMPANION** or **REMOVE XP COMPANION**. The handheld action saves directly; it has no extra confirmation screen.

The active partner cannot fill an XP companion slot because it already earns its own reward. Three selected companions fill the list; remove one before adding another. Changes require Home and no active practice match; native Nearby activity, unresolved transfers and unavailable save storage also block edits. Use the ordinary saved-request recovery flow if the browser loses an acknowledgement. Retrying the same action does not apply it twice.

The collection displays the active partner first, selected XP companions in selection order, then all remaining members from newest to oldest instance ID. Badges show which members are selected. Browse with touch or the browser's two-button controls: left tap Next, left hold Back, right tap Confirm. Companion selection is separate from **Set Partner** and from releasing a member.

## XP and ownership

A successful wild victory or capture grants the active partner and **each** selected companion `20 + 6 × wild level` XP, and the same result's bond once: **8** for a victory and **12** for a capture. A duplicate merge skips that bonus on the matched member so the reward is not paid twice. A level-5 wild result therefore grants **50 XP to each eligible member**, without splitting the reward or reducing the partner's XP. The level cap is 50. Each member applies its own cap; a capped active partner does not prevent the others from progressing. Level increases use the existing progression and proportional-HP rules, and the level survives Digivolution.

Companions receive XP and bond only from that successful terminal result. Capture misses, breakouts, escapes, retreat, care, practice and Nearby battles grant no companion XP or bond. The active partner can also gain **2 XP** from one useful feed, play, rest, or toilet while that action's cooldown is clear. Auto uses the same saved terminal reward. Replaying an animation, retrying a request or restarting cannot repeat it. A newly captured or received Digimon is not selected automatically.

Promoting an XP companion to active partner removes it from the companion list. Releasing or trading away a selected member removes that instance from the list. Evolution preserves the stable member ID and selection. Removing an XP companion keeps its level, XP, care and ownership.

## Saves and verification

The contract is **schema 23 / rules 16**, service store format 18, with **3,216-byte snapshots**. Native `State` measures 3,188 bytes on the host. The 60-member collection and 64 KiB JSON bound remain. Prior saves migrate with their earned levels and an empty companion list when none was selected, preserving members, seeds and progress; migration grants no retrospective XP. A schema 22 save from firmware `171cda7` is a supported historical version. Older firmware cannot read a schema-23 save, so any downgrade requires compatible firmware or that device's own pre-upgrade backup.

Service migration replays old rules-13 and rules-14 suffixes with their frozen executors, then preserves exact archived histories, receipt hashes and Auto traces. An old pending request keeps its old rule marker and requires explicit reconciliation; changing the marker does not authorize applying it again. [Full API contract](../service/API.md#xp-companions-schema-22-rules-15).

Verification covers native capped/uncapped rewards, assignment limits, stable IDs, trade/release/promotion cleanup and snapshot recovery; HTTP concurrent retries, invalid-batch rollback, practice isolation and chained historical migration; and actual Chrome companion selection, ordering, guards, lost acknowledgements and retained old pending requests. These checks use synthetic profiles and do not establish physical touch or pedometer accuracy.

```sh
npm run build
./scripts/node.sh --test tests/service-party.test.ts tests/party.test.mjs
npm run test:browser:party # requires the existing Playwright/Chrome setup
```

The public export additionally provides its curated `npm run test:publication` command because private artwork/scenery fixtures are omitted. No native account or save-sync service is required for handheld companion XP.
