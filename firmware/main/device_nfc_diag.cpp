#include "device_nfc_diag.hpp"
#include "board_hal.hpp"
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#ifndef DIGIVICE_NFC_BINDINGS_HEADER
#define DIGIVICE_NFC_BINDINGS_HEADER "nfc_uid_allowlist.hpp"
#endif
#include DIGIVICE_NFC_BINDINGS_HEADER
#include <cstdio>
#include <cstring>

namespace digivice::nfcdiag {
namespace {
constexpr uart_port_t kPort = UART_NUM_1;
constexpr gpio_num_t kTx = GPIO_NUM_43, kRx = GPIO_NUM_44;
constexpr bool enabled() {
#if defined(CONFIG_DIGIVICE_NFC_UART_DIAGNOSTICS) && CONFIG_DIGIVICE_NFC_UART_DIAGNOSTICS && \
    defined(CONFIG_DIGIVICE_BOARD_WAVESHARE_21) && CONFIG_DIGIVICE_BOARD_WAVESHARE_21 && \
    defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG) && CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG && \
    defined(CONFIG_ESP_CONSOLE_SECONDARY_NONE) && CONFIG_ESP_CONSOLE_SECONDARY_NONE
    return true;
#else
    return false;
#endif
}
}
bool Device::testAvailable() const { return enabled() && board::selectedProfile().id==board::ProfileId::Waveshare21; }
void Device::setTestEnabled(bool active, std::uint64_t now) {
    if(active==testActive_)return;
    testActive_=active; invalidate();
    if(active) { test_.begin(testAvailable()); testFault_=false; nextScan_=now; }
    else if(owner_==Owner::Test) { testCancelled_=true; session_.cancel(now); }
}
void Device::retryTest(std::uint64_t now) {
    if(!testActive_ || busy() || !testAvailable())return;
    testFault_=false; testCancelled_=false; test_.retry(); nextScan_=now;
}
bool Device::pollTest(std::uint64_t now, bool allowed) {
    if(owner_!=Owner::Test && !testActive_)return false;
    if(owner_==Owner::Cards || owner_==Owner::Diagnostic)return false;
    if(owner_==Owner::Test) {
        // The gameplay latch's 250ms freshness limit is not a transport timeout.
        // Session owns bounded ACK/response/overall deadlines during long frames.
        if(!testActive_ || !allowed) { testCancelled_=true; session_.cancel(now); }
        if(session_.busy())session_.poll(now);
        if(session_.busy())return true;
        if(testCancelled_) {
            test_.observe(session_.result(),now); test_.suspend();
            close(); owner_=Owner::None; testCancelled_=false; nextScan_=now+200; return true;
        }
        test_.observe(session_.result(),now);
        testFault_=!test_.view().connected;
        // Close after every bounded test scan; no idle UART/RF ownership.
        close(); owner_=Owner::None; nextScan_=now+200;
        status(); return true;
    }
    if(!allowed) { if(test_.view().available)test_.suspend(); return true; }
    if(testFault_ || !testAvailable() || now<nextScan_)return true;
    if(!open()) { test_.fault(esp_err_to_name(error_)); testFault_=true; return true; }
    if(!session_.start(true,now)) { test_.fault(session_.result().error); testFault_=true; close(); return true; }
    test_.scanning(); owner_=Owner::Test; return true;
}
bool Device::cardsConfigured() const {
#if defined(CONFIG_DIGIVICE_NFC_UART_GAMEPLAY) && CONFIG_DIGIVICE_NFC_UART_GAMEPLAY
    return enabled() && nfccards::config::count && nfccards::valid(nfccards::config::bindings,nfccards::config::count);
#else
    return false;
#endif
}
bool Device::open() {
    // No constructor/boot-time UART, GPIO, reset, I2C, flash or SD operations.
    if (!enabled() || board::selectedProfile().id != board::ProfileId::Waveshare21) {
        error_ = ESP_ERR_NOT_SUPPORTED; return false;
    }
    if (installed_ || uart_is_driver_installed(kPort)) { error_ = ESP_ERR_INVALID_STATE; return false; }
    transport_={};seenService_=false;
    uart_config_t config{};
    config.baud_rate = 115200; config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE; config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE; config.source_clk = UART_SCLK_DEFAULT;
    error_ = uart_param_config(kPort, &config);
    if (error_ != ESP_OK) return false;
    error_ = uart_driver_install(kPort, 256, 0, 8, &events_, 0);
    if (error_ != ESP_OK) return false;
    installed_ = true;
    // TX buffer is disabled: uart_tx_chars writes only available FIFO bytes.
    pins_ = true; // Release both verified spare pins even if set_pin partially fails.
    error_ = uart_set_pin(kPort, kTx, kRx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (error_ != ESP_OK) { close(); return false; }
    return true;
}
void Device::close() {
    if(installed_)discardInput();
    if (installed_) { (void)uart_driver_delete(kPort); installed_ = false; events_ = nullptr; }
    if (pins_) { (void)gpio_reset_pin(kTx); (void)gpio_reset_pin(kRx); pins_ = false; }
}
bool Device::flush() {
    if (!installed_) return false;
    discardInput();
    const auto error = uart_flush_input(kPort);
    if (error != ESP_OK) error_ = error; // Retain the first I/O failure through successful cleanup.
    if (events_) xQueueReset(events_);
    return error == ESP_OK;
}
int Device::write(const std::uint8_t* bytes, std::size_t size) {
    const int count = installed_ ? uart_tx_chars(kPort, reinterpret_cast<const char*>(bytes), static_cast<std::uint32_t>(size)) : -1;
    if (count < 0) error_ = ESP_FAIL;
    if(count>0 && static_cast<std::size_t>(count)<=size) {
        transport_.txBytes+=static_cast<unsigned>(count);
        for(int i=0;i<count && transport_.txSize<sizeof(transport_.tx);++i)transport_.tx[transport_.txSize++]=bytes[i];
    }
    return count;
}
bool Device::readEvents() {
    bool ok=true;
    uart_event_t event{};
    for (unsigned i = 0; i < 8 && xQueueReceive(events_, &event, 0) == pdTRUE; ++i) {
        if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL ||
            event.type == UART_FRAME_ERR || event.type == UART_PARITY_ERR || event.type == UART_BREAK) {
            ++transport_.uartErrors;
            transport_.uartErrorMask|=1u<<static_cast<unsigned>(event.type);
            error_ = ESP_FAIL; ok=false;
        }
    }
    return ok;
}
void Device::discardInput() {
    (void)readEvents();
    // Bound work even with a continuously noisy input. Keep the first 64 bytes
    // and a count; indicate when the 256-byte drain bound was exhausted.
    for(unsigned i=0;i<4;++i) {
        std::uint8_t bytes[64];const int count=uart_read_bytes(kPort,bytes,sizeof(bytes),0);
        if(count<0 || count>64) {error_=ESP_FAIL;return;}
        transport_.discardedBytes+=static_cast<unsigned>(count);
        for(int j=0;j<count && transport_.discardedSize<sizeof(transport_.discarded);++j)
            transport_.discarded[transport_.discardedSize++]=bytes[j];
        if(count<64)return;
    }
    transport_.discardLimit=true;
}
int Device::read(std::uint8_t* bytes, std::size_t size) {
    if (!installed_) return -1;
    if(!readEvents())return -1;
    const int count = uart_read_bytes(kPort, bytes, static_cast<std::uint32_t>(size), 0);
    if (count < 0) error_ = ESP_FAIL;
    if(count>0 && static_cast<std::size_t>(count)<=size)
        for(int i=0;i<count && transport_.rxSize<sizeof(transport_.rx);++i)transport_.rx[transport_.rxSize++]=bytes[i];
    return count;
}
void Device::status() const {
    const auto& r = session_.result();
    std::printf("NFC status diagnostic=%d busy=%d transport=uart1 tx=43 rx=44 baud=115200 mode=%s error=%s io=%s sam_ready=%d firmware_valid=%d ic=%02x version=%u.%u support=%02x ack_count=%u rx_bytes=%u rf_off=%s uid_valid=%d no_card=%d uid=",
        enabled(), busy(), r.scan ? "scan" : "probe", r.error, esp_err_to_name(error_), r.samReady, r.firmwareValid,
        r.firmware[0], r.firmware[1], r.firmware[2], r.firmware[3], r.ackCount, r.rxBytes,
        r.rfOff ? "confirmed" : "unknown", r.uidValid, r.noCard);
    if (r.uidValid) for (unsigned i = 0; i < r.uidSize; ++i) std::printf("%02x", r.uid[i]);
    else std::putchar('-');
    std::printf(" gameplay=%s bindings=%u owner=%s paused=%d\n",
        cardsConfigured()?"configured":"unconfigured",static_cast<unsigned>(nfccards::config::count),
        owner_==Owner::Cards?"cards":owner_==Owner::Diagnostic?"diagnostic":owner_==Owner::Test?"test":"none",stopped_);
    const auto& t=test_.view();
    std::printf("NFC transport tx_bytes=%u discarded_bytes=%u discard_limit=%d uart_errors=%u uart_error_mask=%x max_poll_gap_ms=%llu failed_command=%02x failed_phase=%s cleanup_error=%s framing=8N1 flow=none console=nativeUSB tx_trace=",
        transport_.txBytes,transport_.discardedBytes,transport_.discardLimit,transport_.uartErrors,transport_.uartErrorMask,
        static_cast<unsigned long long>(transport_.maxPollGapMs),r.failedCommand,r.failedPhase,r.cleanupError);
    for(unsigned i=0;i<transport_.txSize;++i)std::printf("%02x",transport_.tx[i]);
    std::printf(" rx_trace=");for(unsigned i=0;i<transport_.rxSize;++i)std::printf("%02x",transport_.rx[i]);
    std::printf(" discarded_trace=");for(unsigned i=0;i<transport_.discardedSize;++i)std::printf("%02x",transport_.discarded[i]);
    std::putchar('\n');
    std::printf("NFC test active=%d state=%s connected=%d scanning=%d swipes=%lu armed=%d last_uid=%s type=%s error=%s rf_off=%s physical_uid_received=%d\n",
        testActive_,t.state,t.connected,t.scanning,static_cast<unsigned long>(t.swipes),t.armed,
        t.uid[0]?t.uid:"-",t.type[0]?t.type:"-",t.error,t.rfOff?"confirmed":"unknown",r.uidValid);


}
bool Device::command(const char* line, std::uint64_t now, bool allowed) {
    if (std::strncmp(line, "nfc", 3) || (line[3] && line[3] != ' ' && line[3] != '\t')) return false;
    if (!std::strcmp(line, "nfc status")) { status(); return true; }
    if (testActive_ && std::strcmp(line,"nfc stop")) {
        std::puts("NFC Test owns reader; use its retry/reset controls or leave screen."); return true;
    }
    if (!std::strcmp(line,"nfc cards on")) {
        stopped_=false;nextScan_=now;invalidate();
        std::puts(cardsConfigured()?"NFC card polling enabled for eligible local battles; physical card read unverified.":"NFC card polling unavailable: build disabled or UID allowlist empty/invalid. No default bindings.");return true;
    }
    if (!std::strcmp(line, "nfc stop") || !std::strcmp(line,"nfc cards off")) {
        stopped_=true;cancelCards_=true;invalidate();session_.cancel(now);
        if(owner_==Owner::Test){testCancelled_=true;testFault_=true;}
        status();return true;
    }
    const bool scan = !std::strcmp(line, "nfc scan");
    if (!scan && std::strcmp(line, "nfc probe")) {
        std::puts("NFC commands: nfc status | nfc probe | nfc scan | nfc stop | nfc cards on/off. Probe/scan require verified UART wiring; one bounded scan, no card writes or rewards."); return true;
    }
    if(owner_==Owner::Cards) {
        stopped_=true;cancelCards_=true;invalidate();
        if(session_.busy()) {
            session_.cancel(now);std::puts("NFC card polling stopping; retry diagnostic after cleanup. Use nfc cards on to resume gameplay scans.");return true;
        }
        close();owner_=Owner::None;
    }
    if (!allowed || busy()) { std::puts("NFC blocked: runtime busy/power/recovery; wait, or nfc stop."); return true; }
    if (!open()) { std::printf("NFC open failed: %s; no probe started.\n", esp_err_to_name(error_)); return true; }
    if (!session_.start(scan, now)) { close(); status(); return true; }
    owner_=Owner::Diagnostic;invalidate();
    std::puts(scan ? "NFC scan started: hold one ISO14443A/NTAG/MIFARE card over the antenna; one bounded UID read." :
        "NFC firmware probe started; USB enumeration alone is not NFC detection.");
    return true;
}
bool Device::serviceDiagnostics(std::uint64_t now, bool allowed) {
    if(session_.busy()) {
        if(seenService_ && now>=lastService_ && now-lastService_>transport_.maxPollGapMs)transport_.maxPollGapMs=now-lastService_;
        lastService_=now;seenService_=true;
    }
    if(pollTest(now,allowed))return true;
    if(owner_==Owner::Diagnostic) {
        if(!allowed)session_.cancel(now);
        session_.poll(now);
        if(!session_.busy()){close();owner_=Owner::None;status();}
        return true; // Diagnostic UID results NEVER enter the gameplay mailbox.
    }
    return false;
}
void Device::poll(std::uint64_t now, bool allowed, bool cardsAllowed,std::uint32_t encounter) {
    const bool stale=seenPoll_ && (now<lastPoll_ || now-lastPoll_>digimodify::ReaderLatch::kMaxGapMs);
    lastPoll_=now;seenPoll_=true;
    if(serviceDiagnostics(now,allowed))return;
    const bool eligible=cardsAllowed && cardsConfigured() && !stopped_ && !testActive_;
    if(owner_==Owner::Cards && (!eligible||stale||encounter!=encounter_)) {
        cancelCards_=true;invalidate();session_.cancel(now);
    }
    if(owner_==Owner::Cards && session_.busy()) {
        session_.poll(now); // At most one nonblocking <=64-byte read/write.
        if(session_.busy())return;
        const auto& result=session_.result();
        if(cancelCards_ || std::strcmp(result.error,"none") || !result.rfOff) {
            invalidate();close();owner_=Owner::None;cancelCards_=false;
            nextScan_=now+1000;return; // Bounded fault/cancellation backoff.
        }
        sample_=identity_.observe(result,nfccards::config::bindings,nfccards::config::count);pending_=true;
        nextScan_=now+40;return; // Emit once, only after a new response AND RF-off acknowledgement.
    }
    if(owner_==Owner::Cards && (!eligible||cancelCards_)) {
        invalidate();close();owner_=Owner::None;cancelCards_=false;nextScan_=now+1000;return;
    }
    if(!eligible || now<nextScan_)return;
    if(owner_==Owner::Cards) {
        if(!session_.rescan(now)){invalidate();close();owner_=Owner::None;nextScan_=now+1000;}
        return;
    }
    invalidate();cancelCards_=false;
    if(!open()){nextScan_=now+1000;return;}
    if(!session_.start(true,now)){close();nextScan_=now+1000;return;}
    encounter_=encounter;owner_=Owner::Cards;
}
} // namespace digivice::nfcdiag
