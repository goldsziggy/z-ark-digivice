#include "network_adapter.hpp"
#include "game.hpp"

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include <cstdio>
#include <cstring>
#include <ctime>

// API references (ESP-IDF5.3.1 compatibility target, not a compiled-board claim):
// https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-reference/network/esp_wifi.html
// https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-reference/protocols/esp_http_client.html
// https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-reference/storage/nvs_flash.html
namespace digivice::net {
namespace {
constexpr char kNamespace[] = "netcfg";
constexpr char kKey[] = "config";
constexpr std::int64_t kHealthTimeoutUs = 5000000;
std::uint64_t nowMs() { return static_cast<std::uint64_t>(esp_timer_get_time() / 1000); }
void wipe(void* buffer, std::size_t length) {
    auto* bytes = static_cast<volatile std::uint8_t*>(buffer);
    while (length--) *bytes++ = 0;
}
bool numberIs(const cJSON* root, const char* name, int value) {
    const auto* item = cJSON_GetObjectItemCaseSensitive(root, name);
    return cJSON_IsNumber(item) && item->valuedouble == value;
}
bool stringIs(const cJSON* root, const char* name, const char* value) {
    const auto* item = cJSON_GetObjectItemCaseSensitive(root, name);
    return cJSON_IsString(item) && item->valuestring && std::strcmp(item->valuestring, value) == 0;
}
bool validHealth(const char* body, std::size_t length) {
    // Require complete JSON and exactly five properties (including duplicates).
    // The protocol is a flat object. Reject nesting before cJSON can recurse.
    if (!length || std::memchr(body, '\0', length)) return false;
    bool quoted = false; unsigned depth = 0;
    for (std::size_t i = 0; i < length; ++i) {
        const char c = body[i];
        if (quoted) {
            // The fixed ASCII protocol has no escaped names/values. Rejecting
            // escapes also prevents cJSON's embedded U+0000/C-string ambiguity.
            if (c == '\\') return false;
            if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '[' || (c == '{' && ++depth > 1)) return false;
        else if (c == '}' && depth) --depth;
    }
    const char* end = nullptr;
    auto* root = cJSON_ParseWithLengthOpts(body, length + 1, &end, true);
    if (!root) return false;
    const bool valid = end == body + length && cJSON_IsObject(root) && cJSON_GetArraySize(root) == 5 &&
        stringIs(root, "status", "ok") && numberIs(root, "protocolVersion", 1) &&
        numberIs(root, "gameRulesVersion", static_cast<int>(kRulesVersion)) &&
        numberIs(root, "gameSchemaVersion", static_cast<int>(kSchemaVersion)) &&
        stringIs(root, "assetProfile", "s3-146-v1");
    cJSON_Delete(root); return valid;
}
bool healthProbe(const char* endpoint, const std::atomic<bool>& cancelled) {
    // Certificate verification remains enabled. The owner waits for SNTP's
    // clock bootstrap before HTTPS. A valid clock and certificate
    // chain are prerequisites; there is no insecure TLS fallback. This proves
    // origin/health compatibility, not application pairing or asset authority.
    char url[224]{};
    auto length = std::strlen(endpoint);
    if (length && endpoint[length - 1] == '/') --length;
    if (std::snprintf(url, sizeof(url), "%.*s/api/device/health", static_cast<int>(length), endpoint) >= static_cast<int>(sizeof(url))) return false;
    esp_http_client_config_t options{};
    options.url = url;
    options.method = HTTP_METHOD_GET;
    options.timeout_ms = 5000;
    options.disable_auto_redirect = true;
    options.max_authorization_retries = -1;
    options.buffer_size = 512;
    options.buffer_size_tx = 512;
    options.skip_cert_common_name_check = false;
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
    options.crt_bundle_attach = esp_crt_bundle_attach;
#else
    if (std::strncmp(endpoint, "https://", 8) == 0) return false;
#endif
    const auto client = esp_http_client_init(&options);
    if (!client) return false;
    // IDF5.3.1 can loop internally while fetching slow headers. timeout_ms is
    // a socket-operation timeout, NOT a hard total wallclock guarantee. This
    // elapsed deadline rejects late responses; the FSM expires at8s regardless.
    // Exactly one worker exists, so a slow peer cannot spawn accumulating tasks.
    const auto deadline = esp_timer_get_time() + kHealthTimeoutUs;
    const auto setRemainingTimeout = [&]() {
        const auto remaining = deadline - esp_timer_get_time();
        return !cancelled.load() && remaining > 0 && esp_http_client_set_timeout_ms(client, static_cast<int>((remaining + 999) / 1000)) == ESP_OK;
    };
    bool valid = false;
    char body[513]{};
    if (setRemainingTimeout() && esp_http_client_open(client, 0) == ESP_OK && setRemainingTimeout()) {
        const auto declared = esp_http_client_fetch_headers(client);
        if (declared >= 0 && declared <= 512 && esp_http_client_get_status_code(client) == 200) {
            std::size_t used = 0;
            while (used < sizeof(body) && setRemainingTimeout()) {
                const int count = esp_http_client_read(client, body + used, static_cast<int>(sizeof(body) - used));
                if (count < 0) break;
                if (count == 0) {
                    if (used <= 512 && esp_http_client_is_complete_data_received(client) && esp_timer_get_time() < deadline) {
                        body[used] = '\0'; valid = validHealth(body, used);
                    }
                    break;
                }
                used += static_cast<std::size_t>(count);
                if (used > 512) break;
            }
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return valid;
}
} // namespace

bool NetworkAdapter::privateHttpBuildEnabled() {
#if CONFIG_DIGIVICE_ALLOW_PRIVATE_HTTP
    return true;
#else
    return false;
#endif
}

esp_err_t NetworkAdapter::begin() {
    if (ready_ || events_) return ESP_ERR_INVALID_STATE;
    auto error = nvs_flash_init();
    if (error != ESP_OK) {
        recoveryRequired_ = true; controller_.pause(true, nowMs()); joinPlanned_ = false; stopRadio();
        probeCancelled_.store(true);
        return error;
    }
    error = nvs_open(kNamespace, NVS_READWRITE, &nvs_);
    if (error != ESP_OK) {
        recoveryRequired_ = true; controller_.pause(true, nowMs()); joinPlanned_ = false; stopRadio();
        probeCancelled_.store(true);
        return error;
    }
    ConfigSnapshot snapshot{}; std::size_t length = 0;
    error = nvs_get_blob(nvs_, kKey, nullptr, &length);
    bool configured = false;
    if (error == ESP_OK) {
        if (length != kConfigBytes) recoveryRequired_ = true;
        else {
            error = nvs_get_blob(nvs_, kKey, snapshot.bytes, &length);
            recoveryRequired_ = error != ESP_OK || decodeConfig(snapshot.bytes, length, config_, privateHttpBuildEnabled()) != ConfigRead::Ok;
            configured = !recoveryRequired_;
        }
    } else if (error != ESP_ERR_NVS_NOT_FOUND) recoveryRequired_ = true;
    wipe(&snapshot, sizeof(snapshot));
    // Corrupt/future credentials stay untouched. Offline setup still starts,
    // permitting explicit wifi-forget; no game namespace is ever erased here.
    events_ = xQueueCreateStatic(8, sizeof(WireEvent), eventBytes_, &eventQueue_);
    jobs_ = xQueueCreateStatic(1, sizeof(ProbeJob), jobBytes_, &jobQueue_);
    replies_ = xQueueCreateStatic(1, sizeof(ProbeReply), replyBytes_, &replyQueue_);
    if (!events_ || !jobs_ || !replies_) return ESP_ERR_NO_MEM;
    error = esp_netif_init();
    if (error != ESP_OK) return error;
    error = esp_event_loop_create_default();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) return error;
    interface_ = esp_netif_create_default_wifi_sta();
    if (!interface_) return ESP_ERR_NO_MEM;
    wifi_init_config_t initial = WIFI_INIT_CONFIG_DEFAULT();
    error = esp_wifi_init(&initial);
    if (error != ESP_OK) return error;
    // Only our explicit bounded blob persists credentials. The SDK's own
    // Wi-Fi namespace must not acquire a second implicit credential copy.
    error = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (error != ESP_OK) return error;
    error = esp_wifi_set_mode(WIFI_MODE_STA);
    if (error != ESP_OK) return error;
    error = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, eventHandler, this, &wifiHandler_);
    if (error != ESP_OK) return error;
    error = esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, eventHandler, this, &ipHandler_);
    if (error != ESP_OK) return error;
    if (xTaskCreate(probeWorker, "service_health", 6144, this, 1, &worker_) != pdPASS) return ESP_ERR_NO_MEM;
    ready_ = true;
    controller_.configure(configured, nowMs(), esp_random());
    return ESP_OK;
}

