#pragma once

#include "audio_cues.hpp"
#include "audio_settings.hpp"
#include "music_synth.hpp"
#include "driver/i2s_std.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <atomic>

namespace digivice::device {

// App-lifetime object. begin/play/preferences are called by the runtime owner;
// only its worker uses the I2S channel after begin. No microphone is created.
class Audio {
public:
    esp_err_t begin();
    bool play(AudioCue cue);
    // Apply immediately in RAM, even if persistence fails. Report a failed save
    // to the user; a storage fault must not prevent muting the speaker.
    esp_err_t setMuted(bool muted);
    bool muted() const { return muted_.load(); }
    esp_err_t setVolume(std::uint8_t percent);
    unsigned volume() const { return volume_.load(); }
    esp_err_t setMusicEnabled(bool enabled);
    bool musicEnabled() const { return musicEnabled_.load(); }
    bool preferencesWritable() const { return settings_.writable(); }
    esp_err_t preferencesError() const { return preferencesError_; }
    void setMusicScene(MusicScene scene) { scene_ = scene; }
    bool effectsQuiescent() const {
        return !effectsActive_.load() && (!queue_ || uxQueueMessagesWaiting(queue_) == 0);
    }
    void pause(bool paused);
    bool quiescent() const {
        return (!task_ || settledEpoch_.load() == epoch_.load()) && !active_.load() &&
            (!queue_ || uxQueueMessagesWaiting(queue_) == 0);
    }
    bool ready() const { return ready_.load(); }
    esp_err_t lastError() const { return error_.load(); }
private:
    struct Request { AudioCue cue; std::uint64_t createdMs; std::uint32_t epoch; };
    static void taskEntry(void* context);
    void run();
    esp_err_t persistPreferences();
    bool stopRequested() const { return paused_.load() || muted_.load() || volume_.load() == 0; }
    QueueHandle_t queue_ = nullptr;
    TaskHandle_t task_ = nullptr;
    i2s_chan_handle_t channel_ = nullptr;
    sound::NvsSettings settings_;
    esp_err_t preferencesError_ = ESP_ERR_INVALID_STATE;
    std::atomic<bool> ready_{false}, active_{false}, effectsActive_{false}, muted_{false}, paused_{false}, musicEnabled_{false};
    std::atomic<MusicScene> scene_{MusicScene::Home};
    std::atomic<unsigned> volume_{15};
    std::atomic<std::uint32_t> epoch_{0}, settledEpoch_{0};
    std::atomic<esp_err_t> error_{ESP_ERR_INVALID_STATE};
    std::uint64_t lastCueMs_[static_cast<unsigned>(AudioCue::Count)]{};
};
} // namespace digivice::device
