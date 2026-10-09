#pragma once

#include <cstdint>

namespace digivice::board {

enum class ProfileId : std::uint8_t {
    GenericSerial,
    Waveshare146,
    HeltecUnverified,
};

// Facts only: selecting a profile does not initialize or enable a peripheral.
// Unknown/unassigned GPIOs are -1; unknown memory/display dimensions are 0.
struct BoardProfile {
    ProfileId id = ProfileId::GenericSerial;
    const char* name = "Generic ESP32-S3 (serial only)";
    const char* displayController = nullptr;
    std::uint32_t flashBytes = 0;
    std::uint32_t psramBytes = 0;
    std::uint16_t displayWidth = 0;
    std::uint16_t displayHeight = 0;
    int powerHoldGpio = -1;       // High sustains battery power on Waveshare146.
    int powerSenseGpio = -1;      // Active-low PWR input on Waveshare146.
    int batteryAdcGpio = -1;
    int i2cSclGpio = -1;
    int i2cSdaGpio = -1;
    std::uint8_t imuI2cAddress = 0;
    int sdClockGpio = -1;
    int sdCommandGpio = -1;
    int sdData0Gpio = -1;
    std::uint8_t expanderI2cAddress = 0;
    std::uint8_t sdSelectExpanderMask = 0; // EXIO3 = TCA9554 P2, not GPIO3.
    int lcdClockGpio = -1;
    int lcdSelectGpio = -1;
    int lcdDataGpios[4] = {-1, -1, -1, -1};
    int lcdBacklightGpio = -1;
    int lcdTearingEffectGpio = -1; // Reserved; no TE interrupt in first port.
    std::uint8_t lcdResetExpanderMask = 0;
    std::uint8_t touchResetExpanderMask = 0;
    std::uint8_t touchI2cAddress = 0;
    int touchInterruptGpio = -1;
    int buttonNextGpio = -1;      // No approved external button wiring yet.
    int buttonConfirmGpio = -1;
    int buttonCandidateA = -1;    // Header facts, never an enabled input map.
    int buttonCandidateB = -1;
};

inline constexpr BoardProfile kGenericSerial{};

// Standard-glass SKU29565, native 412x412. Checked 2026-10-06 against:
// https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.46
// https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.46/ESP32-S3-Touch-LCD-1.46.pdf
// Vendor commit fda89ff2ff32eb1bce561901225c6a36fa684237 (2026-04-30),
// example/ESP-IDF-5.3.2/ESP32-S3-Touch-LCD-1.46-Test/main/:
// PWR_Key/PWR_Key.h, BAT_Driver/BAT_Driver.h, I2C_Driver/I2C_Driver.h,
// LCD_Driver/Display_SPD2010.h and QMI8658/QMI8658.c.
inline constexpr BoardProfile kWaveshare146 = [] {
    BoardProfile p{};
    p.id = ProfileId::Waveshare146;
    p.name = "Waveshare ESP32-S3-Touch-LCD-1.46 (SKU29565; unbenchmarked)";
    p.displayController = "SPD2010";
    p.flashBytes = 16U * 1024U * 1024U;
    p.psramBytes = 8U * 1024U * 1024U;
    p.displayWidth = 412;
    p.displayHeight = 412;
    p.powerHoldGpio = 7;
    p.powerSenseGpio = 6;
    p.batteryAdcGpio = 8;
    p.i2cSclGpio = 10;
    p.i2cSdaGpio = 11;
    p.imuI2cAddress = 0x6B;
    p.sdClockGpio = 14;
    p.sdCommandGpio = 17;
    p.sdData0Gpio = 16;
    p.expanderI2cAddress = 0x20;
    p.sdSelectExpanderMask = 0x04;
    p.lcdClockGpio = 40;
    p.lcdSelectGpio = 21;
    p.lcdDataGpios[0] = 46;
    p.lcdDataGpios[1] = 45;
    p.lcdDataGpios[2] = 42;
    p.lcdDataGpios[3] = 41;
    p.lcdBacklightGpio = 5;
    p.lcdTearingEffectGpio = 18;
    p.lcdResetExpanderMask = 0x02; // EXIO2 = P1, active low.
    p.touchResetExpanderMask = 0x01; // EXIO1 = P0, active low.
    p.touchI2cAddress = 0x53;
    p.touchInterruptGpio = 4;
    p.buttonCandidateA = 12;
    p.buttonCandidateB = 13;
    return p;
}();

// Owned V4 is believed to be ESP32-S3R2, not the distinct V4 R8 model.
// Exact revision is unknown; V4.2/V4.3 change pin functions. No GPIO map.
// https://wiki.heltec.org/docs/devices/open-source-hardware/esp32-series/lora-32/wifi-lora-32-v4/
inline constexpr BoardProfile kHeltecUnverified = [] {
    BoardProfile p{};
    p.id = ProfileId::HeltecUnverified;
    p.name = "Heltec WiFi LoRa 32 V4 (revision unverified; serial only)";
    p.flashBytes = 16U * 1024U * 1024U;
    p.psramBytes = 2U * 1024U * 1024U;
    return p;
}();

constexpr const BoardProfile& profile(ProfileId id) {
    switch (id) {
    case ProfileId::Waveshare146: return kWaveshare146;
    case ProfileId::HeltecUnverified: return kHeltecUnverified;
    case ProfileId::GenericSerial: return kGenericSerial;
    }
    return kGenericSerial;
}

} // namespace digivice::board
