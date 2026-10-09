# Practice battles on the ESP serial interface

The ESP application now links the same C++ practice engine used by the service. A small portable session controller owns Tactical/Auto commands and a separate two-record NVS save. It needs no network, service or LCD driver. This closes the source-level practice integration gap; physical execution remains untested.

## Try the shared serial path on this Mac

```sh
npm run demo:practice:serial
# Optional disposable interactive session:
bash scripts/run-practice-serial-demo.sh --interactive
```

The launcher builds with the existing C++17 compiler, creates a private temporary directory, and uses the **same command parser and session controller as firmware**. Its Impmon care state and seed are explicit reproducible fixtures. It models durable blobs with two host files; it does not open a serial device or simulate electrical NVS behavior. The scripted run starts five separate processes, checks exact Tactical/Auto retries and replay, then deletes only its temporary directory. Actual output (historical local evidence omitted).

## Firmware commands

After restoring or hatching a partner, use `practice help` or `practice status`. On an empty practice store:

```text
practice start 1 0 tactical
practice act 2 1 physical
practice act 3 2 retreat
practice start 4 3 auto
practice replay
practice status
```

`practice start <ID> <REV> tactical|auto` is the **explicit confirmation** that starts a duel. Help/status and selecting a wild mode never start one. A start requires a verified, hatched care save at Home and no active practice duel. The selected companion's identity, species, form and level are frozen. One same-level Flicker, Rill or Cinder rival in its initial form is selected from the new start's seed.

Tactical actions are `physical`, `heavy`, `magic`, `brace`, `counter`, `ward`, `retreat`, or `card 1|2`. The core validates the current attack/defend role. Each accepted action commits once before acknowledgement. Auto instead resolves the whole existing bounded policy at start, saves only the finished result, and rejects manual actions. `practice replay` prints the saved Auto exchanges without writing, reseeding or awarding anything. Practice never changes care HP, bond, captures or rewards. [Shared battle rules](BATTLE_MODES.md).

`ID` is a nonzero increasing 32-bit command ID; `REV` is the current **practice** revision printed by status, independent of care sequence and native exchange number. Start at ID 1 / revision 0 on empty storage. After a lost response, resend the **exact command with the same ID, revision, mode/action and value**. Its most recent receipt returns the existing result even after reboot; using that ID with different content fails. Older IDs or stale revisions are rejected and cannot apply again. Only the latest receipt is retained; this is a small serial protocol, not the service's 32-receipt history. Never wrap IDs or revisions; exhaustion requires reviewed recovery.

## Storage and care isolation

The adapter uses NVS namespace `practice`, keys `state_a` and `state_b`. Each new **304-byte**, CRC-protected format-2 record contains its own revision, mode, frozen companion ID, initial/current native 120-byte rules-7 snapshots (saved rules3–6 remain readable with frozen behavior) and the last canonical command. Existing 288-byte records retain their 112-byte rules-2 snapshots and frozen combat: restore/replay/retry does not rewrite them; the next accepted command writes a new envelope. Care uses schema 12 with a separate 576-byte snapshot; old schema11 saves migrate with proportional HP conversion when appropriate. No service practice-store import or network synchronization is implemented.

A candidate is written to the inactive record, committed and read back before live state or success is published. An uncertain write blocks further practice changes until reboot/recovery. Restore chooses the highest verified revision, validates Auto against native replay, and preserves unknown, corrupt or conflicting records without erasing NVS. A corrupt record is not permission to silently continue from an older result.

Active or unrecovered practice pauses partner switching, evolution and new walk events, including the motion checkpoint path. Feed/play/rest and care inspection remain available subject to ordinary care rules and care-store health. The namespaces share the physical NVS partition and its capacity; logical isolation is not a claim that full or failed flash cannot affect both stores.

## Confirmed evolution on serial

`evolve status` lists the current form, XP, level, bond, direct children, requirements and preview stats. `evolve <formId>` stages one eligible choice; `evolve confirm` commits and verifies the care save before publishing it. `evolve cancel` changes nothing. A choice is tied to the current sequence and partner profile; an intervening care action requires a fresh preview. Active or uncertain practice blocks evolution.

This shares the native rules with the browser's two-button progression screens. Named forms without licensed bundled art remain honestly marked unavailable. Current firmware presentation is serial, not an implemented LCD menu.

## Exact remaining hardware dependency

Serial behavior can be exercised now with the host harness and cross-compiled for all three ESP profiles. A round on-device menu still needs the **SPD2010 display/touch integration, LVGL presentation, and verified external-button GPIO/debounce/tap-hold mapping** for the Waveshare 1.46 board. Those adapters are absent or disabled; no pins or working display are inferred from the serial path. Physical NVS interruption, stack high-water marks, SD/radio coexistence and battery behavior still need the board. No device was flashed.

Practice uses a bounded native trace for resolution/replay and a 2 KiB public-state JSON buffer; it never allocates the 16 KiB host trace JSON buffer. Current linked sizes and focused persistence evidence are recorded in [verification](VERIFICATION.md). The original egg graphics exist, but all eight named Rookie species still lack bundled character artwork; see the [exact art inventory](ONBOARDING.md#artwork-and-storage).
