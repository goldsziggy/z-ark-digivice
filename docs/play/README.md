# z-ark browser demo

A standalone browser sample of the Digivice game, rebuilt from the working-tree changes above base `9e6a745` (current rules 20, snapshot schema 28). The C++ core supplies the current 99-level progression, 250-member collection, injury/treatment, toilet care, focus/capture pauses and dungeon/boss entry rules. The browser owns presentation and input only.

Open `index.html` through the GitHub Pages site or a local HTTP server. The browser must support JavaScript and WebAssembly. There is no account, backend API, analytics, device pairing, or network save service.

## What is playable

- Choose one of the eight fixed starter eggs and hatch a partner.
- Feed, play, rest, use the toilet, and treat injuries using ordinary native care actions. Treatment clears injury; rest recovers health afterward. One native care minute passes per actual minute spent visible and idle at Home. Hidden tabs, battles, held touches, pending actions and dialogs do not accrue care time; there is no offline catch-up.
- Use **Find a demo encounter** to supply a bounded set of synthetic steps through native `Explore` events. The UI reports the actual simulated step count. This does not read a pedometer or change native encounter randomness.
- New and reset adventures default to **Auto** on hatch. Auto runs native battle logic and pauses for eligible manual timing capture. Existing saved mode choices are preserved. Switch to Manual before an encounter to use physical, heavy, or magic attacks.
- Battles show each attack and reply in order, with the acting companion, skill, impact, damage and HP changes. **Skip animation** or Escape reveals the committed outcome immediately.
- Aim a graded capture ring. Green uses the eligible native capture odds, orange half, and red one tenth (with the native positive minimum). Every attempt and random result is decided by the core; a green attempt can fail.
- Manage the 250-member collection, choose an active partner, and select up to three XP companions. Useful care builds bond; battle rewards supply XP. Evolve only when the native conditions are met.

The browser has no physical walking sensor, nearby radio play, trading, or music feature. The demo is not a recording and does not connect to the user's hardware.

## Visual provenance

Sprite atlases are lossless RGBA conversions of the current installed game’s indexed RGB565 device pixels. Background JPEGs are the actual in-game scenes. The renderer uses exact form IDs, audited native facing, opaque clip bounds and integer nearest-neighbor scaling, with the native Home, battle and capture actor positions on a 412 × 412 canvas. The browser HUD and surrounding controls are adapted for the web. Original starter eggs remain the existing game helper’s drawing.

Only the clips needed here are included, loaded for visible forms; original source sheets and import packs are not offered as asset downloads. Missing exact forms are explicitly labeled unavailable. [Source credits and rights notices](ART_SOURCES.md) accompany the [per-asset hash manifest](art/manifest.json).

Battle movement uses lunge, recoil and impact effects around these exact idle sprites. It does not claim additional native attack clips. Auto exchanges use the native trace; Manual exchanges use the before/after command state. Native critical and focus outcomes are retained by the shared core. On a Manual retreat, the native response clears the surviving foe's HP; the display shows `?` for that unavailable value instead of guessing. Reward growth and retreat recovery appear only after combat ends.

`shared/capture-ring.js`, `shared/capture-ring-input.js`, `shared/party.js`, and `shared/starter-onboarding.js` are copied from the installed source. The capture timing sampler and input guards are the existing browser implementations. All other JavaScript is presentation, input, or local persistence; it does not recreate game rules.

## Save and reset

The native egg state requires its original mode invariant. The browser applies Auto immediately after successful hatch using the ordinary native Mode action, then saves both changes together. It does not rewrite existing partners’ settings.

Canonical native snapshots are stored under the isolated key `zark.browser-demo.v1.rules19`, in an envelope identifying rules 20 and schema 28. The former rules-15 save key is preserved untouched; this updated preview starts a separate adventure and does not claim to migrate that save. Native decoding checks integrity and state validity. **Reset demo** replaces only this key after confirmation. It does not clear other site storage or access hardware saves.

