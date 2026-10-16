#include "board_hal.hpp"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

namespace digivice::board {
namespace {
bool initialized = false;
bool displayPinsReleased = false;
esp_err_t holdStatus = ESP_ERR_NOT_SUPPORTED;
esp_err_t senseStatus = ESP_ERR_NOT_SUPPORTED;
bool holdAsserted = false;
esp_err_t busStatus = ESP_ERR_NOT_SUPPORTED;
i2c_master_bus_handle_t sharedBus = nullptr;
[[maybe_unused]] StaticSemaphore_t busMutexStorage{};
SemaphoreHandle_t busMutex = nullptr;
#if ((defined(CONFIG_DIGIVICE_BOARD_WAVESHARE_146) && CONFIG_DIGIVICE_BOARD_WAVESHARE_146) || (defined(CONFIG_DIGIVICE_BOARD_WAVESHARE_21) && CONFIG_DIGIVICE_BOARD_WAVESHARE_21)) && \
    ((defined(CONFIG_DIGIVICE_SD_ASSETS) && CONFIG_DIGIVICE_SD_ASSETS) || \
     (defined(CONFIG_DIGIVICE_DISPLAY_TOUCH) && CONFIG_DIGIVICE_DISPLAY_TOUCH))
i2c_master_dev_handle_t expander = nullptr;
#endif
}

const BoardProfile& selectedProfile() {
#if defined(CONFIG_DIGIVICE_BOARD_WAVESHARE_146) && CONFIG_DIGIVICE_BOARD_WAVESHARE_146
    return kWaveshare146;
#elif defined(CONFIG_DIGIVICE_BOARD_WAVESHARE_21) && CONFIG_DIGIVICE_BOARD_WAVESHARE_21
    return kWaveshare21;
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
#endif // 1.46-only battery hold
#if ((defined(CONFIG_DIGIVICE_BOARD_WAVESHARE_146) && CONFIG_DIGIVICE_BOARD_WAVESHARE_146) || (defined(CONFIG_DIGIVICE_BOARD_WAVESHARE_21) && CONFIG_DIGIVICE_BOARD_WAVESHARE_21))
#if (defined(CONFIG_DIGIVICE_QMI_PEDOMETER) && CONFIG_DIGIVICE_QMI_PEDOMETER) || \
    (defined(CONFIG_DIGIVICE_SD_ASSETS) && CONFIG_DIGIVICE_SD_ASSETS) || \
    (defined(CONFIG_DIGIVICE_DISPLAY_TOUCH) && CONFIG_DIGIVICE_DISPLAY_TOUCH) || \
    (defined(CONFIG_DIGIVICE_AUDIO_MOTION) && CONFIG_DIGIVICE_AUDIO_MOTION)
    if (selectedProfile().id == ProfileId::Waveshare21 || holdStatus == ESP_OK) {
        i2c_master_bus_config_t config{};
        config.i2c_port = 0;
        config.scl_io_num = static_cast<gpio_num_t>(selectedProfile().i2cSclGpio);
        config.sda_io_num = static_cast<gpio_num_t>(selectedProfile().i2cSdaGpio);
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
[[maybe_unused]] esp_err_t setExpanderOutput(std::uint8_t mask, bool high, bool buzzer = false,
                                          bool (*allowOn)(void*) = nullptr, void* context = nullptr) {
#if ((defined(CONFIG_DIGIVICE_BOARD_WAVESHARE_146) && CONFIG_DIGIVICE_BOARD_WAVESHARE_146) || (defined(CONFIG_DIGIVICE_BOARD_WAVESHARE_21) && CONFIG_DIGIVICE_BOARD_WAVESHARE_21)) && \
    ((defined(CONFIG_DIGIVICE_SD_ASSETS) && CONFIG_DIGIVICE_SD_ASSETS) || \
     (defined(CONFIG_DIGIVICE_DISPLAY_TOUCH) && CONFIG_DIGIVICE_DISPLAY_TOUCH))
    if (selectedProfile().id == ProfileId::Waveshare146 && !powerHoldReady()) return ESP_ERR_INVALID_STATE;
    // Joining the mutex wait queue avoids phase-locking with the 20ms IMU and
    // touch workers. Always wait at least one tick on supported tick rates.
    const auto wait = buzzer ? (pdMS_TO_TICKS(10) ? pdMS_TO_TICKS(10) : 1) : pdMS_TO_TICKS(100);
    if (!sharedBus || !busMutex) return ESP_ERR_INVALID_STATE;
    esp_err_t error = xSemaphoreTake(busMutex, wait) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
    if (buzzer && error == ESP_ERR_TIMEOUT) return ESP_ERR_NOT_FINISHED;
    if (error != ESP_OK) return error;
    if (high && allowOn && !allowOn(context)) { unlockSharedI2c(); return ESP_ERR_NOT_FINISHED; }
    if (!expander) {
        i2c_device_config_t config{};
        config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        config.device_address = selectedProfile().expanderI2cAddress;
        config.scl_speed_hz = 100000;
        error = i2c_master_bus_add_device(sharedBus, &config, &expander);
        if (error != ESP_OK) expander = nullptr;
    }
    constexpr std::uint8_t outputRegister = 0x01, directionRegister = 0x03;
    const auto readRegister = [buzzer](std::uint8_t reg, std::uint8_t& value) {
        return i2c_master_transmit_receive(expander, &reg, 1, &value, 1, buzzer ? 10 : 20);
    };
    const auto writeRegister = [buzzer](std::uint8_t reg, std::uint8_t value) {
        const std::uint8_t bytes[] = {reg, value};
        return i2c_master_transmit(expander, bytes, sizeof(bytes), buzzer ? 10 : 20);
    };
    std::uint8_t output = 0, direction = 0, observed = 0;
    if (error == ESP_OK) error = readRegister(outputRegister, output);
    if (error == ESP_OK && high && allowOn && !allowOn(context)) {
        unlockSharedI2c(); return ESP_ERR_NOT_FINISHED;
    }
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
    (void)mask; (void)high; (void)buzzer; (void)allowOn; (void)context;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
} // namespace

esp_err_t setBuzzer(bool on, bool (*allowOn)(void*), void* context) {
#if defined(CONFIG_DIGIVICE_BUZZER_21) && CONFIG_DIGIVICE_BUZZER_21
    const auto& p = selectedProfile();
    if (p.id != ProfileId::Waveshare21 || p.buzzerExpanderMask != 0x80)
        return ESP_ERR_NOT_SUPPORTED;
    static_assert((kWaveshare21.buzzerExpanderMask &
        (kWaveshare21.lcdResetExpanderMask | kWaveshare21.touchResetExpanderMask |
         kWaveshare21.lcdSelectExpanderMask | kWaveshare21.sdSelectExpanderMask)) == 0,
        "Buzzer must not overlap display/touch/SD ownership");
    // EXIO8/P7 is active high. Worker only: bounded mutex wait and <=10 ms per
    // transfer, one six-transfer masked/verified transaction group per edge.
    return setExpanderOutput(p.buzzerExpanderMask, on, true, allowOn, context);
#else
    (void)on; (void)allowOn; (void)context; return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t prepareSdCardSelect() {
#if ((defined(CONFIG_DIGIVICE_BOARD_WAVESHARE_146) && CONFIG_DIGIVICE_BOARD_WAVESHARE_146) || (defined(CONFIG_DIGIVICE_BOARD_WAVESHARE_21) && CONFIG_DIGIVICE_BOARD_WAVESHARE_21)) && \
    defined(CONFIG_DIGIVICE_SD_ASSETS) && CONFIG_DIGIVICE_SD_ASSETS
    if (!sdPinsAvailable()) return ESP_ERR_INVALID_STATE;
    return setExpanderOutput(selectedProfile().sdSelectExpanderMask, true);
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t setPeripheralReset(PeripheralReset peripheral, bool asserted) {
#if ((defined(CONFIG_DIGIVICE_BOARD_WAVESHARE_146) && CONFIG_DIGIVICE_BOARD_WAVESHARE_146) || (defined(CONFIG_DIGIVICE_BOARD_WAVESHARE_21) && CONFIG_DIGIVICE_BOARD_WAVESHARE_21)) && \
    defined(CONFIG_DIGIVICE_DISPLAY_TOUCH) && CONFIG_DIGIVICE_DISPLAY_TOUCH
    if (selectedProfile().lcdCommandPinsSharedWithSd && displayPinsReleased && peripheral == PeripheralReset::Display)
        return ESP_ERR_INVALID_STATE;
    const auto mask = peripheral == PeripheralReset::Display
        ? selectedProfile().lcdResetExpanderMask : selectedProfile().touchResetExpanderMask;
    return setExpanderOutput(mask, !asserted);
#else
    (void)peripheral; (void)asserted;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

bool sdPinsAvailable() {
    return !selectedProfile().lcdCommandPinsSharedWithSd || displayPinsReleased;
}
esp_err_t prepareDisplayCommandPins() {
    const auto& p = selectedProfile();
    if (!p.lcdCommandPinsSharedWithSd || displayPinsReleased) return ESP_ERR_INVALID_STATE;
    auto e = setExpanderOutput(p.lcdSelectExpanderMask, true);
    if (e == ESP_OK) e = setExpanderOutput(p.sdSelectExpanderMask, true);
    if (e == ESP_OK) e = setExpanderOutput(p.buzzerExpanderMask, false);
    return e;
}
esp_err_t setDisplaySelected(bool selected) {
    if (!selectedProfile().lcdCommandPinsSharedWithSd || displayPinsReleased) return ESP_ERR_INVALID_STATE;
    return setExpanderOutput(selectedProfile().lcdSelectExpanderMask, !selected);
}
esp_err_t releaseDisplayCommandPins() {
    // Caller has successfully removed the SPI device and freed its bus. A
    // checked, deasserted CS is the final gate before SD can use GPIO1/2.
    auto e = setDisplaySelected(false);
    if (e == ESP_OK) displayPinsReleased = true;
    return e;
}
} // namespace digivice::board