esp_err_t NetworkAdapter::configure(const Config& config) {
    if (!ready_ || recoveryRequired_ || paused_ || radioLease_ || scanPhase_ != ScanPhase::Idle) return ESP_ERR_INVALID_STATE;
    ConfigSnapshot snapshot{};
    if (!encodeConfig(config, snapshot, privateHttpBuildEnabled())) return ESP_ERR_INVALID_ARG;
    auto error = nvs_set_blob(nvs_, kKey, snapshot.bytes, sizeof(snapshot.bytes));
    if (error == ESP_OK) error = nvs_commit(nvs_);
    wipe(&snapshot, sizeof(snapshot));
    if (error != ESP_OK) {
        recoveryRequired_ = true; controller_.pause(true, nowMs()); joinPlanned_ = false; stopRadio();
        probeCancelled_.store(true);
        return error;
    }
    probeCancelled_.store(true);
    wipe(&config_, sizeof(config_)); config_ = config;
    if (clockState_ != ClockState::Ready) { clockAttempts_ = 0; clockRetryMs_ = 0; }
    controller_.configure(true, nowMs(), esp_random());
    joinPlanned_ = false; stopRadio();
    return ESP_OK;
}
esp_err_t NetworkAdapter::configureWifi(const char* ssid, const char* password) {
    if (!ssid || !password || strnlen(ssid, 33) > 32 || strnlen(password, 65) > 64)
        return ESP_ERR_INVALID_ARG;
    Config next = config_;
    wipe(next.ssid, sizeof(next.ssid)); wipe(next.password, sizeof(next.password));
    std::memcpy(next.ssid, ssid, std::strlen(ssid));
    std::memcpy(next.password, password, std::strlen(password));
    const auto error = configure(next);
    wipe(&next, sizeof(next));
    return error;
}
esp_err_t NetworkAdapter::configureEndpoint(const char* endpoint, bool allowPrivateHttp) {
    if (!endpoint || strnlen(endpoint, 193) > 192) return ESP_ERR_INVALID_ARG;
    Config next = config_;
    wipe(next.endpoint, sizeof(next.endpoint));
    std::memcpy(next.endpoint, endpoint, std::strlen(endpoint));
    next.allowPrivateHttp = allowPrivateHttp;
    const auto error = configure(next);
    wipe(&next, sizeof(next));
    return error;
}

