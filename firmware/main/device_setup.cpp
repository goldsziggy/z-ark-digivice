#include "device_setup.hpp"
#include <algorithm>
#include <cstring>

namespace digivice {
namespace {
void wipe(void* value, std::size_t length) {
    auto* bytes = static_cast<volatile unsigned char*>(value);
    while (length--) *bytes++ = 0;
}
}

setupui::Model DeviceSetup::model() {
    const auto& scan = network_.scanStatus();
    const auto count = std::min(scan.count, setupui::kMaxAccessPoints);
    for (std::size_t i = 0; i < count; ++i) {
        std::memcpy(accessPoints_[i].ssid, scan.accessPoints[i].ssid, sizeof(accessPoints_[i].ssid));
        accessPoints_[i].rssi = scan.accessPoints[i].rssi;
        accessPoints_[i].supported = scan.accessPoints[i].supported;
    }
    setupui::Model view;
    view.network = network_.status();
    view.ready = network_.ready(); view.recovery = network_.recoveryRequired();
    view.scanning = scan.busy; view.accessPoints = accessPoints_; view.count = count;
    view.endpoint = network_.endpoint();
    view.privateHttpAllowed = net::NetworkAdapter::privateHttpBuildEnabled();
    view.allowPrivateHttp = network_.allowsPrivateHttp();
    view.clockReady = network_.clockState() == net::ClockState::Ready;
    view.clockWaiting = network_.clockState() == net::ClockState::Waiting;
    view.clockFailed = network_.clockState() == net::ClockState::Failed;
    view.notice = notice_;
    return view;
}

void DeviceSetup::open() {
    notice_ = nullptr; sawScanBusy_ = false;
    ui_.open(); ui_.update(model());
}
void DeviceSetup::close() {
    ui_.close(); notice_ = nullptr;
    network_.cancelScan(); // Adapter respects paused_ when its STOP arrives.
    wipe(accessPoints_, sizeof(accessPoints_));
}
void DeviceSetup::poll() {
    if (!active()) return;
    const auto& scan = network_.scanStatus();
    if (sawScanBusy_ && !scan.busy)
        notice_ = scan.error == ESP_OK ? (scan.count ? "SELECT YOUR HOTSPOT" : "NO NETWORKS - TRY AGAIN") : "SCAN FAILED - TRY AGAIN";
    sawScanBusy_ = scan.busy;
    ui_.update(model());
}
void DeviceSetup::touch(deviceui::Touch event) {
    if (!active()) return;
    using I = setupui::Intent;
    const auto intent = ui_.touch(model(), event);
    switch (intent) {
    case I::Scan:
        notice_ = network_.startScan() == ESP_OK ? "SCANNING 2.4 GHZ" : "SCAN UNAVAILABLE";
        sawScanBusy_ = network_.scanStatus().busy;
        break;
    case I::SaveWifi: {
        char ssid[33]{}, password[65]{};
        if (ui_.takeWifi(ssid, password)) {
            const auto error = network_.configureWifi(ssid, password);
            notice_ = error == ESP_OK ? "SAVED - CONNECTING" : "NOT SAVED - TRY AGAIN";
        }
        wipe(ssid, sizeof(ssid)); wipe(password, sizeof(password));
        break;
    }
    case I::SaveEndpoint: {
        char endpoint[193]{}; bool allowPrivateHttp = false;
        if (ui_.takeEndpoint(endpoint, allowPrivateHttp)) {
            const auto error = network_.configureEndpoint(endpoint, allowPrivateHttp);
            notice_ = error == ESP_OK ? "SERVER SAVED" : "SERVER NOT SAVED";
        }
        wipe(endpoint, sizeof(endpoint));
        break;
    }
    case I::Retry: network_.retry(); notice_ = "RECONNECT REQUESTED"; break;
    case I::Forget:
        notice_ = network_.forget() == ESP_OK ? "WIFI FORGOTTEN" : "WIFI NOT FORGOTTEN";
        break;
    case I::Close: close(); break;
    case I::None: break;
    }
    if (active()) ui_.update(model());
}
bool DeviceSetup::render(std::uint16_t* pixels, std::size_t capacity) {
    return active() && ui_.render(model(), pixels, capacity);
}

} // namespace digivice
