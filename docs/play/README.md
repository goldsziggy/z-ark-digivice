# z-ark browser demo

A standalone browser sample of the Digivice game, using the same C++ game rules as installed firmware `171cda7` (rules 15, snapshot schema 22). The public source release is `4466aa1`; its game core and browser presentation helpers match that installed source.

Open `index.html` through the GitHub Pages site or a local HTTP server. The browser must support JavaScript and WebAssembly. There is no account, backend API, analytics, device pairing, or network save service.

## What is playable

- Choose one of the eight fixed starter eggs and hatch a partner.
- Feed, play, and rest using ordinary native care actions.
- Use **Find a demo encounter** to supply a bounded set of synthetic steps through native `Explore` events. The UI reports the actual simulated step count. This does not read a pedometer or change native encounter randomness.
- New and reset adventures default to **Auto** on hatch. Auto runs native battle logic and pauses for eligible manual timing capture. Existing saved mode choices are preserved. Switch to Manual before an encounter to use physical, heavy, or magic attacks.
- Aim a graded capture ring. Green uses the eligible native capture odds, orange half, and red one tenth (with the native positive minimum). Every attempt and random result is decided by the core; a green attempt can fail.
- Manage the 60-member collection, choose an active partner, and select up to three XP companions. Useful care builds bond; battle rewards supply XP. Evolve only when the native conditions are met.

The browser has no physical walking sensor, nearby radio play, trading, or music feature. The demo is not a recording and does not connect to the user's hardware.

## Visual provenance

Sprite atlases are lossless RGBA conversions of the current installed game’s indexed RGB565 device pixels. Background JPEGs are the actual in-game scenes. The renderer uses exact form IDs, audited native facing, opaque clip bounds and integer nearest-neighbor scaling, with the native Home, battle and capture actor positions on a 412 × 412 canvas. The browser HUD and surrounding controls are adapted for the web. Original starter eggs remain the existing game helper’s drawing.

Only the clips needed here are included, loaded for visible forms; original source sheets and import packs are not offered as asset downloads. Missing exact forms are explicitly labeled unavailable. [Source credits and rights notices](ART_SOURCES.md) accompany the [per-asset hash manifest](art/manifest.json).

`shared/capture-ring.js`, `shared/capture-ring-input.js`, `shared/party.js`, and `shared/starter-onboarding.js` are copied from the installed source. The capture timing sampler and input guards are the existing browser implementations. All other JavaScript is presentation, input, or local persistence; it does not recreate game rules.

## Save and reset

The native egg state requires its original mode invariant. The browser applies Auto immediately after successful hatch using the ordinary native Mode action, then saves both changes together. It does not rewrite existing partners’ settings.

Canonical native snapshots are stored under the isolated key `zark.browser-demo.v1.rules15`, in an envelope identifying rules 15 and schema 22. Native decoding checks integrity and state validity. **Reset demo** replaces only this key after confirmation. It does not clear other site storage or access hardware saves.

Where browser Web Locks and local storage are available, read/update/save operations are serialized across tabs. Other tabs load changes from the shared demo key. Without Web Locks, or if storage is unavailable, the game uses an explicitly labeled session-only save. An invalid existing save is preserved until the user chooses Reset demo.

## Controls and accessibility

The round display now receives device-style pointer input. Phone taps and swipes and desktop clicks and drags use the same 412-pixel logical coordinates, excluding the canvas border. Swipe left/right or tap the side zones to browse eggs, Home panels, companions, stats and evolution routes; use the labeled screen buttons to choose and go back. In Manual battle, select left/right, then make a separate upward swipe to commit. Center artwork taps do not select or attack. Auto opens its manual capture screen when the native battle pauses.

The adapter follows the installed native recognizer: a 204-pixel circular radius, horizontal/upward gestures of at least 40 pixels over 40–1500 ms with clear axis dominance, button taps of 20–1800 ms with at most 24 pixels of movement, and immediate capture on a fresh down inside the main area (y 80–330). Back uses the installed 124 × 38 logical-pixel target; the pending firmware enlargement is not represented yet. Ordinary gestures cancel on a second contact, interruption, context change, invalid drag or leaving the screen. A committed capture cannot be undone by later dragging or lifting. Only the display suppresses native page scrolling.

Screens and text remain a bounded browser adaptation: hardware settings and nearby play show their limits, while the labeled **Simulate walking** button sits outside the device. The larger Back target and other pending firmware changes require a later shared-core/UI refresh.

All actions also have optional ordinary HTML buttons under **Optional browser buttons & detailed stats**. Use Tab and Enter/Space to activate them. When the round screen is focused, Left/Right browse, Up commits a Manual move, Enter/Space activates its first enabled labeled action, and Escape uses Back. Left/Right and Home/End navigate the Play/Box/Evolve tabs. During capture, a fresh press in the round screen’s main area, the optional capture button, or the **D** key commits the current ring timing. Repeated keydowns and stale entering clicks are guarded by the shared input module. Blur, resize, or hidden-tab changes close the timing view without spending an attempt.

Game messages are announced through a polite live region. The canvas has a current text description and is backed by semantic controls, meters, and collection details. The reduced-motion preference stops decorative movement; the capture ring remains a timing mechanic while active.

## Runtime

The `runtime/` files are generated by `tools/browser-demo` from the shared native source using the pinned WebAssembly compiler documented there. The loader and module are served from this folder using relative URLs so the demo works under a GitHub project Pages path. See the repository's browser-demo build and verification instructions for source provenance, parity checks, and compiler licensing.

The touch adapter is tested against the native gesture boundaries; `tools/browser-demo/test-device-touch-input.mjs` checks mouse/touch event contracts and `test-device-view.mjs` exercises screen intents against the real shipped WASM. These are software checks, not physical finger-fit or mobile Safari certification.
