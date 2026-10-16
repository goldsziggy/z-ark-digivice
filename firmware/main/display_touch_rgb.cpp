// Waveshare flat ESP32-S3-Touch-LCD-2.1 SKU28169, independent of SPD2010.
#include "sdkconfig.h"
#if defined(CONFIG_DIGIVICE_BOARD_WAVESHARE_21) && CONFIG_DIGIVICE_BOARD_WAVESHARE_21 && defined(CONFIG_DIGIVICE_DISPLAY_TOUCH) && CONFIG_DIGIVICE_DISPLAY_TOUCH
#include "display_touch.hpp"
#include "board_hal.hpp"
#include "touch_cst820.hpp"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <algorithm>
#include <cstring>

namespace digivice::display {
namespace {
constexpr auto& pins = board::kWaveshare21;
constexpr int side = 480;
constexpr std::size_t frameBytes = side * side * sizeof(std::uint16_t);
Status current{};
bool initialized = false, lightReady = false, transferUncertain = false;
#if defined(CONFIG_DIGIVICE_DISPLAY_ORIENTATION_CW90) && CONFIG_DIGIVICE_DISPLAY_ORIENTATION_CW90
Orientation orientation = Orientation::Cw90;
#elif defined(CONFIG_DIGIVICE_DISPLAY_ORIENTATION_CCW90) && CONFIG_DIGIVICE_DISPLAY_ORIENTATION_CCW90
Orientation orientation = Orientation::Ccw90;
#else
Orientation orientation = Orientation::Native;
#endif
esp_lcd_panel_handle_t panel = nullptr;
i2c_master_dev_handle_t touch = nullptr;
std::uint16_t* frames[2]{};
unsigned shown = 0;
StaticSemaphore_t vsyncStorage{};
SemaphoreHandle_t vsync = nullptr;

// Each entry is one nine-bit command/data word, or a delay in milliseconds.
struct InitWord { bool data; std::uint16_t value; std::uint16_t delayMs; };
#include "st7701s_21_init.inc"

bool IRAM_ATTR frameBoundary(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t*, void*) {
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(vsync, &wake);
    return wake == pdTRUE;
}
esp_err_t light(unsigned percent) {
    if (!lightReady) return ESP_ERR_INVALID_STATE;
    auto e = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 8191U * percent / 100U);
    if (e == ESP_OK) e = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    return e;
}
esp_err_t initializeLight() {
    auto e = gpio_set_level(static_cast<gpio_num_t>(pins.lcdBacklightGpio), 0);
    gpio_config_t gpio{}; gpio.pin_bit_mask = 1ULL << pins.lcdBacklightGpio; gpio.mode = GPIO_MODE_OUTPUT;
    if (e == ESP_OK) e = gpio_config(&gpio);
    ledc_timer_config_t timer{};
    timer.speed_mode = LEDC_LOW_SPEED_MODE; timer.duty_resolution = LEDC_TIMER_13_BIT;
    timer.timer_num = LEDC_TIMER_0; timer.freq_hz = 5000; timer.clk_cfg = LEDC_AUTO_CLK;
    if (e == ESP_OK) e = ledc_timer_config(&timer);
    ledc_channel_config_t channel{};
    channel.gpio_num = pins.lcdBacklightGpio; channel.speed_mode = LEDC_LOW_SPEED_MODE;
    channel.channel = LEDC_CHANNEL_0; channel.timer_sel = LEDC_TIMER_0;
    if (e == ESP_OK) e = ledc_channel_config(&channel);
    lightReady = e == ESP_OK;
    return e;
}
esp_err_t initializeCommands() {
    auto e = board::prepareDisplayCommandPins();
    if (e != ESP_OK) return e;
    e = board::setPeripheralReset(board::PeripheralReset::Display, true);
    if (e == ESP_OK) { vTaskDelay(pdMS_TO_TICKS(10)); e = board::setPeripheralReset(board::PeripheralReset::Display, false); }
    if (e == ESP_OK) vTaskDelay(pdMS_TO_TICKS(50));
    spi_bus_config_t bus{};
    bus.mosi_io_num = pins.lcdCommandDataGpio; bus.sclk_io_num = pins.lcdClockGpio;
    bus.miso_io_num = bus.quadwp_io_num = bus.quadhd_io_num = -1;
    bus.data4_io_num = bus.data5_io_num = bus.data6_io_num = bus.data7_io_num = -1;
    bool ownsBus = false;
    spi_device_handle_t device = nullptr;
    if (e == ESP_OK) { e = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_DISABLED); ownsBus = e == ESP_OK; }
    spi_device_interface_config_t config{};
    config.command_bits = 1; config.address_bits = 8; config.clock_speed_hz = 4000000;
    config.spics_io_num = -1; config.queue_size = 1;
    if (e == ESP_OK) e = spi_bus_add_device(SPI2_HOST, &config, &device);
    if (e == ESP_OK) e = board::setDisplaySelected(true);
    if (e == ESP_OK) vTaskDelay(pdMS_TO_TICKS(100));
    for (const auto& word : initWords) {
        if (e != ESP_OK) break;
        if (word.delayMs) { vTaskDelay(pdMS_TO_TICKS(word.delayMs)); continue; }
        spi_transaction_t transaction{};
        transaction.cmd = word.data ? 1 : 0; transaction.addr = word.value;
        e = spi_device_transmit(device, &transaction);
    }
    // Cleanup on every path. Never advertise SD ownership after any uncertain
    // deassert/remove/free. The panel is never reset or commanded after this.
    const auto deselect = board::setDisplaySelected(false);
    const auto remove = device ? spi_bus_remove_device(device) : ESP_OK;
    const auto freed = ownsBus && remove == ESP_OK ? spi_bus_free(SPI2_HOST) : ownsBus ? remove : ESP_OK;
    if (deselect != ESP_OK) return deselect;
    if (remove != ESP_OK) return remove;
    if (freed != ESP_OK) return freed;
    // If another owner already had SPI2, leave SD disabled (no ownership proof).
    if (!ownsBus) return e == ESP_OK ? ESP_ERR_INVALID_STATE : e;
    const auto release = board::releaseDisplayCommandPins();
    return e != ESP_OK ? e : release;
}
esp_err_t initializePanel() {
    auto e = initializeLight();
    if (e == ESP_OK) e = initializeCommands();
    if (e != ESP_OK) return e;
    vsync = xSemaphoreCreateBinaryStatic(&vsyncStorage);
    if (!vsync) return ESP_ERR_NO_MEM;
    esp_lcd_rgb_panel_config_t config{};
    config.clk_src = LCD_CLK_SRC_DEFAULT; config.data_width = 16; config.bits_per_pixel = 16;
    config.num_fbs = 2; config.dma_burst_size = 64;
    // SRAM bounce buffers decouple LCD DMA from CPU/flash traffic on PSRAM.
    // 480 rows is an even multiple of 20 rows, as required by IDF 5.3.6.
    // Keep both full framebuffers and the existing two-VSYNC ownership fence.
    config.bounce_buffer_size_px = 20 * side;
    config.disp_gpio_num = -1; config.pclk_gpio_num = pins.lcdPixelClockGpio;
    config.vsync_gpio_num = pins.lcdVsyncGpio; config.hsync_gpio_num = pins.lcdHsyncGpio;
    config.de_gpio_num = pins.lcdDataEnableGpio;
    for (unsigned i = 0; i < 16; ++i) config.data_gpio_nums[i] = pins.lcdRgbDataGpios[i];
    // Retain the vendor porches/polarity; lower bus demand from 36 to 24 MB/s.
    config.timings.pclk_hz = 12000000; config.timings.h_res = config.timings.v_res = side;
    config.timings.hsync_back_porch = 10; config.timings.hsync_front_porch = 50; config.timings.hsync_pulse_width = 8;
    config.timings.vsync_back_porch = 8; config.timings.vsync_front_porch = 8; config.timings.vsync_pulse_width = 3;
    config.flags.fb_in_psram = true;
    e = esp_lcd_new_rgb_panel(&config, &panel);
    void* buffers[2]{};
    if (e == ESP_OK) e = esp_lcd_rgb_panel_get_frame_buffer(panel, 2, &buffers[0], &buffers[1]);
    frames[0] = static_cast<std::uint16_t*>(buffers[0]); frames[1] = static_cast<std::uint16_t*>(buffers[1]);
    if (e != ESP_OK) return e;
    std::memset(frames[0], 0, frameBytes); std::memset(frames[1], 0, frameBytes);
    esp_lcd_rgb_panel_event_callbacks_t callbacks{}; callbacks.on_vsync = frameBoundary;
    e = esp_lcd_rgb_panel_register_event_callbacks(panel, &callbacks, nullptr);
    if (e == ESP_OK) e = esp_lcd_panel_reset(panel);
    if (e == ESP_OK) e = esp_lcd_panel_init(panel);
    if (e == ESP_OK) e = esp_lcd_panel_draw_bitmap(panel, 0, 0, side, side, frames[0]);
    if (e == ESP_OK) e = light(40);
    return e;
}
esp_err_t initializeTouch() {
    gpio_config_t interrupt{};
    interrupt.pin_bit_mask = 1ULL << pins.touchInterruptGpio;
    interrupt.mode = GPIO_MODE_INPUT;
    interrupt.pull_up_en = GPIO_PULLUP_ENABLE;
    auto e = gpio_config(&interrupt);
    if (e == ESP_OK) e = board::setPeripheralReset(board::PeripheralReset::Touch, true);
    if (e == ESP_OK) { vTaskDelay(pdMS_TO_TICKS(10)); e = board::setPeripheralReset(board::PeripheralReset::Touch, false); }
    if (e != ESP_OK) return e;
    vTaskDelay(pdMS_TO_TICKS(100));
    e = board::lockSharedI2c();
    if (e != ESP_OK) return e;
    i2c_device_config_t config{};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7; config.device_address = pins.touchI2cAddress; config.scl_speed_hz = 400000;
    e = i2c_master_bus_add_device(board::sharedI2cBus(), &config, &touch);
    // Waveshare's CST820_AutoSleep(false): keep I2C awake for continuous
    // polling, including neutral/release reports and screen-only wake.
    // Without this the controller NACKs after startup until a new touch.
    const std::uint8_t disableAutoSleep[]{0xfe, 0xff};
    if (e == ESP_OK) e = i2c_master_transmit(touch, disableAutoSleep, sizeof(disableAutoSleep), 20);
    // Only confirm an answering controller. Vendor has no specified identity
    // constant at its so-called CHIP_ID register; never invent one.
    std::uint8_t reg = 0x02, packet[7]{};
    if (e == ESP_OK) e = i2c_master_transmit_receive(touch, &reg, 1, packet, sizeof(packet), 20);
    board::unlockSharedI2c();
    return e;
}
}
Status initialize() {
    if (initialized) return current;
    initialized = true;
    if (!board::sharedI2cBus()) { current.display = current.touch = ESP_ERR_INVALID_STATE; return current; }
    current.display = initializePanel(); current.touch = initializeTouch();
    if (current.display != ESP_OK && lightReady) (void)light(0);
    return current;
}
const Status& status() { return current; }
Orientation configuredOrientation() { return orientation; }
bool displayReady() { return current.display == ESP_OK && !current.suspended; }
bool touchReady() { return current.touch == ESP_OK && !current.suspended; }
bool quiescent() { return !transferUncertain; }
esp_err_t setOrientation(Orientation value) {
    if (!validOrientation(value)) return ESP_ERR_INVALID_ARG;
    if (!quiescent()) return ESP_ERR_INVALID_STATE;
    orientation = value; return ESP_OK;
}
esp_err_t flushRgb565(int x, int y, int width, int height, const std::uint16_t* pixels,
                     std::size_t stride, StripeHook hook, void* context) {
    const Rect logical{x,y,width,height};
    if (!displayReady() || !quiescent()) return ESP_ERR_INVALID_STATE;
    if (!pixels || !validRect(logical) || stride < static_cast<std::size_t>(width) || stride > kWidth) return ESP_ERR_INVALID_ARG;
    const unsigned next = 1 - shown;
    // Preserve pixels outside partial capture updates. The previous transfer
    // waited through two boundaries, including IDF's possible extra old frame.
    // A full logical frame overwrites every physical pixel. Avoid an extra
    // 921,600-byte PSRAM read/write burst while the LCD is scanning out.
    if (x != 0 || y != 0 || width != kWidth || height != kHeight)
        std::memcpy(frames[next], frames[shown], frameBytes);
    const auto physical = logicalRectToPhysical(orientation, logical, side);
    for (int row = 0; row < physical.height; row += kStripeRows) {
        const int rows = std::min(kStripeRows, physical.height-row);
        if (!blitPhysicalRows(orientation, logical, pixels, stride, side, row, rows, frames[next])) return ESP_ERR_INVALID_ARG;
        if (hook) hook(context);
    }
    // In bounce mode IDF reads through DCache and selects the next framebuffer
    // at a complete-frame boundary. Keep buffers owned through two VSYNCs.
    auto e = esp_lcd_panel_draw_bitmap(panel, 0, 0, side, side, frames[next]);
    transferUncertain = true;
    (void)xSemaphoreTake(vsync, 0);
    for (unsigned fence = 0; e == ESP_OK && fence < 2; ++fence)
        if (xSemaphoreTake(vsync, pdMS_TO_TICKS(100)) != pdTRUE) e = ESP_ERR_TIMEOUT;
    if (e != ESP_OK) { current.display = e; (void)light(0); return e; }
    shown = next; transferUncertain = false; ++current.frames; return ESP_OK;
}
esp_err_t pollTouch(TouchPoint& point) {
    point = {};
    if (!touchReady()) return ESP_ERR_INVALID_STATE;
    auto e = board::lockSharedI2c(20);
    std::uint8_t reg = 0x02, packet[7]{};
    if (e == ESP_OK) {
        e = i2c_master_transmit_receive(touch, &reg, 1, packet, sizeof(packet), 20);
        board::unlockSharedI2c();
    } else if (e == ESP_ERR_TIMEOUT) ++current.touchLockMisses;
    if (e == ESP_OK && !decodeCst820(packet, orientation, point)) e = ESP_ERR_INVALID_RESPONSE;
    current.lastTouchError = e;
    if (e != ESP_OK) { point = {}; ++current.touchErrors; return e; }
    ++current.touchSamples; return ESP_OK;
}
esp_err_t setIdleBlank(bool blanked) {
    if (!displayReady()) return ESP_ERR_INVALID_STATE;
    if (!quiescent()) return ESP_ERR_TIMEOUT;
    const auto e = light(blanked ? 0 : 40);
    if (e == ESP_OK) current.idleBlanked = blanked;
    return e;
}
esp_err_t setSuspended(bool suspended) {
    // RGB continues refreshing; only the backlight and touch are suspended.
    // No ST7701 command is ever sent after SD owns GPIO1/2.
    auto e = light(suspended || current.idleBlanked ? 0 : 40);
    current.suspended = suspended;
    return !quiescent() ? ESP_ERR_TIMEOUT : e;
}
} // namespace digivice::display
#endif