esp_err_t NetworkAdapter::startScan() {
    if (!ready_ || recoveryRequired_ || paused_ || radioLease_ || scanPhase_ != ScanPhase::Idle)
        return ESP_ERR_INVALID_STATE;
    const auto generation = scan_.generation + 1;
    scan_ = {}; scan_.generation = generation; scan_.busy = true;
    scanPhase_ = ScanPhase::StopBefore;
    scanDeadlineMs_ = nowMs() + 15000;
    controller_.pause(true, nowMs());
    eventGeneration_.store(controller_.status().generation);
    probeCancelled_.store(true); joinPlanned_ = false;
    stopClock();
    stopRadio();
    return ready_ ? ESP_OK : ESP_FAIL;
}

void NetworkAdapter::finishScan(esp_err_t error) {
    if (scanPhase_ == ScanPhase::Idle || scanPhase_ == ScanPhase::StopAfter) return;
    // The SDK owns its scan-list allocation; discard it on every exit path.
    if (scanPhase_ == ScanPhase::Running) (void)esp_wifi_scan_stop();
    (void)esp_wifi_clear_ap_list();
    scan_.error = error;
    scanPhase_ = ScanPhase::StopAfter;
    stopRadio();
}
void NetworkAdapter::cancelScan() { if (!radioLease_) finishScan(ESP_ERR_INVALID_STATE); }

