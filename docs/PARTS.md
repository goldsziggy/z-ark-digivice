# Parts for the C14-P19 touch-first build

Checked 9 October 2026. This list follows the current **Waveshare ESP32-S3-Touch-LCD-1.46 standard-cover SKU 29565** enclosure and the showcased `f74ee4c` firmware. It does not use the earlier P4, 2.1-inch board, large power-bank or magnetic-cassette prototype bill of materials. No links carry affiliate tags. No prices or stock guarantees are quoted.

## Core parts and supplies

| Part | Quantity and selection | Purchase/reference link | Verification and limits |
| --- | --- | --- | --- |
| Integrated ESP32-S3 touchscreen board | 1 × ESP32-S3-Touch-LCD-1.46, standard protective cover, SKU 29565, 412 × 412 | [Amazon listing, ASIN B0DRJBVQ3X](https://www.amazon.com/dp/B0DRJBVQ3X) · [Waveshare specification](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.46) | Amazon title/model identify the 1.46-inch protective-cover board. Official model mapping distinguishes SKU 29565 from no-cover 1.46B / 29564 and widened-cover 1.46C / 33837. Confirm the selected option. |
| LiPo battery candidate | 1 protected 1S 3.7 V pack compatible with the board charger; design envelope 36 × 67 × 10 mm | [Amazon search, not a verified listing](https://www.amazon.com/s?k=103665+3.7V+protected+lipo+MX1.25) · [DNK 103665 dimensional reference](https://www.dnkpower.com/products/103665-3-7v-3000mah-lithium-polymer-battery/) | No exact cell is selected or qualified. The reference is not a plug-ready recommendation. Measure the entire pack including protection, wrapping and leads; check MX1.25 two-pin mating, polarity and charger compatibility. The reserved CAD bay is 41 × 71 × 13 mm; the extra space is not an invitation to enlarge the cell. |
| M2 screws | 3 × 4 mm, 10 × 8 mm, 4 × 16 mm; 17 total | [Amazon search](https://www.amazon.com/s?k=M2+button+head+screw+kit+4mm+8mm+16mm) · [Current assembly schedule](https://github.com/goldsziggy/z-ark-digivice/blob/main/hardware/c14-p19/ASSEMBLY.md#screws-and-remaining-checks) | Design head bounds Ø4 × 2 mm. Actual threads, heads, printed pilots and retention are unverified. No nuts, inserts or washers are modeled. Never insert a full 16 mm screw into the bare white shell as a fit test. |
| Printed enclosure | Black, white and red PLA; 1.75 mm filament for the published setup | [Amazon search](https://www.amazon.com/s?k=PLA+1.75mm+black+white+red) · [Print choices](https://github.com/goldsziggy/z-ark-digivice/tree/main/hardware/c14-p19/downloads) · [Anycubic PLA reference](https://store.anycubic.com/products/pla-filament) | Choose the C14-P19 touch-only front and corresponding common parts. Supplied toolpaths are specific to Anycubic Kobra S1, PLA and a 0.4 mm nozzle; use STL geometry and a suitable slicer profile for other printers. |
| microSD card | 1 for the complete downloaded-asset pack | [Amazon search](https://www.amazon.com/s?k=microSD+card) · [Installation guidance](https://github.com/goldsziggy/z-ark-digivice/blob/main/docs/INSTALLATION.md) | No brand/capacity is qualified here. Showcased firmware supports FAT32/exFAT, MBR or unpartitioned cards, not GPT. Resident fallback content is available without a card. This list does not instruct formatting an existing card. |
| USB-C data cable | 1, with host end appropriate to the computer | [Amazon search](https://www.amazon.com/s?k=usb+c+data+cable) · [Waveshare connections](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.46) | Data capability is required for loading firmware/assets. Check the plug overmold against the enclosure opening. No exact cable is fit-qualified. |

Use soft, insulated battery restraint and follow the current assembly guide. The printed enclosure's physical fit, screw retention, battery connector and complete assembled operation remain unverified; a matching nominal dimension is not a fit test.

## Optional alternatives, omitted from the current build

- **NFC:** [ELECHOUSE PN5321 MINI](https://www.elechouse.com/product/pn532-mini/), with the 10 × 25 mm ferrite antenna reference, approximately 25 × 16.4 mm reader and nominal 100 mm antenna lead. The optional printed tray adds 3 × M2×8 screws, making 20 total. No exact Amazon listing was verified, so the manufacturer is the fallback. Generic larger PN532 V2/V3 boards are not substitutes for this geometry. Harness, RF routing and firmware integration remain unqualified.
- **External buttons:** [DaierTek B09C8C53DM on Amazon](https://www.amazon.com/dp/B09C8C53DM), verified as prewired SPST normally-open momentary switches with 7 mm mounting holes and 150 mm leads. Two are referenced by the alternate two-button front. They are not part of the touch-only front; physical stack, wiring and firmware integration remain unverified.
- **MagSafe:** a future enclosure idea, excluded from this shopping list. The earlier magnetic cassette requires a roughly 75 mm rear adapter and is incompatible with the current 60 mm C14-P19 outline. No magnetic cassette is supplied or compatible purchase asserted.

## Source of the build requirements

- [Current hardware overview](https://github.com/goldsziggy/z-ark-digivice/blob/main/hardware/README.md)
- [C14-P19 assembly and screw schedule](https://github.com/goldsziggy/z-ark-digivice/blob/main/hardware/c14-p19/ASSEMBLY.md)
- [Touch-only mechanical contract](https://github.com/goldsziggy/z-ark-digivice/blob/main/hardware/c14-p19/cad/touch-only/reference/MECHANICAL_CONTRACT.json)
- [Touch-only fastener contract](https://github.com/goldsziggy/z-ark-digivice/blob/main/hardware/c14-p19/cad/touch-only/reference/FASTENER_CONTRACT.json)
- [Firmware microSD setup and limits](https://github.com/goldsziggy/z-ark-digivice/blob/main/docs/USB_SD_TRANSFER.md)

[Back to the showcase](./#parts) · [Showcase sources](SHOWCASE_SOURCES.md)
