# J9 harness for NFC and optional buttons

Verified against official Waveshare 1.46 documentation, product photographs and the STEP-derived connector geometry, not the user's inaccessible earlier photo bytes or a physical mating test.

**J9 is a male, unshrouded 2×10 header with 1.27 mm pitch.** Use a female 20-position socket. The vendor geometry has twenty 0.40 mm square pins projecting 3.95 mm beyond the body. Ordinary 2.54 mm Dupont sockets and JST 1.25 mm connectors do not mate correctly.

- [Official angled photograph](https://www.waveshare.com/media/catalog/product/cache/1/image/800x800/9df78eab33525d08d6e5fb8d27136e95/e/s/esp32-s3-lcd-1.46-4_3.jpg)
- [Official schematic/BOM](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.46/ESP32-S3-Touch-LCD-1.46.pdf)
- [Official mechanical archive](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.46/ESP32-S3-Touch-LCD-1.46_Structural%20Diagram-With%20Cover.rar)

## Assembled bench parts

| Part | Verified catalog facts |
|---|---|
| [Connectors Pro 20P-FF-15CM-2PK, Amazon B0CSB6RTFC](https://www.amazon.com/dp/B0CSB6RTFC) | 15 cm assembled female/female ribbon, 1.27 mm connector pitch, 2×10. Listing photo shows all twenty sockets open, with no blocked key position. |
| [Proto-Advantage DR127D254P20M, Amazon B097RTRBVP](https://www.amazon.com/dp/B097RTRBVP) | Installed male 1.27 mm 2×10 top connector and 2.54 mm DIP legs; 7.62 mm spacing between DIP rows. |

Route: **J9 → female/female ribbon → M breakout → 2.54 mm female jumper leads or breadboard**. The breakout is already assembled, per the [manufacturer](https://www.chipquik.com/store/product_info.php?products_id=2700031) and [drawing](https://www.chipquik.com/datasheets/DR127D254P20M.pdf). The manufacturer also sells the identical adapter directly. Amazon pages showed no featured offer during research; delivery/availability was not established and no purchase was made.

The cable's 0.635 mm specification describes its conductor spacing. Do not substitute a standard 2.54 mm IDC connector merely because its ribbon wires are described as 1.27 mm pitch. Avoid 10-position SWD cables or 20-position housings with a blocked socket.

**This is a nominally compatible bench harness, not an enclosure-qualified harness.** The adapter alone is 17.78 × 25.4 × 1.6 mm. No dimensioned cable-housing drawing was available; socket height, cable bend radius, speaker/board clearance and C13 case interference require mechanical checking. The outer polarizing tab does not key an unshrouded board header. Verify orientation electrically/by board labels; a stripe alone is insufficient.

## NFC and buttons

The ELECHOUSE PN5321 Mini has a separate 7-position MX1.25 host connector. Retain its matching supplied harness if present; it does not plug directly onto J9. Its documented host power input accepts regulated 3.3–5 V even though pin 6 is marked `5V`; logic is 3.3 V. Use the board's regulated 3V3 and GND, never raw battery voltage. The [official module manual](https://www.elechouse.com/wp-content/uploads/2024/07/Elechouse-Pn5321-Mini-Product-Manual-V1.pdf) is the connection authority.

For planned I²C: module pin 1 SCL → board SCL/GPIO10; module pin 2 SDA → board SDA/GPIO11; module pin 6 power → 3V3; module pin 7 → GND. I²C mode requires its documented solder-pad bridge with power disconnected; the SPI bridge stays open. A harness alone does not make this mode solder-free. The module defaults to UART with both bridges open; a future UART driver could instead use board TX43 → module RX1 and board RX44 → module TX2. No physical NFC driver is implemented yet, so that remains a driver/wiring choice rather than an active firmware setting.

Optional normally-open gameplay buttons can use candidate GPIO12 and GPIO13 to GND with internal pullups once their input adapter is implemented. Their firmware constants remain disabled. Bare stranded button leads need suitable crimp or terminal connectors; do not promise they plug reliably into a breadboard loose.

Use board signal labels: the official rear illustration numbers its connector positions oppositely to schematic J9 numbering. The rear drawing's top-left position is GPIO13 and next-left is GPIO12. Do not combine bare pin numbers from the two views. NFC still needs a harness for the touch-only enclosure; the onboard QMI8658 gyro does not.
