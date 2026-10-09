# Display orientation in the enclosure

The sideways-screen report is a display/input alignment issue. C13 and C14-P19 mount the Waveshare SKU29565 board 90° clockwise relative to the official mechanical drawing: with the handle down and screen facing the viewer, USB is on the left, PWR is lower-right and the swipe housing is above the screen. Keep this mechanical mounting unchanged; rotating the board would misalign its asymmetric mounting posts and port openings.

The former firmware sent the logical image to the panel without rotation and returned native touch coordinates. The pinned SPD2010 driver explicitly rejects `swap_xy`, so quarter turns are performed while copying RGB565 pixels into the existing DMA stripe. Touch applies the inverse transform once when decoding a new contact. The shared game and setup UI continue to use their original 412×412 logical coordinates.

## Confirmed case direction

The photo supplied with the report could not be downloaded because access was denied; its pixels were not inspected. The user then confirmed directly: “With the device up and down, our Impmon’s legs are to the left.” That establishes image bottom-left/top-right when held upright. **The display-enabled Waveshare case profile now selects a 90° counterclockwise image rotation.** Touch uses the corresponding inverse map `(411 − nativeY, nativeX)`. The generic Kconfig fallback remains Native. Physical acceptance and installation are still pending.

With the handle pointing down, identify which way the **top of the currently displayed picture or text** points:

| Current picture top | Required image correction | Build option |
|---|---|---|
| Toward the viewer's left | 90° clockwise from native | `CONFIG_DIGIVICE_DISPLAY_ORIENTATION_CW90=y` |
| Toward the viewer's right | 90° counterclockwise from native | `CONFIG_DIGIVICE_DISPLAY_ORIENTATION_CCW90=y` |

The corresponding inverse touch mapping is selected by the same option; there is no independent touch rotation setting. The selected case direction is recorded in the reviewed `waveshare` board-profile defaults, not only in generated `sdkconfig`, which the build wrapper recreates.

## Boundaries and checks

Rendering still uses the existing 339,488-byte PSRAM framebuffer and 13,184-byte internal DMA stripe. Rotation does not allocate another frame. A full frame still transfers 339,488 pixel bytes in 26 stripes; strided-read CPU cost and real-device frame time remain unmeasured.

The mapping covers partial rectangles, supplied source strides and the final short stripe. Boot clearing submits zeroed panel stripes directly, avoiding source/destination aliasing in the rotation helper. The helper rejects overlapping buffers and invalid dimensions before writing output.

Touch retains the existing bounded I2C wait, freshness, cancellation, release rearming and cached-point behavior. Taps, horizontal carousels, upward tactical commits and capture flicks are interpreted in logical UI coordinates. Saves, downloaded assets, pairing state and game rules are untouched. Cosmetic IMU tilt has a separate board-axis calibration and is not qualified by this display correction.

Focused host checks cover both quarter turns with pixel/rectangle goldens and actual game/setup UI gesture traces. Hardware acceptance must still verify an upright image, aligned taps, left/right browsing, upward commits and capture flick direction on the assembled device. Do not hatch, capture or otherwise change a preserved save merely to test orientation without authorization.

No USB access or flashing is authorized by the orientation photo. Confirm the intended units are plugged in for an update before opening any port. The local publication export remains frozen at its previously reviewed source until these firmware changes are finalized and verified. The user now reports a hatched Impmon; preserve each actual device save rather than assuming the earlier egg checkpoints remain current.
