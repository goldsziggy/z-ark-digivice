#pragma once

#include "board_profiles.hpp"
#include "driver/i2c_master.h"
#include "esp_err.h"

namespace digivice::board {

// Capabilities describe usable game interfaces, not selected build options.
// Selecting a board or creating its shared bus does not prove a peripheral works.
struct Capabilities {
    bool display;
    bool touch;
    bool motionSteps;
    // Optional: initialize() explicitly leaves this false for both first units.
    // Only a future verified, initialized reader may advertise NFC readiness.
    bool nfc;
    bool gps;
    bool network;
    bool batteryTelemetry;
    bool chargingControl;
};

// Call once, promptly at app startup, before optional initialization/tasks.
// GenericSerial and HeltecUnverified touch no GPIOs. The explicitly selected
// Waveshare146 profile asserts only its verified power hold, then optionally
// creates one shared I2C bus for gated SD and/or read-only QMI bring-up.
Capabilities initialize();
const char* boardName();
const BoardProfile& selectedProfile();
bool powerHoldReady();
esp_err_t powerHoldStatus();
// Onboard PWR only. Generic/Heltec return NOT_SUPPORTED without touching GPIO.
// External pull-up R1 already exists; no game-button pin or USB-sense inference.
bool powerControlAvailable();
esp_err_t readPowerPressed(bool& pressed);
esp_err_t setPowerHold(bool asserted);
bool powerHoldAsserted();
// Getter only: never creates another bus. The board owns this application-life
// handle. Device drivers add/remove their devices, never delete/recreate the bus
// or install the vendor legacy I2C driver. Serialize bus calls in the owner task.
i2c_master_bus_handle_t sharedI2cBus();
esp_err_t sharedI2cStatus();
// All shared-bus users must take this bounded lock around complete transaction
// groups. The IDF5.3.6 device APIs are not safe across uncoordinated tasks.
esp_err_t lockSharedI2c(std::uint32_t timeoutMs = 100);
void unlockSharedI2c();
// Waveshare SD only: preload TCA9554 P2 high and make only P2 an output.
// Owns the expander handle and takes the shared-bus lock for full RMW/readback.
// Future expander writers must use the same board-owned lock/API; do not create
// another expander driver that writes whole output/configuration registers.
esp_err_t prepareSdCardSelect();

enum class PeripheralReset { Display, Touch };
// Changes only the selected EXIO1/P0 or EXIO2/P1 latch/direction bit.
// Active low; delays between assertion/release belong to the caller.
// Preserves SD EXIO3/P2 and every unrelated expander bit under the same lock.
esp_err_t setPeripheralReset(PeripheralReset peripheral, bool asserted);

} // namespace digivice::board
