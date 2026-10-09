# Historical 2.1-inch hardware research

> Superseded board research. The published firmware and C14-P19 enclosure target **Waveshare ESP32-S3-Touch-LCD-1.46, standard glass, SKU29565**, with a 412 × 412 display. Use the [current firmware guide](FIRMWARE.md), [connector guide](HARDWARE_CONNECTORS.md) and [C14-P19 hardware](../hardware/README.md). The 2.1-inch pin assignments and power notes below do not apply to the current target.

Researched 2026-10-04, without purchase or bench validation. The earlier candidate was the **flat Waveshare ESP32-S3-Touch-LCD-2.1, SKU 28169**. It is distinct from the 2.1B curved lens, earlier P4 concept, and T-Watch Ultra.

## Board facts and authority

The [current official board documentation](https://docs.waveshare.com/ESP32-S3-Touch-LCD-2.1) specifies an ESP32-S3R8, dual LX7 up to 240 MHz, 512 KB internal SRAM, 16 MB flash, 8 MB PSRAM, 480 × 480 touch display, Wi-Fi/BLE, QMI8658 accelerometer/gyro, PCF85063 RTC, microSD, buzzer, and battery circuitry. Its expander has no spare exposed channels. The 3.3 V regulator's 800 mA maximum is not an available-peripheral-current guarantee.

The [official schematic](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-2.1/ESP32-S3-Touch-LCD-2.1_schematic_diagram.pdf) was inspected as text and rendered image. Downloaded research evidence remains local and is not part of this publication.

| Interface | Confirmed board mapping / consequence |
| --- | --- |
| Shared I2C | SCL GPIO7; SDA GPIO15. Touch, expander, RTC and IMU use it. Own the bus once; do not remap it or initialize incompatible I2C driver stacks alongside one another. |
| UART | TX GPIO43; RX GPIO44. FSUSB42 mux makes the external 4-pin UART unavailable when USB-to-UART Type-C is connected. Reserve logging/programming strategy before assigning GPS or NFC. |
| Native USB | D− GPIO19; D+ GPIO20, also present on the multifunction header. Native USB Type-C is J3; J2 is the separate CH343 USB-to-UART socket. Do not allocate these pins to another device while retaining native USB. |
| GPIO0 | Exposed but also BOOT. Avoid an external device holding it in the download strap state. |
| Display / SD | LCD configuration and SD share GPIO1/2; SD D0 is GPIO42. Vendor initialization sequences and bus ownership matter. |
| Other signals | Backlight GPIO6; touch interrupt GPIO16; battery sense GPIO4. QMI8658 interrupt lines and RTC interrupt terminate at the expander. No direct IMU wake path to ESP32 is established; the expander INT appears unconnected in the supplied schematic. |

The [FAQ](https://docs.waveshare.com/ESP32-S3-Touch-LCD-2.1/FAQ) lists occupied I2C addresses `0x15, 0x20, 0x51, 0x6B, 0x7E`, single-touch input and RGB565 operation. Treat the unusual `0x7E` listing as a vendor observation, not an address to assign. Verify actual devices and bus waveforms on the purchased revision.

## Exact inspected example, rather than an assumed BSP

The [official resources](https://docs.waveshare.com/ESP32-S3-Touch-LCD-2.1/Resources-And-Documents) link this [demo ZIP](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-2.1/ESP32-S3-Touch-LCD-2.1-Demo.zip). Downloaded temporary copy: `/tmp/digivice-waveshare-2p1-demo.zip`.

SHA-256: `ddb55c65b37cc204f071b505abc7636504a657485ae9f3fabcf8bcd9418654aa`.

Archive project: `ESP-IDF/ESP32-S3-Touch-LCD-2.1-Test/`.

- `components/lvgl__lvgl/lvgl.h` and its manifest identify **LVGL 8.2.0**. `main/idf_component.yml` specifies `idf >=4.4` and `lvgl/lvgl ~8.2.0`. The Arduino example is separate and uses LVGL 8.3.10.
- The legacy Wiki snapshot requests IDF >=5.3.1. The [current IDF tutorial](https://docs.waveshare.com/ESP32-S3-Touch-LCD-2.1/ESP-IDF) shows 5.5.2 only as an illustrative installation screenshot and explicitly asks developers to match their example. **No exact tested IDF patch release is pinned by this archive.** IDF 5.3.1 is a reasonable initial compatibility candidate, not a verified build claim. Pin the release actually compiled in this project; a display port then needs a separate compile and bench check. Do not assume a move to IDF 6 or LVGL 9 is drop-in.
- Vendor-specific code lives in `main/EXIO/`, `LCD_Driver/`, `Touch_Driver/`, `I2C_Driver/`, and `LVGL_Driver/`. The display is ST7701S RGB via `esp_lcd`; touch is CST820. This is a source example, not evidence of a separately versioned official registry BSP.
- Required minimum order is I2C and expander initialization, `LCD_Init()`, `Touch_Init()`, then `LVGL_Init()`. Keep the round-screen UI adapter above those functions and the game core below no hardware dependencies. The example also initializes RTC, motion, SD and radio, which need not all be enabled for first display bring-up.

Two source defects/settings need deliberate treatment before reuse:

1. With double-framebuffer mode disabled, `LVGL_Driver.c` allocates each draw buffer for `480 * 100` pixels but calls `lv_disp_draw_buf_init` with `480 * 480`. Correct the advertised capacity if that path is enabled. The default is double framebuffer.
2. `SD_Card/SD_MMC.c` uses `format_if_mount_failed = true`. Product startup must leave existing cards untouched on mount failure and fall back to built-in assets. Do not inherit demo auto-format behavior.

## Resource planning: calculated, not measured on device

| Item | Amount / status |
| --- | --- |
| One RGB565 framebuffer | 480 × 480 × 2 = 460,800 bytes (450 KiB), calculated |
| Vendor default two framebuffers | 921,600 bytes (900 KiB) in PSRAM, calculated |
| Alternative two 100-line draw buffers | 192,000 bytes total plus panel framebuffer; requires the capacity fix above |
| Original 64 × 64 RGB565 sprite | 8,192 bytes/frame; four frames = 32 KiB, calculated content planning |
| Vendor memory configuration | Octal PSRAM at 80 MHz; display pixel clock 18 MHz; not a frame-rate benchmark |
| RAM, firmware image, stack high water, battery runtime | Measure with compiled product firmware and hardware; no result claimed here |

[Espressif's RGB LCD guide](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/api-reference/peripherals/lcd/rgb_lcd.html) explains that flash, PSRAM, CPU and LCD DMA contend for bandwidth. Test display stability while saving, downloading, using Wi-Fi and reading assets. Keep active sprites in a bounded RAM cache and schedule durable writes outside animation bursts where practical. Allocate a clear internal-RAM reserve for networking and DMA instead of treating all 8 MB PSRAM as interchangeable RAM.

## NFC, GPS, motion and power decisions still open

- **NFC candidate:** [ELECHOUSE PN5321 MINI Rev. 1.5 manual](https://www.elechouse.com/wp-content/uploads/2024/07/Elechouse-Pn5321-Mini-Product-Manual-V1.pdf) specifies 3.3 V logic, 3.3–5 V supply, UART/I2C/SPI, default UART, and I2C address 0x24 after selecting only the I2C pads while unpowered. A 10 × 25 mm external antenna fits the compact concept. First validate firmware-version response, then stationary tag read, then card dwell and repeat suppression. A UID identifies a card; it is not proof of authenticity or swipe direction. No SPI/reset GPIO is assigned yet. Two optical direction sensors require additional verified IO and supply design.
- **GPS candidate:** [Adafruit PA1010D #4415](https://www.adafruit.com/product/4415) provides UART/I2C and an internal antenna. Its [pinout guide](https://learn.adafruit.com/adafruit-mini-gps-pa1010d-module/pinouts) says I2C pull-ups go to VIN: use a verified 3.3 V supply/logic arrangement on this bus. Its 30 mA navigation figure is a vendor value, not a full-device runtime measurement. Quiet power, sky view and final enclosure reception need testing. Keep GPS optional and do not block encounters while waiting for a fix.
- **Shared bus:** I2C is the simplest provisional way to connect both external devices without UART mux conflicts. Start conservatively, check combined pull-ups and cable capacitance, enforce finite transaction timeouts, and ensure a missing/stuck peripheral does not stall touch or game state. No wiring harness is approved by these logical assignments.
- **Steps:** QMI8658 supplies acceleration; the example does not supply a validated pedometer. Implement and test a step estimator, orientation robustness, false-shake rejection and batching separately. Deep-sleep step retention/wakeup remains a hardware feasibility question. The simulator's step events prove game behavior, not pedometer accuracy.
- **Battery:** existing compact research proposes protected Adafruit #2011 2000 mAh and external charging. Its maximum allowed charge current is 2 A, while the stock ETA6098 / 82 kΩ setting is 2 A typical without a sufficient guaranteed upper bound. The existing research therefore does **not** approve onboard charging. MX1.25 main battery vs JST-PH candidate connector and physical polarity must be verified. Use the existing historical battery research (local reference omitted); do not connect the proposed cell and USB together pending resolution. RTC battery socket is separate.

For the next bench stage: purchase selection first; vendor display/touch-only bring-up over USB with no proposed battery; add NVS recovery and asset fallback; add motion sampling; then NFC and optional GPS one at a time. Measure screen-on, dim, radio-sync and sleep current, supply dips and wake behavior before making runtime claims. No device was flashed and nothing was ordered during this research.
