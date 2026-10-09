# Production encounters and battle artwork

The reported Flicker encounter and sprite flashes were reproducible from both installed `6e058e1` and the uninstalled `517f2b6` source. This follow-up retains the Home carousel, walking across screens, one waiting encounter and confirmed CCW90 display/touch correction. No device was opened or flashed while preparing it.

## Confirmed causes and corrections

The previous encounter selector forced form 4, Flicker, as the first opponent. Later encounter and practice pools also included the ten original test forms. Production selectors now use only the **266 Digimon forms, IDs 11–276**, including the first encounter. Existing level/stage eligibility and 70/25/5 rarity weights remain; there is no longer a guaranteed test opponent.

The native runtime loaded the partner's Attack clip during its turn, while the renderer required Idle or Hurt. The strict animation check rejected that valid artwork and drew a procedural placeholder. Between turns, ordinary Battle did not load the partner artwork at all. The loader and renderer now use the same request for each actor in both waiting and animated battle states. Wrong-form frames remain rejected.

Facing was not normalized by battle side. The verified local pack contains 235 left-facing forms, two right-facing forms and four frontal forms. Impmon's source frames face left. A stable, per-form source-facing map now applies one horizontal transform for the actor's side: the left actor faces right and the right actor faces left. Frontal art stays frontal; unreviewed artwork is not assigned a guessed direction. The display's case rotation is separate from this sprite-facing transform.

The supplied private packs reuse their idle frames for Attack and Hurt; they do not contain separate attack poses. The code keeps that same creature visible while battle effects communicate attacks and hits. Missing or corrupt exact-form artwork shows a neutral **ART MISSING** tile, never a different or test creature. Production loading rejects test-form DVAs before opening them. Existing SD files and user caches are not erased.

Every horizontal carousel also accepts its small left/right edge arrows: Home, starter choices, partners, stats, evolution choices and information, Nearby discovery, and wild/Nearby attack or defence choices. Right tap advances like a left swipe; left tap goes back like a right swipe. Battle side icons also browse. Center controls, upward attack commits and capture flicks keep their own actions. A moved contact cannot return to an arrow and become a tap, and a completed contact cannot dispatch twice. The same CCW90 inverse touch transform applies to these targets. These controls do not alter saves or rules.

## Saves and historical compatibility

Current care uses **schema 17 / rules 13 / 652-byte snapshots**. The byte layout remains that of schema 16. Frozen rules 12 replay retains historical events and outcomes, and existing 636-byte schema-15 device saves remain supported. Combat and care formulas are unchanged; rules 13 establishes the production roster boundary.

An explicit `resolve-test-encounter` event retires a saved active or waiting test encounter. It grants no reward and preserves the current partner, collection, XP, care, counters, pacing and RNG. It does not replace the foe with a newly rolled encounter. For installed rules-12 saves, numeric HP is unchanged. Much older pre-rules-9 fights retain the established HP-fraction conversion needed when returning to current Home stats.

Native startup applies this event only when needed, saves and verifies it, then publishes the result. A normal reboot does not repeat it. If storage is uncertain, the original state remains retained, gameplay is blocked and the display shows save recovery. Read-only diagnostics and existing power/reboot recovery remain available. Historical test capture records are preserved but are not replayed as a new reveal.

Already owned legacy members are not deleted. Their IDs remain available to old-save decoding and frozen replay; they cannot be newly selected through production encounter, starter, practice or evolution pools. Production catalogs and the evolution atlas omit them.

The earlier atlas draft counted **276 forms and 172 routes**, including ten original test forms and their six internal routes. The corrected production chart contains **266 Digimon and 166 routes**, arranged into **42 connected families and 61 isolated forms**. Stable IDs do not change.

## Asset delivery boundary

Normal full-content local-service catalogs expose scenery, not the three toy sprite packs or ten toy DVAs. This reduced publication omits that scenery, so its normal catalogs are empty. Those fixtures require explicit development/test opt-in. Browser caches retain old files but exclude toy packs from normal selection; missing art does not select one as a replacement. The actual Digimon art remains the existing owner-supplied local/SD collection. No replacement art, production signing credentials or paid service was created.

## Verification and remaining acceptance

Host checks cover production pool exclusion and reachability, frozen replay, saved encounter cleanup and write failures, two-slot recovery, exact-form loading, animation-request agreement, fixed facing on both sides, repeated turns and missing art. Native previews use actual decoded SD pixels and the committed-result battle sequencer, with framebuffer bounds checks. They are software renderings, not footage from the device.

The exact combined ESP build, source hashes, test logs and preview frames are retained in the task's deliverables. Physical battle playback, frame timing, upright display and finger alignment remain untested for this revision. The later f74 installation checkpoint is documented with the [verified firmware images](../releases/firmware-f74ee4c/README.md). Both units passed bounded installation/save/SD/reboot checks, while physical gameplay acceptance remains open.