void NetworkAdapter::collectScan() {
    wifi_ap_record_t records[kScanAccessPoints]{};
    std::uint16_t count = kScanAccessPoints;
    const auto error = esp_wifi_scan_get_ap_records(&count, records);
    if (error == ESP_OK) {
        for (std::size_t i = 0; i < count && i < kScanAccessPoints; ++i) {
            const auto length = strnlen(reinterpret_cast<const char*>(records[i].ssid), 33);
            if (!length || length > 32) continue;
            bool valid = true, duplicate = false;
            for (std::size_t j = 0; j < length; ++j)
                if (records[i].ssid[j] < 32 || records[i].ssid[j] == 127) valid = false;
            for (std::size_t j = 0; j < scan_.count; ++j)
                if (!std::strcmp(scan_.accessPoints[j].ssid, reinterpret_cast<const char*>(records[i].ssid))) duplicate = true;
            if (!valid || duplicate) continue;
            auto& item = scan_.accessPoints[scan_.count++];
            std::memcpy(item.ssid, records[i].ssid, length);
            item.rssi = records[i].rssi;
            item.supported = records[i].authmode == WIFI_AUTH_WPA2_PSK ||
                records[i].authmode == WIFI_AUTH_WPA_WPA2_PSK || records[i].authmode == WIFI_AUTH_WPA2_WPA3_PSK;
        }
    }
    wipe(records, sizeof(records));
    finishScan(error);
}

void NetworkAdapter::runScanPlan(std::uint64_t now) {
    if (scanPhase_ == ScanPhase::Idle) return;
    if (scanPhase_ != ScanPhase::StopAfter && (paused_ || now >= scanDeadlineMs_))
        finishScan(paused_ ? ESP_ERR_INVALID_STATE : ESP_ERR_TIMEOUT);
    if (scanPhase_ == ScanPhase::StopBefore && !radioStarted_ && !startPending_ && !stopPending_) {
        const auto error = esp_wifi_start();
        if (error == ESP_OK) {
            startPending_ = true; radioDeadlineMs_ = now + 5000;
            scanPhase_ = ScanPhase::Starting;
        } else finishScan(error);
    }
    if (scanPhase_ == ScanPhase::StopAfter && !radioStarted_ && !startPending_ && !stopPending_) {
        scanPhase_ = ScanPhase::Idle; scan_.busy = false;
        if (!paused_ && !recoveryRequired_) controller_.pause(false, now);
    }
}

esp_err_t NetworkAdapter::forget() {
    if (!ready_ || paused_ || radioLease_ || scanPhase_ != ScanPhase::Idle) return ESP_ERR_INVALID_STATE;
    auto error = nvs_erase_key(nvs_, kKey);
    if (error == ESP_ERR_NVS_NOT_FOUND) error = ESP_OK;
    if (error == ESP_OK) error = nvs_commit(nvs_);
    if (error != ESP_OK) {
        recoveryRequired_ = true; controller_.pause(true, nowMs()); joinPlanned_ = false; stopRadio();
        probeCancelled_.store(true);
        return error;
    }
    probeCancelled_.store(true);
    controller_.configure(false, nowMs());
    joinPlanned_ = false; stopRadio();
    wipe(&config_, sizeof(config_)); recoveryRequired_ = false;
    return ESP_OK;
}
void NetworkAdapter::retry() {
    if (ready_ && !recoveryRequired_ && !paused_ && !radioLease_ && scanPhase_ == ScanPhase::Idle) {
        if (clockState_ != ClockState::Ready) { clockAttempts_ = 0; clockRetryMs_ = 0; }
        controller_.retry(nowMs());
    }
}
void NetworkAdapter::pause(bool paused) {
    // Borrower must deinitialize ESP-NOW before Wi-Fi stop. A power pause may
    // retain the gate, but cannot bypass that owner or resume hotspot joining.
    if (radioLease_) { if (paused) paused_ = true; return; }
    // Independently gate owner requests, even after partial initialization.
    // A pending connect can outlive the controller's configured status.
    paused_ = paused;
    if (ready_ && (paused || !recoveryRequired_)) {
        controller_.pause(paused, nowMs());
    }
    if (paused) {
        cancelScan();
        stopClock();
        probeCancelled_.store(true);
        joinPlanned_ = false;
        stopRadio();
    }
}
esp_err_t NetworkAdapter::quiescence() const {
    if (!paused_) return ESP_ERR_INVALID_STATE;
    // Even a failed radio transition cannot release the worker's reservation.
    // Callers may offer local resume after a terminal radio error, but only
    // after pending HTTP cleanup has completed.
    if (probePending_.load()) return ESP_ERR_NOT_FINISHED;
    if (clockActive_) return ESP_ERR_NOT_FINISHED;
    if (radioStateFailed_) return ESP_FAIL;
    if (radioLease_) return ESP_ERR_NOT_FINISHED;
    if (radioStarted_ || startPending_ || stopPending_ || joinPlanned_ || scanPhase_ != ScanPhase::Idle)
        return ESP_ERR_NOT_FINISHED;
    return ESP_OK;
}


