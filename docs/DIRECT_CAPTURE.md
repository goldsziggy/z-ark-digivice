# Direct capture and continuous ring motion

Historical `110212b` interface milestone. The current source adds
[graded capture and a shaded target](GRADED_CAPTURE.md); the binary hit/miss
odds described below remain the historical behavior of this milestone.

This refinement supersedes the prepared `0f56258` capture interface. Neither
version has been installed; both physical units remain on `8be26c6`. Device
availability must be confirmed before an installation.

## Player contract

Press anywhere in the main play area when the shrinking ring enters the target
band. The press immediately commits the timing and throws. There is no THROW
button, center-only hit target, hold or release timing. The main play area is
the circular screen between reference y=80 and y=330. Back and Skip / Resume
Fighting remain separate controls below that area.

A press is final: dragging away, lifting a finger or switching windows after
the press cannot undo an already submitted throw. Input requires a fresh press;
a held contact, its follow-up click, or a rapid double tap cannot issue another
throw. Re-arming requires release, settled save/presentation state, and the
450 ms guard. The existing uncertain-save retry sends the exact pending event.

The ring continuously shrinks from radius 100 to 20 over 2.4 seconds, resets
large and repeats. Target radii remain `[52,60,68,76][formId % 4]`, with a ±12
band. Timing comes from elapsed monotonic time, independently of rendering.
An on-target press uses the existing HP/rarity/level capture probability; it
does not guarantee a catch. An off-target press spends an attempt without a
capture RNG draw. Three attempts, wiggles, manual Auto capture and Skip keep
their previous game rules.

## Rendering and artwork

The browser uses animation-frame painting while the capture ring is active.
Firmware has a capture-specific cadence and redraws only the changing arena
when the surrounding screen is unchanged. Normal screens retain their existing
cadence. The same framebuffer, display rotation and DMA stripe are reused.
Scheduling targets and host bandwidth estimates are not measured hardware FPS.

The partial rectangle is 208×208 pixels: 86,528 RGB565 bytes, or 25.5% of the
339,488-byte full frame. The requested frame-start interval is 33 ms. The owner
loop waits at least one 10 ms RTOS tick during capture; other screens retain
their 20 ms wait. A conservative host timing model produced 25.4 frames/second
and a 29.5 ms maximum steady touch-call gap. Synchronous hardware transfers can
take longer; neither 30 FPS nor a 20 ms input bound is a measured guarantee.

Final private previews use a real production encounter, its audited sprite and
the actual capture background. Native previews use the real controller and
production DVA decoder; host JPEG decoding is identified separately from the
board's ROM decoder. Orientation, crop and source hashes are checked. No new
character art is imported, and private sprite bytes stay out of Git and public
packages. The user's requested private Library preview is the only delivery
of those rendered assets.

## Preservation and verification

The `0f56258` independent profile/world/pacing/offer seeds are retained. Care
remains schema 20/rules 13 with a 664-byte snapshot. This interface refinement
changes neither capture odds nor saved encounter selection. Native service
pairing/save sync, GPS and NFC hardware remain separate future work.

Run the affected checks from the repository root:

```sh
npm run test:capture-ring
npm run test:capture-runtime
npm run test:entropy
npm run build:esp:waveshare
```

The build command uses the existing official toolchain and never flashes.
Evidence and private previews are under `../deliverables/direct-capture-20261009/`.
Physical touch feel, actual frame rate, power draw and on-device acceptance
remain unmeasured until an authorized installation and playtest.
