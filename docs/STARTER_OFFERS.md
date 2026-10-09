# Saved Rookie starter offers

New-device onboarding retains the eight fixed eggs and adds three distinct Rookie choices saved for that device. Each extra choice has a level-1 profile, three named combat skills, at least one authored forward evolution and an exact-form asset in the reviewed 251-entry pack. Selecting any one of the eleven choices creates exactly one owned partner.

The runtime creates an explicit `starter-offer-seed` event with one nonzero entropy value and checkpoints the resulting Egg state before displaying the offers. The seed is not an identity credential. Replay tools/tests can supply a deterministic seed. Drawing uses a separate xorshift stream and selection without replacement; it does not consume capture RNG or walking RNG. The saved seed and three form IDs are immutable. Restart, back, cancel and browsing do not draw again.

`starterForm(state, slot)` maps slots 1–8 to the original fixed forms and slots 9–11 to that saved state's three offers. `hatch` accepts only these slots; it never accepts an arbitrary form ID. Slot 9–11 is invalid before initialization. Existing hatched saves stay unchanged. Existing Egg saves can receive the one-time event while preserving their identity and any runtime choice that still exists. Migration itself does not draw randomness or hatch anything.

The bounded candidate IDs are:

```
78 79 80 81 82 84 85 88 89 90 91 92 93 94 95 96
97 98 99 100 101 103 105 106 109 110 112 113 114 116 117 118
```

In that order: Armadillomon, Aruraumon, Betamon, Biyomon, BlackAgumon, DemiDevimon, Dorumon, Dracmon, Falcomon, Floramon, Gaomon, Gizamon, Goburimon, Gotsumon, Guilmon, Hagurumon, Hawkmon, Kamemon, Keramon, Kotemon, Kudamon, Lalamon, Muchomon, Otamamon, Penguinmon, Salamon, SnowAgumon, Tapirmon, Terriermon, Tsukaimon, Veemon and Wormmon.

The original fixed forms `{11,18,25,32,39,46,53,60}` are excluded. Nine other Rookie forms without an authoritative outgoing edge are excluded: `{83,86,87,102,104,107,108,111,115}`. Named variants with distinct IDs/art and valid routes are retained. “Forward route” does not promise a complete Mega chain: Armadillomon→Ankylomon, Gizamon→Raremon and Kamemon→Gwappamon currently end at authored leaves.

The core pool is a reviewed constant, not an SD scan or dependency on private image contents. The metadata-only audit checked exact-form entries against pack-manifest SHA256 `b0a9f9251835a5cf4324937dbfb370d9c677987997b6bd8d0b55d065bdf1e14f`. All 32 have distinct source keys and DVA hashes. The device renderer must honestly use its fallback if a required pack is absent; these IDs do not authorize distributing artwork.

Schema 15 stores the offer seed and three IDs in 16 bytes at offsets 616–631. Decode recomputes the expected selection, rejects mismatches and rejects duplicate or out-of-pool offers. JSON adds `onboarding.offerSeed` and `onboarding.offers`; uninitialized offers are `[0,0,0]`. The existing starter catalog remains the eight fixed entries; form IDs supply the extra names, art identity and level-1 profiles.

`tests/care_capture_core_test.cpp` exercises 128 deterministic offer seeds covering every candidate, all eleven hatch slots, future evolution, restart persistence, invalid/repeated initialization, fixed-starter exclusion and migration from both old Egg and hatched states. Device checkpoint-before-publish and uncertain-write recovery are tested separately by the runtime owner.
