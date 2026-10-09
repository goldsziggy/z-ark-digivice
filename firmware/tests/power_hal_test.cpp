#include "board_hal.hpp"
#include "driver/gpio.h"
#include "freertos/semphr.h"
#include <cstdio>
#include <cstdlib>

namespace {
unsigned checks = 0, failures = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::fprintf(stderr, "%s:%d %s\n", __FILE__, __LINE__, #x); } } while (false)
enum class Kind { Set, Config, Read };
struct Event { Kind kind{}; int pin = -1; std::uint32_t level = 99; gpio_config_t config{}; };
Event events[32]{};
unsigned count = 0, writes = 0, failWrite = 0, semaphoreCalls = 0;
int sensedLevel = 1;
void record(Event event) {
    CHECK(count < 32);
    if (count < 32) events[count++] = event;
}
esp_err_t writeResult() { return ++writes == failWrite ? ESP_FAIL : ESP_OK; }
void checkCapabilities(const digivice::board::Capabilities& c) {
    CHECK(!c.display && !c.touch && !c.motionSteps && !c.nfc && !c.gps &&
          !c.network && !c.batteryTelemetry && !c.chargingControl);
}
[[maybe_unused]] void checkConfig(const Event& event, int pin, gpio_mode_t mode) {
    CHECK(event.kind == Kind::Config && event.config.pin_bit_mask == (1ULL << pin));
    CHECK(event.config.mode == mode);
    CHECK(event.config.pull_up_en == GPIO_PULLUP_DISABLE && event.config.pull_down_en == GPIO_PULLDOWN_DISABLE);
    CHECK(event.config.intr_type == GPIO_INTR_DISABLE);
}
void checkOnlyPowerPins() {
    for (unsigned i = 0; i < count; ++i) {
        const auto& event = events[i];
        if (event.kind == Kind::Set) CHECK(event.pin == 7); // Never GPIO6 output.
        else if (event.kind == Kind::Read) CHECK(event.pin == 6);
        else CHECK((event.config.pin_bit_mask == (1ULL << 7) && event.config.mode == GPIO_MODE_OUTPUT) ||
                   (event.config.pin_bit_mask == (1ULL << 6) && event.config.mode == GPIO_MODE_INPUT));
    }
}
} // namespace

esp_err_t gpio_set_level(gpio_num_t pin, std::uint32_t level) {
    record({Kind::Set, pin, level, {}}); return writeResult();
}
esp_err_t gpio_config(const gpio_config_t* config) {
    record({Kind::Config, -1, 99, *config}); return writeResult();
}
int gpio_get_level(gpio_num_t pin) { record({Kind::Read, pin, 99, {}}); return sensedLevel; }
int xSemaphoreTake(SemaphoreHandle_t, TickType_t) { ++semaphoreCalls; return pdTRUE; }
int xSemaphoreGive(SemaphoreHandle_t) { ++semaphoreCalls; return pdTRUE; }

int main(int argc, char** argv) {
    using namespace digivice::board;
    if (argc != 2) return 2;
    failWrite = static_cast<unsigned>(std::strtoul(argv[1], nullptr, 10));
    if (failWrite > 4) return 2;
    bool pressed = true;
    CHECK(!powerControlAvailable() && !powerHoldReady() && !powerHoldAsserted());
#if CONFIG_DIGIVICE_BOARD_WAVESHARE_146
    CHECK(selectedProfile().id == ProfileId::Waveshare146);
    CHECK(readPowerPressed(pressed) == ESP_ERR_INVALID_STATE && pressed);
    CHECK(setPowerHold(false) == ESP_ERR_INVALID_STATE && count == 0);
    checkCapabilities(initialize());
    CHECK(count == (failWrite ? failWrite : 4));
    CHECK(events[0].kind == Kind::Set && events[0].pin == 7 && events[0].level == 1);
    if (count >= 2) checkConfig(events[1], 7, GPIO_MODE_OUTPUT);
    if (count >= 3) CHECK(events[2].kind == Kind::Set && events[2].pin == 7 && events[2].level == 1);
    if (count >= 4) checkConfig(events[3], 6, GPIO_MODE_INPUT);
    const auto afterInit = count;
    checkCapabilities(initialize()); CHECK(count == afterInit); // No reset or low pulse on reentry.
    if (failWrite) {
        CHECK(!powerControlAvailable());
        CHECK(powerHoldReady() == (failWrite == 4));
        CHECK(powerHoldStatus() == (failWrite == 4 ? ESP_OK : ESP_FAIL));
        CHECK(powerHoldAsserted() == (failWrite == 4));
        CHECK(readPowerPressed(pressed) == ESP_ERR_INVALID_STATE && pressed);
        CHECK(setPowerHold(false) == ESP_ERR_INVALID_STATE);
        CHECK(setPowerHold(true) == ESP_ERR_INVALID_STATE);
        CHECK(count == afterInit); // Failed startup never reads or cuts the latch.
    } else {
        CHECK(powerControlAvailable() && powerHoldReady() && powerHoldAsserted());
        CHECK(powerHoldStatus() == ESP_OK);
        sensedLevel = 1; CHECK(readPowerPressed(pressed) == ESP_OK && !pressed);
        sensedLevel = 0; CHECK(readPowerPressed(pressed) == ESP_OK && pressed);
        CHECK(setPowerHold(false) == ESP_OK && !powerHoldAsserted());
        CHECK(events[count - 1].kind == Kind::Set && events[count - 1].level == 0);
        failWrite = writes + 1;
        CHECK(setPowerHold(true) == ESP_FAIL && !powerHoldAsserted());
        failWrite = 0;
        CHECK(setPowerHold(true) == ESP_OK && powerHoldAsserted());
        failWrite = writes + 1;
        CHECK(setPowerHold(false) == ESP_FAIL && powerHoldAsserted());
        failWrite = 0;
        CHECK(setPowerHold(true) == ESP_OK && powerHoldAsserted());
        const auto beforeReentry = count;
        initialize(); CHECK(count == beforeReentry);
    }
#else
    CHECK(selectedProfile().id == ProfileId::GenericSerial);
    CHECK(readPowerPressed(pressed) == ESP_ERR_NOT_SUPPORTED && pressed);
    CHECK(setPowerHold(false) == ESP_ERR_NOT_SUPPORTED && count == 0);
    checkCapabilities(initialize()); checkCapabilities(initialize());
    CHECK(!powerControlAvailable() && !powerHoldReady() && !powerHoldAsserted());
    CHECK(powerHoldStatus() == ESP_ERR_NOT_SUPPORTED);
    CHECK(readPowerPressed(pressed) == ESP_ERR_NOT_SUPPORTED && pressed);
    CHECK(setPowerHold(true) == ESP_ERR_NOT_SUPPORTED);
    CHECK(setPowerHold(false) == ESP_ERR_NOT_SUPPORTED);
    CHECK(count == 0 && writes == 0); // Generic leaves ALL pins untouched.
#endif
    CHECK(sharedI2cBus() == nullptr && sharedI2cStatus() == ESP_ERR_NOT_SUPPORTED);
    CHECK(lockSharedI2c() == ESP_ERR_INVALID_STATE);
    unlockSharedI2c(); CHECK(semaphoreCalls == 0);
    CHECK(prepareSdCardSelect() == ESP_ERR_NOT_SUPPORTED);
    checkOnlyPowerPins();
    std::printf("Power HAL board=%s startupFault=%s: %u checks, %u failures. GPIO call model only.\n",
                CONFIG_DIGIVICE_BOARD_WAVESHARE_146 ? "Waveshare146" : "generic", argv[1], checks, failures);
    return failures ? 1 : 0;
}