Where browser Web Locks and local storage are available, read/update/save operations are serialized across tabs. Other tabs load changes from the shared demo key. Without Web Locks, or if storage is unavailable, the game uses an explicitly labeled session-only save. An invalid existing save is preserved until the user chooses Reset demo.

The command outcome is saved once before its visual playback. Input waits while turns play, and Auto capture opens after the last response. Changing views, resetting, hiding the tab or leaving the page ends playback without rerunning combat or rewards. Reload continues the committed save and restores a pending capture opportunity; it does not replay attacks. Reduced motion keeps the readable turn sequence and HP feedback while removing lunges and flashes.

## Controls and accessibility

The round display now receives device-style pointer input. Phone taps and swipes and desktop clicks and drags use the same 412-pixel logical coordinates, excluding the canvas border. Swipe left/right or tap the side zones to browse eggs, Home panels, companions, stats and evolution routes; use the labeled screen buttons to choose and go back. In Manual battle, select left/right, then make a separate upward swipe to commit. Center artwork taps do not select or attack. Auto opens the same guarded timing surface for native focus or capture pauses. Focus taps dispatch their actual ring phase; Skip Focus dispatches the native no-tap value and never chooses a perfect grade.

The adapter follows the installed native recognizer: a 204-pixel circular radius, horizontal/upward gestures of at least 40 pixels over 40–1500 ms with clear axis dominance, button taps of 20–1800 ms with at most 24 pixels of movement, and immediate capture on a fresh down inside the main area (y 80–330). Back uses a 148 × 44 logical-pixel target inside the round display. Ordinary gestures cancel on a second contact, interruption, context change, invalid drag or leaving the screen. A committed capture cannot be undone by later dragging or lifting. Only the display suppresses native page scrolling.

Dungeon and boss lobbies expose native solo entry, with the held key/sigil count, disabled entry when empty, and explicit immediate spending even on a loss or retreat. Shared play is unavailable without two connected handhelds. Screens and text remain a bounded browser adaptation: hardware settings and nearby play show their limits, while the labeled **Simulate walking** button sits outside the device. Home uses five distinct existing scene images, a 220 ms eased slide, real level/XP progress, and a bottom Lifetime Steps label marked On Handheld because the browser has no hardware lifetime counter. Reduced motion changes scenes immediately. The animation never disables touch or issues a game command.

All actions also have optional ordinary HTML buttons under **Optional browser buttons & detailed stats**. Use Tab and Enter/Space to activate them. When the round screen is focused, Left/Right browse, Up commits a Manual move, Enter/Space activates its first enabled labeled action, and Escape uses Back. Left/Right and Home/End navigate the Play/Box/Evolve tabs. During capture, a fresh press in the round screen’s main area, the optional capture button, or the **D** key commits the current ring timing. Repeated keydowns and stale entering clicks are guarded by the shared input module. Blur, resize, or hidden-tab changes close the timing view without spending an attempt.

Game messages are announced through a polite live region. The canvas has a current text description and is backed by semantic controls, meters, and collection details. The reduced-motion preference stops decorative movement; the capture ring remains a timing mechanic while active.

## Runtime

The `runtime/` files are generated by `tools/browser-demo` from the shared native source using the pinned WebAssembly compiler documented there. The loader and module are served from this folder using relative URLs so the demo works under a GitHub project Pages path. See the repository's browser-demo build and verification instructions for source provenance, parity checks, and compiler licensing.

The touch adapter is tested against the native gesture boundaries; `tools/browser-demo/test-device-touch-input.mjs` checks mouse/touch event contracts and `test-device-view.mjs` exercises screen intents against the real shipped WASM. These are software checks, not physical finger-fit or mobile Safari certification.

Rules 20 is an uncommitted update above the recorded base commit. The browser keeps its existing `rules19` storage and lock key, migrates genuine rules-19/schema-27 snapshots through the native decoder, and saves the original envelope under `.before-rules20` before replacing it. An old pending service action retains its original rules marker and must be reconciled.
