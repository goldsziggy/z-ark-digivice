# First enclosures: NFC optional

Both first units run without an NFC reader. The existing capability contract is the switch: `board::initialize()` explicitly returns `nfc=false`, and the runtime never promotes it. No PN532 driver, task, reader probe, external reader pin assignment or NFC component dependency is present. An absent reader causes no startup error, retry loop or gameplay requirement. No NFC build toggle is offered until a working driver exists.

The physical screen supports starter selection, care, demo encounters, Tactical/Auto battles and capture without cards. It presents no NFC/card button or missing-reader warning. Card boosts remain optional core actions. USB `card 1` / `card 2` and browser card controls are intentional simulations; they do not enable NFC or establish that a physical card was read. Physical walking remains a separate capability; the current screen labels its encounter input as a demo.

Future reader support should default off, use the selected board's existing bus owner and bounded transactions, and set `Capabilities::nfc` true only after verified reader initialization. Reader absence or failure must retain normal local play and clear readiness. Map recognized cards to bounded game IDs, suppress repeat reads, and submit accepted actions through the existing main-task save-before-publish path. Add reader cleanup to the power barrier before enabling hardware. Do not infer wiring, identity or swipe direction from a build option or a UID.

## Focused verification

Run `bash firmware/tests/test_power_hal.sh` from the repository root. It compiles the actual board HAL against GPIO stubs with AddressSanitizer and UndefinedBehaviorSanitizer, checks absent NFC capability for GenericSerial and Waveshare initialization, and exercises all four power-startup fault points. It performs no serial or physical device access. The audit also checks `firmware/main/CMakeLists.txt`, `firmware/main/handheld_runtime.cpp`, `firmware/runtime/device_ui.cpp` and `core/game.cpp` for dependencies and required-card flows.

This is source/host verification. The main task owns ESP builds, per-unit saved-state checks and physical acceptance; no save reset or new reader is needed for the enclosure milestone.
