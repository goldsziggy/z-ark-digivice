#pragma once

#include "network_adapter.hpp"
#include "../runtime/setup_ui.hpp"

namespace digivice {

// One main-task owner, alongside NetworkAdapter. Never calls its tick() or
// overrides its power pause. No sockets, logs, game-state writes or allocation.
class DeviceSetup {
public:
    explicit DeviceSetup(net::NetworkAdapter& network) : network_(network) {}
    void open();
    void close(); // Wipes drafts, cancels scan; never resumes a power-paused radio.
    void suspend() { close(); }
    bool active() const { return ui_.active(); }
    void poll(); // Root keeps network.tick() running independently.
    void touch(deviceui::Touch event);
    void cancelTouch() { ui_.cancelTouch(); }
    bool render(std::uint16_t* pixels, std::size_t capacity);
private:
    setupui::Model model();
    net::NetworkAdapter& network_;
    setupui::Controller ui_;
    setupui::AP accessPoints_[setupui::kMaxAccessPoints]{};
    const char* notice_ = nullptr; // Static text only, never credential-derived.
    bool sawScanBusy_ = false;
};

} // namespace digivice
