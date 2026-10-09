#pragma once
#include "esp_err.h"
#include "esp_now.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace digivice::nearby {
constexpr std::size_t kRadioPacketBytes = 240;
constexpr std::size_t kRadioReceiveSlots = 8;
constexpr std::uint8_t kRadioChannel = 1;
struct RadioPacket {
    std::uint8_t source[6]{}, destination[6]{};
    std::uint16_t size = 0;
    std::int8_t rssi = 0;
    bool broadcast = false;
    std::uint32_t generation = 0;
    std::uint8_t bytes[kRadioPacketBytes]{};
};
struct RadioSendResult {
    std::uint32_t token = 0, generation = 0;
    bool delivered = false; // MAC callback only, NEVER an application receipt.
};
struct RadioStatus {
    bool active = false, peerSelected = false, encrypted = false, sending = false;
    bool recoveryRequired = false;
    std::uint32_t receivedDropped = 0;
    esp_err_t error = ESP_OK;
};

// One application-lifetime instance; public calls serialized on the UI owner.
// Prerequisite: root owns an EXCLUSIVE lease of initialized, stopped Wi-Fi STA,
// with NetworkAdapter paused/quiescent and its borrowed START/STOP event routing
// handled. Merely pausing without a lease would make its tick stop this radio.
// begin starts that STA on fixed channel1, then ESP-NOW. end unregisters/deinits
// ESP-NOW then requests Wi-Fi stop; caller awaits the Wi-Fi STOP acknowledgement
// before restoring its prior hotspot/pause state. Never deinitializes Wi-Fi/NVS.
// No protocol, retries, matching, pairing trust, gameplay or persistent keys.
// Broadcast is unencrypted; an optional already-agreed16B LMK enables unicast
// encryption. MAC identity/unencrypted packets are not authenticated identities.
class Radio {
public:
    Radio() = default;
    Radio(const Radio&) = delete;
    Radio& operator=(const Radio&) = delete;
    esp_err_t begin();
    esp_err_t end();
    // Retained across end for the NetworkAdapter START/STOP lease barrier.
    bool startedThisLease() const { return startedThisLease_; }
    esp_err_t selectPeer(const std::uint8_t mac[6], const std::uint8_t* lmk = nullptr);
    esp_err_t clearPeer();
    esp_err_t sendBroadcast(const void* bytes, std::size_t length, std::uint32_t token);
    esp_err_t sendPeer(const void* bytes, std::size_t length, std::uint32_t token);
    bool receive(RadioPacket& packet);
    bool sendResult(RadioSendResult& result);
    void tick(); // Missing send callback after1500ms -> fail closed; explicit end required.
    RadioStatus status() const;
    const std::uint8_t* identity() const { return identity_; }
    // Adapter resources only. Wi-Fi STOP event barrier still belongs to lease owner.
    bool quiescent() const;
private:
    static void received(const esp_now_recv_info_t*, const std::uint8_t*, int);
    static void sent(const std::uint8_t*, esp_now_send_status_t);
    esp_err_t send(const std::uint8_t*, const void*, std::size_t, std::uint32_t);
    QueueHandle_t receives_ = nullptr, sends_ = nullptr;
    StaticQueue_t receiveQueue_{}, sendQueue_{};
    std::uint8_t receiveBytes_[kRadioReceiveSlots * sizeof(RadioPacket)]{};
    std::uint8_t sendBytes_[sizeof(RadioSendResult)]{};
    std::uint8_t identity_[6]{}, peer_[6]{}, sendingTo_[6]{};
    std::atomic<bool> accepting_{false};
    std::atomic<std::uint32_t> generation_{0}, callbacks_{0}, dropped_{0};
    mutable portMUX_TYPE callbackLock_ = portMUX_INITIALIZER_UNLOCKED;
    std::uint32_t token_ = 0;
    std::uint64_t sentAtMs_ = 0;
    bool txPending_ = false, txCompleted_ = false, peerSelected_ = false, encrypted_ = false;
    bool wifiStarted_ = false, initialized_ = false, recvRegistered_ = false, sendRegistered_ = false;
    bool recoveryRequired_ = false, startedThisLease_ = false;
    esp_err_t error_ = ESP_OK;
};
} // namespace digivice::nearby
