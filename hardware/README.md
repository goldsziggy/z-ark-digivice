# C14-P19 enclosure

This is the current enclosure export for the **Waveshare ESP32-S3-Touch-LCD-1.46 standard-cover SKU29565**. Both editable front variants now use **1.9 mm smooth receiving pilots in the white front**. Black pilots remain **1.6 mm** and through-clearances **2.4 mm**. This replaces the C13 white fronts; the common black, red and small white RF-window parts are unchanged.

The firmware supports touchscreen controls. The current touch-only first build omits external gameplay buttons, NFC, GPS and the J9 harness. It uses **17 M2 screws: 4 × 16 mm, 3 × 4 mm and 10 × 8 mm**. Adding the optional NFC reader tray uses another three M2×8 screws, for 20 total. The two-button front remains an editable mechanical alternative; its external gameplay switch wiring and firmware integration are future work and are not enabled in the current firmware.

**Physical fit, screw retention and assembled operation remain unverified.** The printed outline is still 60 × 135 × 37.4 mm. A 1.9 mm modeled pilot is not a measured printed bore or proof of thread grip; enlarging it reduces material available for grip.

- [Assembly instructions and part selection](c14-p19/ASSEMBLY.md)
- [Latest six-page assembly and screw guide](c14-p19/docs/z-ark_Digivice_C14_P19_Assembly_and_Screws.pdf)
- [Print download choices](c14-p19/downloads/README.md) and [release asset hashes](c14-p19/release-assets.json)
- [Touch-only editable source](c14-p19/cad/touch-only/README.txt) and [two-button editable source](c14-p19/cad/two-button/README.txt)
- [Rebuild prerequisites](c14-p19/REBUILD.md)
- [Source and exported-file hashes](c14-p19/publication-manifest.json)

Choose the two-front **repair** plate when reusing existing matching white RF windows. Choose a **full-white** plate for two fronts plus two unchanged RF windows. Both variants have these choices. Common black8 and red4 plates remain unchanged; black8 includes the two optional NFC reader trays. Toolpaths are specific to Anycubic Kobra S1, PLA and a 0.4 mm nozzle. Times and material are slicer estimates.

The eleven distinct print STLs comprise two alternative fronts, seven common parts including the optional NFC tray, and two fit gauges. Old filenames such as `back_shell_C10.stl` and `nfc_reader_tray_C13.stl` intentionally identify unchanged parts; use the current exported hashes.

Required vendor reference meshes and the Elechouse manual PDF are excluded. Consequently the editable CAD **does not rebuild from this checkout alone**. See the rebuild guide for official acquisition links and the unfilled mesh-conversion prerequisite. No project license has been selected, and no third-party redistribution permission is inferred.

The supplied diagrams reuse C13 geometry with explicit C14-P19 captions: white pilots are now 1.9 mm even where reused views cannot show that difference. This export checked identities and copied files; it did not regenerate geometry, reslice, print or perform a physical assembly.
