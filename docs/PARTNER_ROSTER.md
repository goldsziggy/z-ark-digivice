# Companions and Set Partner

Open **Menu → Companions** to browse the creatures in the current save. Every capture is an individual with a stable member number, even when two creatures share a species or name. The **PARTNER** badge identifies the one currently travelling with you.

The collection holds **eight members including the founder**. A fresh paired save with zero captures shows its founder; the empty view is for an unpaired device. A full roster keeps all eight; it does not replace an existing creature. This interface provides inspection and partner selection. Release, deletion, trading and nicknames are not implemented.

## Routes and controls

| Route | Purpose |
| --- | --- |
| Menu → Companions | Browse member portraits, numbers, names, type, stage and HP; move between roster pages. |
| Companions → member | Inspect that individual's care and bond, then choose **Set Partner** or open **Stats & moves**. |
| Member → Stats & moves → Moves | Inspect that member's current combat profile and three named skills. Browsing does not select it. |
| Member → Set Partner | Save this member as the active companion, then show **Partner set** and a disabled **Current partner** button. |

Touch a choice directly, or use the two controls: **left tap = Next**, **left hold = Back**, **right tap = Confirm**. Keys A/D simulate those buttons. Type colors supplement written labels; identity and partner status remain readable without relying on color.

## What selection changes

**Set Partner** uses the existing saved `select` action with the member's ID. It restores that individual's HP, energy, fullness, mood, bond, evolution stage and derived combat profile as the active state. It does not copy the previous partner's care values, reset the chosen creature or move it into a different collection slot. Subsequent care, exploration and new battles use the selected member.

Selection is available at home with no active practice duel. Finish the wild encounter, or finish/leave practice, before switching. The current partner's selection button is disabled. Pending commands, recovery, an unavailable service or an unconfirmed practice status also block new selection; refresh the practice status before trying again. Inspection can remain available.

The UI waits for the service's confirmed native-core result before showing success. A lost reply retains the same request for retry. An already-committed selection receipt is acknowledged unchanged even if practice started afterward; it is not submitted as a new selection. Reloading restores the saved partner and individual care. The browser prototype requires its local service; physical device controls and display remain unverified.

## Full roster and artwork

The roster shows **three members per page**. The next-page control wraps after the last page; a full eight-member collection occupies three pages. Stable `#` numbers distinguish duplicate species. Original family packs supply the portraits; a missing optional pack shows a type silhouette and **Art not saved**, with access to the existing pack library. This does not change the member's identity, stats or availability. Personal appearance packs remain optional artwork only.

For exact storage, evolution and capacity rules, see [Collection rules](COLLECTION_RULES.md). [Battle stats](BATTLE_STATS.md) lists the ten current species/stage profiles and their skills.

## Actual browser evidence

- Full eight-member roster, page 1 of 3 (historical local evidence omitted)
- Rill #03 before selection (historical local evidence omitted)
- Confirmed Set Partner result (historical local evidence omitted)

These three **480 × 480** screenshots total **584,609 bytes** and show the final compact member layout with the existing original artwork. A separate temporary save reached eight members through valid service actions replayed by the native core. The actual UI then selected Rill #03: revision **56 → 57**, HP **72/104**, with every individual member record unchanged. Reload restored #03 as partner. No browser script errors occurred. Recorded evidence (historical local evidence omitted) includes the setup actions and public member records, without credentials. No existing user save or hardware was changed.
