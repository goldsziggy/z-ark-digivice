# C14-P19 assembly

Use the current C14-P19 front for the **Waveshare ESP32-S3-Touch-LCD-1.46 standard-cover SKU29565**. Do not use the superseded C13 front or substitute the earlier 2.1-inch/Heltec board. Case fit, actual printed bores, screw grip and assembled operation remain unverified.

The fifteen white receiving pilots A1–A4, C1–C3, D1–D3, E1–E3 and F1–F2 are now **1.9 mm**, rather than 1.6 mm. Black G1/G2 pilots remain **1.6 mm**; through-clearances remain **2.4 mm**. Screw axes, seats, lengths and axial cut spans are unchanged. These are smooth bores, not modeled threads.

## Guides

Use the [six-page assembly and screw PDF](docs/z-ark_Digivice_C14_P19_Assembly_and_Screws.pdf), or these phone-sized pages:

1. [Exploded touch-only core](docs/C14_P19_01_Exploded_Core_TOUCH_ONLY.png)
2. [Optional NFC/top assembly and front variants](docs/C14_P19_02_Top_NFC_and_Front_Variants.png)
3. [Exterior screw map](docs/C14_P19_Screw_Map_01_Exterior.png)
4. [Internal screw map](docs/C14_P19_Screw_Map_02_Internal.png)
5. [Connections](docs/C14_P19_Connections_Phone.png)
6. [Assembly order and screw schedule](docs/C14_P19_Assembly_Order_and_Screw_Schedule.png)

The views explicitly reuse C13 geometry. Read their C14-P19 pilot callouts; these are not newly rendered 1.9 mm bore close-ups. All seven guide files retain their supplied hashes.

## Parts per device

| Color | Part | Quantity |
|---|---|---:|
| White | **Choose one:** [touch-only front](stl/touch-only/front_shell_TOUCH_ONLY.stl) or [two-button front](stl/two-button/front_shell_PANEL_SWITCHES.stl) | 1 |
| Black | [Back shell](stl/common/back_shell_C10.stl) | 1 |
| Black | [Display carrier](stl/common/display_carrier_COMMON.stl) | 1 |
| Black | [Top antenna housing](stl/common/card_antenna_housing_TOP.stl) | 1 |
| White | [RF window](stl/common/card_RF_window_and_gap_TOP.stl) | 1 |
| Red | [Screen bezel](stl/common/screen_bezel_COMMON.stl) | 1 |
| Red | [Card-channel roof](stl/common/card_channel_roof_TOP.stl) | 1 |
| Black | [NFC reader tray](stl/common/nfc_reader_tray_C13.stl), only when adding optional NFC hardware | 1 optional |

The [battery frame](stl/optional-fit-gauges/battery_41x71_FIT_FRAME.stl) and [screen ring](stl/optional-fit-gauges/screen_fit_ring_46p8.stl) are optional gauges, not installed parts. The seven common parts remain identical to C13. No magnetic cassette is supplied.

## Print selection

[Download choices](downloads/README.md) distinguish four new white plates: touch-only or two-button, each with a repair front pair or full-white set. A repair pair contains **two fronts only** and reuses two existing RF windows. Each full-white plate includes **two fronts and two RF windows**. Common black8 and red4 projects are unchanged; the black project includes two optional reader trays even if the first build omits NFC.

Prepared toolpaths are for two devices on an Anycubic Kobra S1 with PLA and a 0.4 mm nozzle. Preserve supplied support/brim settings. Slicer estimates and nominal checks do not prove successful printing or assembly.

## Assembly order

1. Remove supports and debris before fitting electronics. Keep assembly-coordinate hardware/gauge meshes out of print plates; only the provided print-oriented STLs are printable parts.
2. With the back, face bezel, carrier and reader tray absent, complete the top assembly first: fit black housing F, the white RF window and red roof G, following the guide’s screw map. Then insert the display/board **from the front with the face bezel absent**, using the retained USB/display-tab lead-in.
3. Install the carrier from the rear with three M2×8 shell screws and three M2×4 board-post screws. Check actual metal-post blind depth before tightening.
4. For the current touch-only first build, omit the reader, NFC tray, GPS, external gameplay buttons and J9 harness. For an optional NFC assembly, route the stock antenna lead along the C13 interior route and install the rotated reader/tray from the rear, RF connector toward the display (+Y), host connector toward the grip (−Y). Fit its three additional M2×8 screws and soft restraint. Inspect actual cable bends and clearance before closure.
5. Finish the front face bezel after the carrier is installed; the top housing, white RF window and red roof were already fitted before the board. External gameplay switches are a mechanical option of the two-button front, but their wiring and firmware integration remain future work and are not enabled in the current firmware. Check seating, direct PWR access, USB insertion, touch access and wire restraint. Install the insulated, softly restrained battery and back only after checking the actual parts. Do not force closure or compress the cell/wires.

## Screws and remaining checks

The base touch-only/no-NFC build uses **17 screws: 4 × M2×16, 3 × M2×4 and 10 × M2×8**. The optional reader tray adds **3 × M2×8**, giving 20. This optional-assembly distinction does not change the frozen common print plates.

**Never test a full 16 mm screw directly in the bare white shell.** Nominal assembled penetration into white is only 6.4 mm, and a bare-shell test can bottom out. Check actual screws and grip gently with the correct assembled stack. Modeled head bounds and nominal pilots do not qualify purchased screw threads or retention.

Physical checks remain for PWR finger comfort and loose flex; actual USB overmold and seated shoulder; battery size, connector polarity and charger compatibility; screw grip; and any optional RF/J9 wiring. The battery assumption is 36 × 67 × 10 mm with a 41 × 71 × 13 mm reservation. No smaller cell is qualified. Optional RF routing is 87.129 mm plus 10 mm allowance within a nominal 100 mm lead; actual terminations and bends remain untested. The provisional Ø2.6 mm host bundle has only 0.25 mm clearance. No complete solder-free harness is established.