esp_err_t NetworkAdapter::beginRadioLease() {
    if (!ready_ || recoveryRequired_ || radioLease_ || !paused_)
        return ESP_ERR_INVALID_STATE;
    // Consume queued ordinary events first; a delayed old START must complete
    // its normal STOP barrier before an unrelated owner can borrow the driver.
    tick();
    const auto idle = quiescence();
    if (idle != ESP_OK) return idle;
    radioLease_ = true; leaseReleasing_ = false;
    leaseStartSeen_ = leaseStopSeen_ = leaseExpectedStart_ = false;
    leaseDeadlineMs_ = nowMs() + 5000;
    return ESP_OK;
}
esp_err_t NetworkAdapter::releaseRadioLease(bool radioWasStarted) {
    if (!radioLease_) return ESP_ERR_INVALID_STATE;
    if (radioStateFailed_ || recoveryRequired_ || !ready_) return ESP_FAIL;
    if (!leaseReleasing_) {
        leaseReleasing_ = true; leaseExpectedStart_ = radioWasStarted;
        leaseDeadlineMs_ = nowMs() + 5000;
    } else if (leaseExpectedStart_ != radioWasStarted) return ESP_ERR_INVALID_ARG;
    tick();
    if (radioStateFailed_ || recoveryRequired_ || !ready_) return ESP_FAIL;
    if (!radioWasStarted && (leaseStartSeen_ || leaseStopSeen_)) {
        radioStateFailed_ = recoveryRequired_ = true; ready_ = false;
        return ESP_FAIL;
    }
    if (radioWasStarted && (!leaseStartSeen_ || !leaseStopSeen_)) return ESP_ERR_NOT_FINISHED;
    radioLease_ = leaseReleasing_ = false;
    radioStarted_ = startPending_ = stopPending_ = false;
    leaseDeadlineMs_ = 0;
    // Explicit pause remains in effect. The caller restores its previous pause
    // state only after both the borrower and this event barrier are finished.
    return ESP_OK;
}

void NetworkAdapter::eventHandler(void* context, esp_event_base_t base, std::int32_t id, void* data) {
    auto& self = *static_cast<NetworkAdapter*>(context);
    WireType type;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) type = WireType::Started;
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_STOP) type = WireType::Stopped;
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) type = WireType::Disconnected;
    else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) type = WireType::ScanDone;
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) type = WireType::GotIp;
    else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) type = WireType::Disconnected;
    else return;
    const auto detail = type == WireType::ScanDone && data
        ? static_cast<wifi_event_sta_scan_done_t*>(data)->status : 0;
    const WireEvent event{type, self.eventGeneration_.load(), detail};
    if (xQueueSend(self.events_, &event, 0) != pdTRUE) self.eventOverflow_.store(true);
}
void NetworkAdapter::probeWorker(void* context) {
    auto& self = *static_cast<NetworkAdapter*>(context);
    ProbeJob job{};
    for (;;) {
        if (xQueueReceive(self.jobs_, &job, portMAX_DELAY) == pdTRUE) {
            const ProbeReply reply{job.token, !self.probeCancelled_.load() && healthProbe(job.endpoint, self.probeCancelled_)};
            xQueueOverwrite(self.replies_, &reply);
            self.probePending_.store(false);
        }
    }
}
bool NetworkAdapter::currentIpMatches() const {
    wifi_ap_record_t accessPoint{}; esp_netif_ip_info_t ip{};
    if (esp_wifi_sta_get_ap_info(&accessPoint) != ESP_OK ||
        esp_netif_get_ip_info(interface_, &ip) != ESP_OK || ip.ip.addr == 0) return false;
    return std::memcmp(accessPoint.ssid, config_.ssid, std::strlen(config_.ssid)) == 0 &&
        accessPoint.ssid[std::strlen(config_.ssid)] == 0;
}
void NetworkAdapter::stopRadio() {
    if (radioLease_) return; // Exclusive borrower owns stop ordering.
    stopClock();
    if (stopPending_ || (!radioStarted_ && !startPending_)) return;
    const auto error = esp_wifi_stop();
    if (error == ESP_OK) { stopPending_ = true; radioDeadlineMs_ = nowMs() + 5000; }
    else {
        controller_.pause(true, nowMs()); joinPlanned_ = false;
        radioStateFailed_ = true;
        ready_ = false; recoveryRequired_ = true; // Driver state requires reboot.
    }
}

