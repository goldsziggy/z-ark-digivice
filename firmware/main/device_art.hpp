#pragma once

#include "device_ui.hpp"
#include "sprite.hpp"
#include "sprite_bounds.hpp"

namespace digivice::device {

// App-lifetime, main-task owner. prepare is synchronous and closes every file
// before returning. Call only with mounted/healthy SD, before rendering begins;
// do not retain views across another prepare with a different form or scene.
class Art {
public:
    deviceui::Artwork prepare(const deviceui::ArtRequest& request, bool storageReady);
    void pause(bool paused) { paused_ = paused; }
    bool quiescent() const { return !loading_; }
    const char* spriteDiagnostic() const { return spriteDiagnostic_; }
    const char* backgroundDiagnostic() const { return backgroundDiagnostic_; }
    std::size_t allocatedBytes() const;
    // Explicit retry after repairing/reinserting media. No automatic file scan.
    void retry();
private:
    void loadSprite(std::uint32_t formId);
    void loadBackground(int scene);
    std::uint16_t* spritePixels_ = nullptr;
    std::uint8_t* spriteMasks_ = nullptr;
    std::uint16_t* background_ = nullptr;
    sprite::Info spriteInfo_{};
    sprite::OpaqueBounds spriteBounds_[sprite::kAnimationCount]{};
    std::size_t spriteBytes_ = 0;
    std::uint32_t requestedForm_ = 0, loadedForm_ = 0;
    int requestedScene_ = -2, loadedScene_ = -1;
    bool paused_ = false, loading_ = false, previousStorageReady_ = false;
    const char* spriteDiagnostic_ = "no exact-form artwork requested";
    const char* backgroundDiagnostic_ = "no local scene requested";
};
} // namespace digivice::device
