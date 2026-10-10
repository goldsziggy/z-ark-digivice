#include "display_touch.hpp"
#include "board_hal.hpp"
#include "sdkconfig.h"

#include <algorithm>
#include <cstring>
#include <iterator>

#if defined(CONFIG_DIGIVICE_BOARD_WAVESHARE_146) && CONFIG_DIGIVICE_BOARD_WAVESHARE_146 && \
    defined(CONFIG_DIGIVICE_DISPLAY_TOUCH) && CONFIG_DIGIVICE_DISPLAY_TOUCH
#define DIGIVICE_PANEL_ENABLED 1
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "third_party/spd2010/esp_lcd_spd2010.h"
#else
#define DIGIVICE_PANEL_ENABLED 0
#endif

namespace digivice::display {
namespace {
Status current{};
bool initialized = false;
#if defined(CONFIG_DIGIVICE_DISPLAY_ORIENTATION_CW90) && CONFIG_DIGIVICE_DISPLAY_ORIENTATION_CW90
constexpr Orientation kOrientation = Orientation::Cw90;
#elif defined(CONFIG_DIGIVICE_DISPLAY_ORIENTATION_CCW90) && CONFIG_DIGIVICE_DISPLAY_ORIENTATION_CCW90
constexpr Orientation kOrientation = Orientation::Ccw90;
#else
constexpr Orientation kOrientation = Orientation::Native;
#endif

#if DIGIVICE_PANEL_ENABLED
const auto& pins = board::kWaveshare146;
esp_lcd_panel_io_handle_t panelIo = nullptr;
esp_lcd_panel_handle_t panel = nullptr;
i2c_master_dev_handle_t touchDevice = nullptr;
StaticSemaphore_t completionStorage{};
SemaphoreHandle_t completion = nullptr;
std::uint8_t* dmaStripe = nullptr;
bool backlightReady = false;
bool panelSuspended = false;
portMUX_TYPE dmaStateLock = portMUX_INITIALIZER_UNLOCKED;
bool dmaInFlight = false;
TouchPoint previousTouch{};
constexpr int kTouchDeadlineUs = 16000;
constexpr std::size_t kTouchPacketBytes = 64;

bool colorComplete(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*) {
    BaseType_t wake = pdFALSE;
    portENTER_CRITICAL_ISR(&dmaStateLock);
    dmaInFlight = false;
    portEXIT_CRITICAL_ISR(&dmaStateLock);
    xSemaphoreGiveFromISR(completion, &wake);
    // IDF 5.3.6's SPI panel callback discards our return value. Request the
    // ISR-exit yield here so each stripe does not wait for the next 10ms tick.
    if (wake == pdTRUE) portYIELD_FROM_ISR();
    return wake == pdTRUE;
}

esp_err_t backlight(std::uint32_t percent) {
    if (!backlightReady) return ESP_ERR_INVALID_STATE;
    auto error = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (8191U * percent) / 100U);
    if (error == ESP_OK) error = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    return error;
}

// The single stripe remains owned until its completion callback, even after a
// timeout. This submission path is shared by drawing and the raw zero boot clear.
esp_err_t submitStripe(Rect native) {
    // A previous stripe can complete after its wait timed out. Drop that stale
    // signal so this transfer waits for its own callback, and do not mark the
    // panel dead: one late stripe must not blank the screen for the rest of boot.
    for (unsigned i = 0; i < 4 && xSemaphoreTake(completion, 0) == pdTRUE; ++i) {}
    portENTER_CRITICAL(&dmaStateLock);
    dmaInFlight = true;
    portEXIT_CRITICAL(&dmaStateLock);
    auto error = esp_lcd_panel_draw_bitmap(panel, native.x, native.y,
        native.x + native.width, native.y + native.height, dmaStripe);
    if (error == ESP_OK && xSemaphoreTake(completion, pdMS_TO_TICKS(100)) != pdTRUE)
        error = ESP_ERR_TIMEOUT;
    if (error != ESP_OK && error != ESP_ERR_TIMEOUT) {
        current.display = error;
        (void)backlight(0);
    }
    return error;
}

esp_err_t initializeBacklight() {
    // Low before direction/PWM prevents an uninitialized bright panel.
    auto error = gpio_set_level(static_cast<gpio_num_t>(pins.lcdBacklightGpio), 0);
    gpio_config_t gpio{};
    gpio.pin_bit_mask = 1ULL << pins.lcdBacklightGpio;
    gpio.mode = GPIO_MODE_OUTPUT;
    if (error == ESP_OK) error = gpio_config(&gpio);
    ledc_timer_config_t timer{};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution = LEDC_TIMER_13_BIT;
    timer.timer_num = LEDC_TIMER_0;
    timer.freq_hz = 5000;
    timer.clk_cfg = LEDC_AUTO_CLK;
    if (error == ESP_OK) error = ledc_timer_config(&timer);
    ledc_channel_config_t channel{};
    channel.gpio_num = pins.lcdBacklightGpio;
    channel.speed_mode = LEDC_LOW_SPEED_MODE;
    channel.channel = LEDC_CHANNEL_0;
    channel.intr_type = LEDC_INTR_DISABLE;
    channel.timer_sel = LEDC_TIMER_0;
    channel.duty = 0;
    if (error == ESP_OK) error = ledc_channel_config(&channel);
    backlightReady = error == ESP_OK;
    return error;
}

esp_err_t reset(board::PeripheralReset peripheral, std::uint32_t delayMs) {
    auto error = board::setPeripheralReset(peripheral, true);
    if (error != ESP_OK) return error;
    vTaskDelay(pdMS_TO_TICKS(delayMs));
    error = board::setPeripheralReset(peripheral, false);
    if (error == ESP_OK) vTaskDelay(pdMS_TO_TICKS(delayMs));
    return error;
}

esp_err_t initializePanel() {
    auto error = initializeBacklight();
    if (error != ESP_OK) return error;
    completion = xSemaphoreCreateBinaryStatic(&completionStorage);
    if (!completion) return ESP_ERR_NO_MEM;
    dmaStripe = static_cast<std::uint8_t*>(heap_caps_malloc(kDmaStripeBytes,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
    if (!dmaStripe) return ESP_ERR_NO_MEM;
    error = reset(board::PeripheralReset::Display, 100);
    if (error != ESP_OK) return error;
    spi_bus_config_t bus{};
    bus.sclk_io_num = pins.lcdClockGpio;
    bus.data0_io_num = pins.lcdDataGpios[0];
    bus.data1_io_num = pins.lcdDataGpios[1];
    bus.data2_io_num = pins.lcdDataGpios[2];
    bus.data3_io_num = pins.lcdDataGpios[3];
    bus.data4_io_num = bus.data5_io_num = bus.data6_io_num = bus.data7_io_num = -1;
    bus.max_transfer_sz = kDmaStripeBytes;
    bus.flags = SPICOMMON_BUSFLAG_MASTER;
    error = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (error != ESP_OK) return error;
    esp_lcd_panel_io_spi_config_t io{};
    io.cs_gpio_num = pins.lcdSelectGpio;
    io.dc_gpio_num = -1;
    io.spi_mode = 0; // Vendor Display_SPD2010.c, not the generic header macro.
    io.pclk_hz = 40 * 1000 * 1000; // Conservative first port; vendor uses 80MHz.
    io.trans_queue_depth = 1;
    io.on_color_trans_done = colorComplete;
    io.lcd_cmd_bits = 32;
    io.lcd_param_bits = 8;
    io.flags.quad_mode = true;
    error = esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(SPI2_HOST), &io, &panelIo);
    if (error != ESP_OK) return error;
    spd2010_vendor_config_t vendor{};
    vendor.flags.use_qspi_interface = true;
    esp_lcd_panel_dev_config_t device{};
    device.reset_gpio_num = -1; // Board-owned EXIO2 reset above, never ESP GPIO2.
    device.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    device.bits_per_pixel = 16;
    device.vendor_config = &vendor;
    error = esp_lcd_new_panel_spd2010(panelIo, &device, &panel);
    if (error == ESP_OK) error = esp_lcd_panel_reset(panel);
    if (error == ESP_OK) error = esp_lcd_panel_init(panel);
    if (error == ESP_OK) error = esp_lcd_panel_disp_on_off(panel, true);
    return error;
}

// Independent adapter for the pinned vendor's SPD2010 wire protocol. All
// request and response arrays are fixed-size, all SDK errors are propagated.
// The 16 ms deadline starts after acquiring the bus; mutex contention has its
// own bounded 20 ms allowance (two ticks at the configured 100 Hz).
struct TouchBus {
    std::int64_t deadline = esp_timer_get_time() + kTouchDeadlineUs;
    int remainingMs() const {
        const auto us = deadline - esp_timer_get_time();
        // IDF floors milliseconds to ticks. Never start a transfer with a
        // zero-tick completion wait or extend this group's deadline to round up.
        const int ms = us > 0 ? static_cast<int>(us / 1000) : 0;
        return ms >= portTICK_PERIOD_MS ? ms : 0;
    }
    esp_err_t read(std::uint16_t address, std::uint8_t* data, std::size_t size) const {
        if (!data || size == 0 || size > kTouchPacketBytes) return ESP_ERR_INVALID_SIZE;
        const int timeout = remainingMs();
        if (!timeout) return ESP_ERR_TIMEOUT;
        const std::uint8_t bytes[] = {static_cast<std::uint8_t>(address >> 8), static_cast<std::uint8_t>(address)};
        const auto error = i2c_master_transmit_receive(touchDevice, bytes, 2, data, size, timeout);
        if (error == ESP_OK) esp_rom_delay_us(200);
        return error;
    }
    esp_err_t command(std::uint16_t address, std::uint16_t value = 0) const {
        const int timeout = remainingMs();
        if (!timeout) return ESP_ERR_TIMEOUT;
        const std::uint8_t bytes[] = {static_cast<std::uint8_t>(address >> 8), static_cast<std::uint8_t>(address),
                                     static_cast<std::uint8_t>(value), static_cast<std::uint8_t>(value >> 8)};
        const auto error = i2c_master_transmit(touchDevice, bytes, sizeof(bytes), timeout);
        if (error == ESP_OK) esp_rom_delay_us(200);
        return error;
    }
    esp_err_t clear() const { return command(0x0200, 1); }
};

esp_err_t initializeTouch() {
    auto error = reset(board::PeripheralReset::Touch, 50);
    if (error != ESP_OK) return error;
    error = board::lockSharedI2c();
    if (error != ESP_OK) return error;
    i2c_device_config_t config{};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = pins.touchI2cAddress;
    config.scl_speed_hz = 400000;
    error = i2c_master_bus_add_device(board::sharedI2cBus(), &config, &touchDevice);
    std::uint8_t version[18]{};
    if (error == ESP_OK) error = TouchBus{}.read(0x2600, version, sizeof(version));
    if (error == ESP_OK) {
        const bool zeros = std::all_of(std::begin(version), std::end(version), [](auto b) { return b == 0; });
        const bool ones = std::all_of(std::begin(version), std::end(version), [](auto b) { return b == 0xff; });
        if (zeros || ones) error = ESP_ERR_INVALID_RESPONSE;
    }
    board::unlockSharedI2c();
    return error;
}

esp_err_t readTouch(TouchPoint& point) {
    point.fresh = false;
    TouchBus bus;
    std::uint8_t state[4]{};
    auto error = bus.read(0x2000, state, sizeof(state));
    if (error != ESP_OK) return error;
    const auto length = static_cast<std::size_t>(state[2] | (state[3] << 8));
    if (state[1] & 0x80) return ESP_OK; // Busy is no new report, never neutral.
    if (state[1] & 0x40) { // BIOS -> CPU (vendor handshake, no flash update).
        point.pressed = false;
        error = bus.clear();
        if (error == ESP_OK) error = bus.command(0x0400, 1);
        // A controller restart cancels a held gesture; it is never touch Up.
        return error == ESP_OK ? ESP_ERR_INVALID_STATE : error;
    }
    if (state[1] & 0x20) { // CPU -> point mode -> start.
        point.pressed = false;
        error = bus.command(0x5000);
        if (error == ESP_OK) error = bus.command(0x4600);
        if (error == ESP_OK) error = bus.clear();
        return error == ESP_OK ? ESP_ERR_INVALID_STATE : error;
    }
    if ((state[1] & 0x08) && length == 0 && !(state[0] & 0x03)) {
        point.pressed = false;
        point.fresh = true;
        return bus.clear();
    }
    if (state[0] & 0x03) {
        if (length < 4 || length > kTouchPacketBytes) {
            error = bus.clear();
            return error == ESP_OK ? ESP_ERR_INVALID_SIZE : error;
        }
        std::uint8_t packet[kTouchPacketBytes]{};
        error = bus.read(0x0003, packet, length);
        if (error != ESP_OK) return error;
        point.pressed = false;
        // Decode exactly one contact. Malformed, gesture and multicontact
        // reports still drain/ack below, then cancel input rather than emit Up.
        esp_err_t reportError = ESP_ERR_INVALID_RESPONSE;
        if ((state[0] & 0x01) && length == 10 && packet[4] <= 0x0a) {
            const auto x = static_cast<std::uint16_t>(((packet[7] & 0xf0) << 4) | packet[5]);
            const auto y = static_cast<std::uint16_t>(((packet[7] & 0x0f) << 8) | packet[6]);
            if (x < kWidth && y < kHeight) {
                const auto logical = panelToLogical(kOrientation, {x, y});
                point = {packet[8] != 0, static_cast<std::uint16_t>(logical.x),
                         static_cast<std::uint16_t>(logical.y), true};
                reportError = ESP_OK;
            }
        }
        // Acknowledge complete HDP, draining at most eight bounded continuations.
        for (int attempt = 0; attempt < 8; ++attempt) {
            std::uint8_t hdp[8]{};
            error = bus.read(0xfc02, hdp, sizeof(hdp));
            if (error != ESP_OK) return error;
            if (hdp[5] == 0x82) {
                error = bus.clear();
                return error == ESP_OK ? reportError : error;
            }
            if (hdp[5] != 0) return ESP_ERR_INVALID_RESPONSE;
            const auto remaining = static_cast<std::size_t>(hdp[2] | (hdp[3] << 8));
            if (remaining > kTouchPacketBytes) return ESP_ERR_INVALID_SIZE;
            if (remaining) {
                error = bus.read(0x0003, packet, remaining);
                if (error != ESP_OK) return error;
            }
        }
        return ESP_ERR_TIMEOUT;
    }
    if ((state[1] & 0x08) && (state[0] & 0x08)) return bus.clear();
    // Busy/no new report: preserve the previous level, never invent a release.
    return ESP_OK;
}
#endif
} // namespace

Status initialize() {
    if (initialized) return current;
    initialized = true;
#if DIGIVICE_PANEL_ENABLED
    if (!board::powerHoldReady() || !board::sharedI2cBus()) {
        current.display = current.touch = ESP_ERR_INVALID_STATE;
        return current;
    }
    current.display = initializePanel();
    if (current.display == ESP_OK) {
        // Clear native GRAM directly before illumination. A uniform zero clear
        // needs no rotation and must not alias the packer's source/destination.
        std::memset(dmaStripe, 0, kDmaStripeBytes);
        for (int y = 0; y < kHeight && current.display == ESP_OK; y += kStripeRows) {
            current.display = submitStripe({0, y, kWidth, std::min(kStripeRows, kHeight - y)});
            if (current.display == ESP_OK) ++current.frames;
        }
        if (current.display == ESP_OK) current.display = backlight(40);
    }
    if (current.display != ESP_OK && backlightReady) (void)backlight(0);
    current.touch = initializeTouch();
#endif
    return current;
}

const Status& status() { return current; }
Orientation configuredOrientation() { return kOrientation; }
bool displayReady() { return current.display == ESP_OK && !current.suspended; }
bool touchReady() { return current.touch == ESP_OK && !current.suspended; }
bool quiescent() {
#if DIGIVICE_PANEL_ENABLED
    portENTER_CRITICAL(&dmaStateLock);
    const bool idle = !dmaInFlight;
    portEXIT_CRITICAL(&dmaStateLock);
    return idle;
#else
    return true;
#endif
}

esp_err_t flushRgb565(int x, int y, int width, int height,
                     const std::uint16_t* pixels, std::size_t stridePixels,
                     StripeHook betweenStripes, void* context) {
#if DIGIVICE_PANEL_ENABLED
    if (!displayReady() || !quiescent()) return ESP_ERR_INVALID_STATE;
    const Rect logical{x, y, width, height};
    if (!pixels || !validRect(logical) || stridePixels < static_cast<std::size_t>(width) ||
        stridePixels > static_cast<std::size_t>(kWidth)) return ESP_ERR_INVALID_ARG;
    const auto native = logicalRectToPanel(kOrientation, logical);
    const auto sourcePixels = static_cast<std::size_t>(height - 1) * stridePixels +
                              static_cast<std::size_t>(width);
    for (int row = 0; row < native.height; row += kStripeRows) {
        const int rows = std::min(kStripeRows, native.height - row);
        if (!packStripe(kOrientation, logical, pixels, sourcePixels, stridePixels,
                        row, rows, dmaStripe, kDmaStripeBytes)) return ESP_ERR_INVALID_ARG;
        const auto error = submitStripe({native.x, native.y + row, native.width, rows});
        if (error != ESP_OK) return error;
        if (betweenStripes && row + rows < native.height) betweenStripes(context);
    }
    ++current.frames;
    return ESP_OK;
#else
    (void)x; (void)y; (void)width; (void)height; (void)pixels; (void)stridePixels;
    (void)betweenStripes; (void)context;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t pollTouch(TouchPoint& point) {
    point.fresh = false;
#if DIGIVICE_PANEL_ENABLED
    if (!touchReady()) return ESP_ERR_INVALID_STATE;
    auto next = previousTouch;
    next.fresh = false;
    // The independent IMU worker briefly owns this bus while awaiting its
    // transfers. 2 ms floors to zero ticks at 100 Hz and would cancel ordinary
    // touch gestures on contention. Wait a bounded two ticks instead.
    auto error = board::lockSharedI2c(20);
    if (error == ESP_OK) {
        error = readTouch(next); // Fresh 16 ms transaction budget after acquisition.
        board::unlockSharedI2c();
    } else if (error == ESP_ERR_TIMEOUT) {
        ++current.touchLockMisses;
    }
    current.lastTouchError = error;
    if (error != ESP_OK) {
        ++current.touchErrors;
        previousTouch = {}; // Caller must cancel, not generate Up on this error.
        return error;
    }
    previousTouch = next;
    point = next;
    ++current.touchSamples;
    return ESP_OK;
#else
    (void)point;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t setIdleBlank(bool blanked) {
#if DIGIVICE_PANEL_ENABLED
    if (current.suspended || current.display != ESP_OK || !backlightReady) return ESP_ERR_INVALID_STATE;
    if (current.idleBlanked == blanked) return ESP_OK;
    if (!quiescent()) return ESP_ERR_TIMEOUT;
    const auto error = backlight(blanked ? 0 : 40);
    if (error == ESP_OK) current.idleBlanked = blanked;
    return error;
#else
    (void)blanked;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t setSuspended(bool suspended) {
#if DIGIVICE_PANEL_ENABLED
    if (current.suspended == suspended && panelSuspended == suspended && quiescent()) return ESP_OK;
    previousTouch = {};
    current.suspended = suspended;
    auto error = backlightReady ? backlight(0) : ESP_OK;
    // Panel commands may synchronously wait for the SPI queue. Never issue one
    // after an uncertain DMA; backlight off and input suspended are safe now.
    if (!quiescent()) return ESP_ERR_TIMEOUT;
    if (current.display == ESP_OK && panelSuspended != suspended) {
        const auto panelError = esp_lcd_panel_disp_on_off(panel, !suspended);
        if (panelError == ESP_OK) panelSuspended = suspended;
        if (error == ESP_OK) error = panelError;
        if (!suspended && error == ESP_OK) error = backlight(current.idleBlanked ? 0 : 40);
        if (error != ESP_OK) current.display = error;
    }
    return error;
#else
    (void)suspended;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

} // namespace digivice::display