void NetworkAdapter::stopClock() {
    if (clockActive_) { esp_netif_sntp_deinit(); clockActive_ = false; }
    if (clockState_ == ClockState::Waiting) clockState_ = ClockState::Unset;
}
void NetworkAdapter::pollClock(std::uint64_t now) {
    if (!controller_.status().hasIp || paused_ || scanPhase_ != ScanPhase::Idle) { stopClock(); return; }
    // An existing plausible clock still undergoes ordinary certificate checks.
    constexpr std::time_t earliest = 1704067200; //2024-01-01, not a forced clock.
    if (std::time(nullptr) >= earliest) {
        const bool becameReady = clockState_ != ClockState::Ready;
        stopClock(); clockState_ = ClockState::Ready;
        if (becameReady) controller_.retry(now);
        return;
    }
    if (clockActive_) {
        // Zero ticks is a nonblocking semaphore poll; no socket waits here.
        if (esp_netif_sntp_sync_wait(0) == ESP_OK && std::time(nullptr) >= earliest) {
            stopClock(); clockState_ = ClockState::Ready; controller_.retry(now);
        } else if (now >= clockDeadlineMs_) {
            stopClock(); clockState_ = ClockState::Failed; clockRetryMs_ = now + 60000;
        }
        return;
    }
    if (clockAttempts_ >= 3 || now < clockRetryMs_) return;
    esp_sntp_config_t config{};
    config.wait_for_sync = true; config.start = true;
    config.num_of_servers = 1; config.servers[0] = "pool.ntp.org";
    ++clockAttempts_;
    const auto error = esp_netif_sntp_init(&config);
    clockActive_ = error == ESP_OK;
    clockState_ = clockActive_ ? ClockState::Waiting : ClockState::Failed;
    clockDeadlineMs_ = now + 15000;
    clockRetryMs_ = now + 60000;
}
void NetworkAdapter::runRadioPlan() {
    if (paused_ || !joinPlanned_ || stopPending_) return;
    if (radioStarted_ || startPending_) { stopRadio(); return; }
    wifi_config_t wifi{};
    std::memcpy(wifi.sta.ssid, config_.ssid, std::strlen(config_.ssid));
    std::memcpy(wifi.sta.password, config_.password, std::strlen(config_.password));
    wifi.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi.sta.pmf_cfg.capable = true;
    wifi.sta.pmf_cfg.required = false;
    // All-channel scan supports a phone hotspot changing its2.4GHz channel.
    // No country/channel/power overrides, AP mode, GPIOs or open-network fallback.
    wifi.sta.channel = 0;
    auto error = esp_wifi_set_config(WIFI_IF_STA, &wifi);
    wipe(&wifi, sizeof(wifi));
    eventGeneration_.store(plannedGeneration_);
    if (error == ESP_OK) error = esp_wifi_start();
    if (error == ESP_OK) { startPending_ = true; radioDeadlineMs_ = nowMs() + 5000; }
    else controller_.disconnected(plannedGeneration_, nowMs());
    joinPlanned_ = false;
}
void NetworkAdapter::tick() {
    if (!ready_) return;
    const auto now = nowMs();
    if (eventOverflow_.exchange(false)) {
        // Loss of event ordering requires reboot. Even the STOP event could
        // have been lost; retry must not wait forever on an ambiguous barrier.
        controller_.pause(true, now); joinPlanned_ = false; stopRadio();
        probeCancelled_.store(true); radioStateFailed_ = true;
        ready_ = false; recoveryRequired_ = true;
        return;
    }
    WireEvent event{};
    for (unsigned i = 0; i < 8 && xQueueReceive(events_, &event, 0) == pdTRUE; ++i) {
        if (radioLease_) {
            // STA/IP callbacks carry no driver-session token. An exclusive
            // stopped-driver lease and ordered START/STOP acknowledgments form
            // the barrier; IP/disconnect/scan messages never reach the FSM.
            if (event.type == WireType::Started) {
                if (leaseStartSeen_ || leaseStopSeen_) {
                    radioStateFailed_ = recoveryRequired_ = true; ready_ = false;
                } else { leaseStartSeen_ = true; radioStarted_ = true; }
            } else if (event.type == WireType::Stopped) {
                if (!leaseStartSeen_ || leaseStopSeen_) {
                    radioStateFailed_ = recoveryRequired_ = true; ready_ = false;
                } else { leaseStopSeen_ = true; radioStarted_ = false; }
            }
            continue;
        }
        if (event.type == WireType::Stopped) {
            radioStarted_ = false; startPending_ = false; stopPending_ = false;
        } else if (event.type == WireType::Started) {
            startPending_ = false; radioStarted_ = true;
            if (scanPhase_ == ScanPhase::Starting && !paused_ && !stopPending_) {
                wifi_scan_config_t scan{};
                scan.show_hidden = false;
                scan.scan_type = WIFI_SCAN_TYPE_ACTIVE;
                scan.scan_time.active.min = 0;
                scan.scan_time.active.max = 120;
                const auto error = esp_wifi_scan_start(&scan, false);
                if (error == ESP_OK) scanPhase_ = ScanPhase::Running;
                else finishScan(error);
            } else if (scanPhase_ != ScanPhase::Idle) stopRadio();
            else if (!stopPending_ && event.generation == controller_.status().generation &&
                controller_.status().state == State::Joining) {
                if (esp_wifi_connect() != ESP_OK) {
                    controller_.disconnected(event.generation, now); stopRadio();
                }
            } else stopRadio();
        } else if (event.type == WireType::ScanDone) {
            if (scanPhase_ == ScanPhase::Running && !paused_ && !stopPending_) {
                if (event.detail == 0) collectScan();
                else finishScan(ESP_FAIL);
            } else (void)esp_wifi_clear_ap_list();
        } else if (scanPhase_ == ScanPhase::Idle && !stopPending_ && !startPending_) {
            if (event.type == WireType::GotIp && currentIpMatches()) controller_.gotIp(event.generation, now);
            else if (event.type == WireType::Disconnected && event.generation == controller_.status().generation) {
                controller_.disconnected(event.generation, now); stopRadio();
            }
        }
    }
    if (radioLease_) {
        const bool awaiting = leaseReleasing_
            ? leaseExpectedStart_ && (!leaseStartSeen_ || !leaseStopSeen_)
            : !leaseStartSeen_;
        if (awaiting && now >= leaseDeadlineMs_) {
            radioStateFailed_ = recoveryRequired_ = true; ready_ = false;
        }
        return; // No SNTP, health probe, scanning, connect or stop while leased.
    }
    if ((startPending_ || stopPending_) && now >= radioDeadlineMs_) {
        controller_.pause(true, now); joinPlanned_ = false;
        probeCancelled_.store(true); radioStateFailed_ = true;
        esp_wifi_stop(); // Best effort; never spin indefinitely on a lost barrier.
        ready_ = false; recoveryRequired_ = true;
        return;
    }
    if (!ready_) return;
    ProbeReply reply{};
    if (xQueueReceive(replies_, &reply, 0) == pdTRUE) controller_.serviceResult(reply.token, reply.reachable, now);
    if (scanPhase_ != ScanPhase::Idle) { runScanPlan(now); return; }
    pollClock(now);
    const auto command = controller_.tick(now);
    if (command.type == CommandType::Disconnect) { joinPlanned_ = false; stopRadio(); }
    else if (command.type == CommandType::Connect) { plannedGeneration_ = command.token; joinPlanned_ = true; }
    else if (command.type == CommandType::ProbeService) {
        if (paused_ || probePending_.load() || !config_.endpoint[0] ||
            (!std::strncmp(config_.endpoint, "https://", 8) && clockState_ != ClockState::Ready)) {
            controller_.serviceResult(command.token, false, now);
            return;
        }
        ProbeJob job{}; job.token = command.token;
        std::memcpy(job.endpoint, config_.endpoint, sizeof(job.endpoint));
        probeCancelled_.store(false); probePending_.store(true);
        if (xQueueSend(jobs_, &job, 0) != pdTRUE) {
            probePending_.store(false);
            controller_.serviceResult(command.token, false, now);
        }
    }
    runRadioPlan();
}
} // namespace digivice::net
