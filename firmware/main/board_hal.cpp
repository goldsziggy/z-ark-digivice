#include "board_hal.hpp"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

namespace digivice::board {
namespace {
bool initialized = false;
esp_err_t holdStatus = ESP_ERR_NOT_SUPPORTED;
esp_err_t senseStatus = ESP_ERR_NOT_SUPPORTED;
bool holdAsserted = false;
esp_err_t busStatus = ESP_ERR_NOT_SUPPORTED;
i2c_master_bus_handle_t sharedBus = nullptr;
[[maybe_unused]] StaticSemaphore_t busMutexStorage{};
SemaphoreHandle_t busMutex = nullptr;
#if defined(CONFIG_DIGIVICE_BOARD_WAVESHARE_146) && CONFIG_DIGIVICE_BOARD_WAVESHARE_146 && \
    ((defined(CONFIG_DIGIVICE_SD_ASSETS) && CONFIG_DIGIVICE_SD_ASSETS) || \
     (defined(CONFIG_DIGIVICE_DISPLAY_TOUCH) && CONFIG_DIGIVICE_DISPLAY_TOUCH))
i2c_master_dev_handle_t expander = nullptr;
#endif
}

const BoardProfile& selectedProfile() {
#if defined(CONFIG_DIGIVICE_BOARD_WAVESHARE_146) && CONFIG_DIGIVICE_BOARD_WAVESHARE_146
    return kWaveshare146;
#elif defined(CONFIG_DIGIVICE_BOARD_HELTEC_UNVERIFIED) && CONFIG_DIGIVICE_BOARD_HELTEC_UNVERIFIED
    return kHeltecUnverified;
#else
    return kGenericSerial;
#endif
}

Capabilities initialize() {
    if (initialized) return {};
    initialized = true;
#if defined(CONFIG_DIGIVICE_BOARD_WAVESHARE_146) && CONFIG_DIGIVICE_BOARD_WAVESHARE_146
    // Pin truth: pinned vendor PWR_Key/PWR_Key.h + board_profiles.hpp sources.
    // Unlike the demo's low pulse/wait, preload high then enable output promptly.
    // This is a proposed source sequence, NOT a battery/USB bench validation.
    // Do not reset, pulse low, repurpose PWR/BOOT, or infer charging controls.
    constexpr auto hold = static_cast<gpio_num_t>(kWaveshare146.powerHoldGpio);
    holdStatus = gpio_set_level(hold, 1);
    if (holdStatus == ESP_OK) {
        gpio_config_t config{};
        config.pin_bit_mask = 1ULL << kWaveshare146.powerHoldGpio;
        config.mode = GPIO_MODE_OUTPUT;
        config.pull_up_en = GPIO_PULLUP_DISABLE;
        config.pull_down_en = GPIO_PULLDOWN_DISABLE;
        config.intr_type = GPIO_INTR_DISABLE;
        holdStatus = gpio_config(&config);
        if (holdStatus == ESP_OK) holdStatus = gpio_set_level(hold, 1);
    }
    if (holdStatus == ESP_OK) {
        holdAsserted = true;
        gpio_config_t sense{};
        sense.pin_bit_mask = 1ULL << kWaveshare146.powerSenseGpio;
        sense.mode = GPIO_MODE_INPUT;
        sense.pull_up_en = GPIO_PULLUP_DISABLE; // Board R1 pulls Key_BAT to 3V3.
        sense.pull_down_en = GPIO_PULLDOWN_DISABLE;
        sense.intr_type = GPIO_INTR_DISABLE;
        senseStatus = gpio_config(&sense);
    }
#if (defined(CONFIG_DIGIVICE_QMI_PEDOMETER) && CONFIG_DIGIVICE_QMI_PEDOMETER) || \
    (defined(CONFIG_DIGIVICE_SD_ASSETS) && CONFIG_DIGIVICE_SD_ASSETS) || \
    (defined(CONFIG_DIGIVICE_DISPLAY_TOUCH) && CONFIG_DIGIVICE_DISPLAY_TOUCH) || \
    (defined(CONFIG_DIGIVICE_AUDIO_MOTION) && CONFIG_DIGIVICE_AUDIO_MOTION)
    if (holdStatus == ESP_OK) {
        i2c_master_bus_config_t config{};
        config.i2c_port = 0;
        config.scl_io_num = static_cast<gpio_num_t>(kWaveshare146.i2cSclGpio);
        config.sda_io_num = static_cast<gpio_num_t>(kWaveshare146.i2cSdaGpio);
        config.clk_source = I2C_CLK_SRC_DEFAULT;
        config.glitch_ignore_cnt = 7;
        // Matches the vendor's internal-pullup setting; inspect the actual
        // board and aggregate pull-ups before adding external I2C modules.
        config.flags.enable_internal_pullup = true;
        busStatus = i2c_new_master_bus(&config, &sharedBus);
        if (busStatus != ESP_OK) sharedBus = nullptr;
        if (sharedBus) {
            busMutex = xSemaphoreCreateMutexStatic(&busMutexStorage);
            if (!busMutex) busStatus = ESP_ERR_NO_MEM;
        }
    } else busStatus = ESP_ERR_INVALID_STATE;
#endif
#endif
    // QMI support is a read-only counter adapter requiring verified revision
    // and external configuration. No measured steps/display/touch/NFC/GPS,
    // network reachability, battery telemetry or charging control is asserted.
    return {false, false, false, false, false, false, false, false};
}

const char* boardName() { return selectedProfile().name; }
bool powerHoldReady() { return holdStatus == ESP_OK; }
esp_err_t powerHoldStatus() { return holdStatus; }
bool powerControlAvailable() { return initialized && holdStatus == ESP_OK && senseStatus == ESP_OK; }
bool powerHoldAsserted() { return holdAsserted; }
esp_err_t readPowerPressed(bool& pressed) {
#if defined(CONFIG_DIGIVICE_BOARD_WAVESHARE_146) && CONFIG_DIGIVICE_BOARD_WAVESHARE_146
    if (!initialized || senseStatus != ESP_OK) return ESP_ERR_INVALID_STATE;
    pressed = gpio_get_level(static_cast<gpio_num_t>(kWaveshare146.powerSenseGpio)) == 0;
    return ESP_OK;
#else
    (void)pressed; return ESP_ERR_NOT_SUPPORTED;
#endif
}
esp_err_t setPowerHold(bool asserted) {
#if defined(CONFIG_DIGIVICE_BOARD_WAVESHARE_146) && CONFIG_DIGIVICE_BOARD_WAVESHARE_146
    if (!initialized || senseStatus != ESP_OK) return ESP_ERR_INVALID_STATE;
    const auto result = gpio_set_level(static_cast<gpio_num_t>(kWaveshare146.powerHoldGpio), asserted ? 1 : 0);
    if (result == ESP_OK) holdAsserted = asserted;
    return result;
#else
    (void)asserted; return ESP_ERR_NOT_SUPPORTED;
#endif
}
i2c_master_bus_handle_t sharedI2cBus() { return sharedBus; }
esp_err_t sharedI2cStatus() { return busStatus; }
esp_err_t lockSharedI2c(std::uint32_t timeoutMs) {
    if (!sharedBus || !busMutex) return ESP_ERR_INVALID_STATE;
    return xSemaphoreTake(busMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}
void unlockSharedI2c() { if (busMutex) xSemaphoreGive(busMutex); }

namespace {
[[maybe_unused]] esp_err_t setExpanderOutput(std::uint8_t mask, bool high) {
#if defined(CONFIG_DIGIVICE_BOARD_WAVESHARE_146) && CONFIG_DIGIVICE_BOARD_WAVESHARE_146 && \
    ((defined(CONFIG_DIGIVICE_SD_ASSETS) && CONFIG_DIGIVICE_SD_ASSETS) || \
     (defined(CONFIG_DIGIVICE_DISPLAY_TOUCH) && CONFIG_DIGIVICE_DISPLAY_TOUCH))
    if (!powerHoldReady()) return ESP_ERR_INVALID_STATE;
    esp_err_t error = lockSharedI2c();
    if (error != ESP_OK) return error;
    if (!expander) {
        i2c_device_config_t config{};
        config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        config.device_address = kWaveshare146.expanderI2cAddress;
        config.scl_speed_hz = 100000;
        error = i2c_master_bus_add_device(sharedBus, &config, &expander);
        if (error != ESP_OK) expander = nullptr;
    }
    constexpr std::uint8_t outputRegister = 0x01, directionRegister = 0x03;
    const auto readRegister = [](std::uint8_t reg, std::uint8_t& value) {
        return i2c_master_transmit_receive(expander, &reg, 1, &value, 1, 20);
    };
    const auto writeRegister = [](std::uint8_t reg, std::uint8_t value) {
        const std::uint8_t bytes[] = {reg, value};
        return i2c_master_transmit(expander, bytes, sizeof(bytes), 20);
    };
    std::uint8_t output = 0, direction = 0, observed = 0;
    if (error == ESP_OK) error = readRegister(outputRegister, output);
    output = high ? static_cast<std::uint8_t>(output | mask)
                  : static_cast<std::uint8_t>(output & static_cast<std::uint8_t>(~mask));
    // Preload the selected level BEFORE direction; preserve every other bit.
    if (error == ESP_OK) error = writeRegister(outputRegister, output);
    if (error == ESP_OK) error = readRegister(directionRegister, direction);
    direction = static_cast<std::uint8_t>(direction & static_cast<std::uint8_t>(~mask));
    if (error == ESP_OK) error = writeRegister(directionRegister, direction);
    if (error == ESP_OK) error = readRegister(outputRegister, observed);
    if (error == ESP_OK && observed != output) error = ESP_ERR_INVALID_STATE;
    if (error == ESP_OK) error = readRegister(directionRegister, observed);
    if (error == ESP_OK && observed != direction) error = ESP_ERR_INVALID_STATE;
    unlockSharedI2c();
    return error;
#else
    (void)mask; (void)high;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
} // namespace

esp_err_t prepareSdCardSelect() {
#if defined(CONFIG_DIGIVICE_BOARD_WAVESHARE_146) && CONFIG_DIGIVICE_BOARD_WAVESHARE_146 && \
    defined(CONFIG_DIGIVICE_SD_ASSETS) && CONFIG_DIGIVICE_SD_ASSETS
    return setExpanderOutput(kWaveshare146.sdSelectExpanderMask, true);
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t setPeripheralReset(PeripheralReset peripheral, bool asserted) {
#if defined(CONFIG_DIGIVICE_BOARD_WAVESHARE_146) && CONFIG_DIGIVICE_BOARD_WAVESHARE_146 && \
    defined(CONFIG_DIGIVICE_DISPLAY_TOUCH) && CONFIG_DIGIVICE_DISPLAY_TOUCH
    const auto mask = peripheral == PeripheralReset::Display
        ? kWaveshare146.lcdResetExpanderMask : kWaveshare146.touchResetExpanderMask;
    return setExpanderOutput(mask, !asserted);
#else
    (void)peripheral; (void)asserted;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

} // namespace digivice::board
