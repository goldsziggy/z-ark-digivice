#pragma once

#include "../runtime/network.hpp"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include <atomic>

namespace digivice::net {

constexpr std::size_t kScanAccessPoints = 16;
struct ScanAccessPoint {
    char ssid[33]{};
    std::int8_t rssi = 0;
    bool supported = false; // WPA2 personal, including mixed WPA2/WPA3 mode.
};
struct ScanStatus {
    ScanAccessPoint accessPoints[kScanAccessPoints]{};
    std::size_t count = 0;
    bool busy = false;
    esp_err_t error = ESP_OK;
    std::uint32_t generation = 0;
};
enum class ClockState : std::uint8_t { Unset, Waiting, Ready, Failed };

// One application-lifetime instance. All public methods belong to one owner
// task; tick at least every 100 ms. SDK callbacks and HTTP worker only post queues.
// Owns Wi-Fi STA exclusively; it must not share a driver with another provisioner.
// HTTPS waits for a valid system clock. On IP, one SDK SNTP client bootstraps
// via pool.ntp.org, at most three15s attempts per explicit retry cycle. No TLS
// bypass; clock/server failures retain local play. Pause deinitializes SNTP.
class NetworkAdapter {
public:
    NetworkAdapter() = default;
    NetworkAdapter(const NetworkAdapter&) = delete;
    NetworkAdapter& operator=(const NetworkAdapter&) = delete;
    esp_err_t begin(); // Does not erase NVS on initialization/recovery failure.
    esp_err_t configure(const Config& config); // Commit first; no secret logging.
    // On-device setup only receives the newly entered password; stored secrets
    // never leave this owner. Empty endpoint retains Wi-Fi/offline functionality.
    esp_err_t configureWifi(const char* ssid, const char* password);
    esp_err_t configureEndpoint(const char* endpoint, bool allowPrivateHttp);
    esp_err_t startScan(); // Async, STA-only2.4GHz; restores normal joining after.
    void cancelScan();
    const ScanStatus& scanStatus() const { return scan_; }
    ClockState clockState() const { return clockState_; }
    esp_err_t forget(); // Explicit command: erase only netcfg/config, never saves.
    void tick(); // No socket waits; drains fixed queues and applies FSM commands.
    void retry();
    void pause(bool paused);
    // Continue tick() during shutdown. ESP_OK means paused, radio STOP was
    // acknowledged and the HTTP worker released all resources. NOT_FINISHED
    // means wait; other errors must retain power. No hard HTTP timeout claim.
    esp_err_t quiescence() const;
    // Exclusive initialized/stopped STA lease for Nearby. Caller first pauses
    // this adapter; tick must continue while borrowed. No config/NVS mutation.
    esp_err_t beginRadioLease();
    // Call only after Radio.end() succeeds, passing Radio.startedThisLease().
    // A successful start requires ordered START/STOP event acknowledgments;
    // keep polling on NOT_FINISHED. Failures retain the lease until reboot.
    esp_err_t releaseRadioLease(bool radioWasStarted);
    bool radioLeased() const { return radioLease_; }
    const Status& status() const { return controller_.status(); }
    bool recoveryRequired() const { return recoveryRequired_; }
    bool ready() const { return ready_; }
    // Owner-requested pause, distinct from temporary scan suspension of the FSM.
    bool requestedPaused() const { return paused_; }
    static bool privateHttpBuildEnabled();
    // Origin only for a separate downloader. No password/SSID accessor.
    const char* endpoint() const { return config_.endpoint; }
    bool allowsPrivateHttp() const { return config_.allowPrivateHttp && privateHttpBuildEnabled(); }
private:
    enum class WireType : std::uint8_t { Started, Stopped, GotIp, Disconnected, ScanDone };
    struct WireEvent { WireType type; std::uint32_t generation; std::uint32_t detail = 0; };
    enum class ScanPhase : std::uint8_t { Idle, StopBefore, Starting, Running, StopAfter };
    struct ProbeJob { std::uint32_t token; char endpoint[193]; };
    struct ProbeReply { std::uint32_t token; bool reachable; };
    static void eventHandler(void* context, esp_event_base_t base, std::int32_t id, void* data);
    static void probeWorker(void* context);
    void stopRadio();
    void runRadioPlan();
    void runScanPlan(std::uint64_t now);
    void finishScan(esp_err_t error);
    void collectScan();
    void pollClock(std::uint64_t now);
    void stopClock();
    bool currentIpMatches() const;
    Controller controller_{};
    Config config_{};
    nvs_handle_t nvs_ = 0;
    esp_netif_t* interface_ = nullptr;
    esp_event_handler_instance_t wifiHandler_ = nullptr;
    esp_event_handler_instance_t ipHandler_ = nullptr;
    QueueHandle_t events_ = nullptr, jobs_ = nullptr, replies_ = nullptr;
    StaticQueue_t eventQueue_{}, jobQueue_{}, replyQueue_{};
    std::uint8_t eventBytes_[8 * sizeof(WireEvent)]{};
    std::uint8_t jobBytes_[sizeof(ProbeJob)]{};
    std::uint8_t replyBytes_[sizeof(ProbeReply)]{};
    TaskHandle_t worker_ = nullptr;
    std::atomic<std::uint32_t> eventGeneration_{0};
    std::atomic<bool> eventOverflow_{false};
    // Reserved before enqueue and released only after worker cleanup. Never
    // reset the job queue: dequeue/reset races could otherwise fake idle.
    std::atomic<bool> probePending_{false}, probeCancelled_{false};
    bool ready_ = false, recoveryRequired_ = false;
    bool paused_ = false, radioStateFailed_ = false;
    bool radioStarted_ = false, startPending_ = false, stopPending_ = false;
    bool joinPlanned_ = false;
    bool radioLease_ = false, leaseReleasing_ = false, leaseStartSeen_ = false;
    bool leaseStopSeen_ = false, leaseExpectedStart_ = false;
    std::uint64_t leaseDeadlineMs_ = 0;
    std::uint32_t plannedGeneration_ = 0;
    std::uint64_t radioDeadlineMs_ = 0;
    ScanStatus scan_{};
    ScanPhase scanPhase_ = ScanPhase::Idle;
    std::uint64_t scanDeadlineMs_ = 0;
    ClockState clockState_ = ClockState::Unset;
    bool clockActive_ = false;
    std::uint8_t clockAttempts_ = 0;
    std::uint64_t clockDeadlineMs_ = 0, clockRetryMs_ = 0;
};
} // namespace digivice::net
