#pragma once
#include "device_ui.hpp"
#include "network.hpp"
#include <cstddef>
#include <cstdint>

// Pure, single-owner setup UI. No allocations, SDK calls, logging or storage.
// The owner supplies public status and consumes proposals immediately. Password
// drafts are never exposed except by takeWifi(), which wipes the internal copy.
namespace digivice::setupui {
constexpr std::size_t kMaxAccessPoints = 16;
struct AP { char ssid[33]{}; std::int8_t rssi = 0; bool supported = false; };
struct Model {
    net::Status network{};
    bool ready = false, recovery = false, scanning = false;
    const AP* accessPoints = nullptr;
    std::size_t count = 0;
    const char* endpoint = nullptr;
    bool privateHttpAllowed = false; // Build permits the explicit opt-in.
    bool allowPrivateHttp = false;   // Current configuration opted in.
    bool clockReady = false, clockWaiting = false, clockFailed = false;
    const char* notice = nullptr;    // Static public text, never credentials.
};
enum class Intent : std::uint8_t { None, Scan, SaveWifi, SaveEndpoint, Retry, Close, Forget };
enum class Screen : std::uint8_t { Status, Networks, Ssid, Password, Endpoint, ForgetReview };
class Controller {
public:
    Controller() = default;
    ~Controller();
    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;
    void open();
    void close();
    bool active() const { return active_; }
    void update(const Model& model);
    Intent touch(const Model& model, deviceui::Touch event);
    void cancelTouch();
    bool render(const Model& model, std::uint16_t* pixels, std::size_t capacity) const;
    bool takeWifi(char (&ssid)[33], char (&password)[65]);
    bool takeEndpoint(char (&endpoint)[193], bool& allowPrivateHttp);
    Screen screen() const { return screen_; }
private:
    struct Button { std::int16_t x, y, w, h; int id; char label[33]; bool enabled; };
    std::size_t buttons(const Model& model, Button* output) const;
    int hit(const Model& model, int x, int y) const;
    Intent activate(const Model& model, int id);
    void changeScreen(Screen screen);
    void clearDrafts();
    void append(char character);
    void eraseCharacter();
    bool validWifi();
    char ssid_[33]{}, password_[65]{}, endpoint_[193]{}, error_[48]{};
    std::uint32_t context_ = 0;
    std::uint64_t downAt_ = 0, lastAt_ = 0;
    std::int16_t downX_ = 0, downY_ = 0;
    int downButton_ = 0;
    Screen screen_ = Screen::Status;
    Intent pending_ = Intent::None;
    std::uint8_t keyboardPage_ = 0, networkPage_ = 0;
    bool active_ = false, down_ = false, initialized_ = false, privateHttp_ = false;
};
} // namespace digivice::setupui
