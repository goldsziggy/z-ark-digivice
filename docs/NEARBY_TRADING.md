# Nearby trading

Trading exchanges one owned partner from each device over ESP-NOW, without a
phone or service connection. Both devices need the new firmware; `f74ee4c`
supports friendly battles but no trading.

## Review and consent

In Nearby, choose Trade, select a partner, review both offers and their stats,
then confirm the exact pair. Changing an offer invalidates earlier confirmation.
Once either device durably confirms, offers freeze until completion or abort.

Each collection must retain another playable production partner. A pending trade
locks foreground gameplay, including use of that retained partner; walking still
counts. Nearby battles and trades cannot run together.

## What an exchange changes

The received partner keeps its form, species, level, XP, HP, energy, fullness,
mood and bond, with a fresh local member ID and acquisition sequence. Collection
size stays unchanged; the obtained journal adds its form and keeps old entries.
Trading changes neither battle rewards, capture totals nor RNG.

Current save schema 19 is **660 bytes**; host `State` is **632 bytes**. Schema 18
introduced the four-byte `receivedTrades` counter; schema 19 appends the
[Auto capture pause](AUTO_CAPTURE.md). Saves through schema 17 migrate with zero
received trades. Schema 18 trade counts are preserved, and older saves start
with no Auto pause. Rules 13 historical game events retain their meaning.

Native device cloud pairing, outbox/save synchronization and upload of trade
receipts are **not implemented**. The service understands the state fields;
HTTP save-sync does not import arbitrary device snapshots.

## Durable ownership

The canonical **152-byte transcript** binds ordered identities, a 64-bit session,
both nonces, offer revision, source sequences, creature data and pre-trade
`receivedTrades` counts. Preparation requires the local count and creature to
still match. The lower identity coordinates the decision. `DGT1` transaction
packets are **172 bytes**, below the 240-byte radio limit; `DGN1` battle packets
remain unchanged.

Prepared is mirrored and read back before readiness; the commit decision is
durable before Commit is sent. Both care-save slots contain the ownership result
before Applied. Local Applied unlocks play. Its receipt remains until another
trade can safely replace it, after evidence that the peer reached a terminal
state. A radio send callback is insufficient.

The `trade` NVS journal has two **1,512-byte copies**, each containing a 1,492-byte
core record and envelope. Retained evidence can repair a missing mirror or valid
adjacent phase before acknowledgement/unlock. Corrupt or conflicting records
require recovery and are never erased automatically. Both care slots are upgraded
before readiness to prevent normal firmware downgrade from reopening a pre-trade
save. Missing care data with trade evidence must not create a new egg.

After receipt retirement, an advanced local `receivedTrades` count permits a
limited reply to a valid old peer Applied or coordinator Commit packet. An old
Prepared packet or advanced counter alone never proves commitment. Such replies
do not adopt the transaction, change ownership or interrupt another negotiation.

## Interruptions and limits

Before preparation, offers can be changed or cancelled freely. Afterwards,
cancellation requires the coordinator's durable decision. Timeout cannot prove
that no commit occurred: an uncertain trade stays locked until the same peer
reconnects, without silently restoring the offered creature. Permanent peer loss
can require manual recovery.

Recovery assumes honest compatible firmware and retained durable NVS. MAC,
nonce and CRC checks detect corruption and bind messages, without cryptographic
authentication. Hostile firmware, spoofed identities, restored old NVS backups
and loss of both journal copies are outside this guarantee.

## Verification

The shared core and mirrored-storage tests are runnable without a device:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
ctest --test-dir build --output-on-failure -R 'trade|nearby'
```

Host checks cover all 266 released forms, progression preservation, stale offer
counts, chained exchanges, duplicate writes, missing mirrors and interruptions
before or after writes land. Protocol checks cover dropped/duplicate packets and
the restricted terminal-reply path. The shared core passed ASan/UBSan tests.

Trading has **not been physically accepted on two devices**. Radio timing,
touch interaction, power use and actual interrupted-power recovery still need
bench testing. Preparing this source update does not install it or reset either
playtest device.
