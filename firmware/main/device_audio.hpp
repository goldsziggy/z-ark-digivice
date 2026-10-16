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

enum class PlayResult : std::uint8_t { Queued, NotReady, Muted, Off, Paused, Cooldown, QueueFull, InvalidCue };
struct BuzzerDiagnostics {
    std::uint32_t queued, started, backgroundStarted, expired, busyRetries, onWrites, offWrites;
    bool outputKnown, outputOn;
};

// App-lifetime object. begin/play/preferences are called by the runtime owner;
// only its worker uses the I2S channel after begin. No microphone is created.
class Audio {
public:
    esp_err_t begin();
    bool play(AudioCue cue);
    PlayResult lastPlayResult() const { return lastPlayResult_; }
    const char* playResultText() const;
    BuzzerDiagnostics buzzerDiagnostics() const;
    // Apply immediately in RAM, even if persistence fails. Report a failed save
    // to the user; a storage fault must not prevent muting the speaker.
    esp_err_t setMuted(bool muted);
    bool muted() const { return muted_.load(); }
    esp_err_t setVolume(std::uint8_t percent);
    unsigned volume() const { return volume_.load(); }
    esp_err_t setMusicEnabled(bool enabled);
    bool musicEnabled() const { return musicEnabled_.load(); }
    bool buzzerOnly() const { return buzzer_; }
    void setScreenBlanked(bool blanked) {
        if (!buzzer_) return;
        screenBlanked_ = blanked;
        if (blanked) { ++epoch_; if (queue_) xQueueReset(queue_); }
    }
    bool preferencesWritable() const { return settings_.writable() && (!buzzer_ || rhythmSettings_.writable()); }
    esp_err_t preferencesError() const { return preferencesError_ != ESP_OK ? preferencesError_ : rhythmPreferencesError_; }
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
    void runBuzzer();
    esp_err_t persistPreferences();
    bool stopRequested() const { return paused_.load() || screenBlanked_.load() || muted_.load() || volume_.load() == 0; }
    bool buzzer_ = false;
    std::atomic<bool> screenBlanked_{false};
    QueueHandle_t queue_ = nullptr;
    TaskHandle_t task_ = nullptr;
    i2s_chan_handle_t channel_ = nullptr;
    sound::NvsSettings settings_;
    // Separate opt-in record: an old PCM music=true must not start buzzing.
    sound::NvsSettings rhythmSettings_;
    esp_err_t preferencesError_ = ESP_ERR_INVALID_STATE;
    esp_err_t rhythmPreferencesError_ = ESP_OK;
    PlayResult lastPlayResult_ = PlayResult::NotReady;
    std::atomic<std::uint32_t> buzzerQueued_{0}, buzzerStarted_{0}, backgroundStarted_{0},
        buzzerExpired_{0}, buzzerBusyRetries_{0}, buzzerOnWrites_{0}, buzzerOffWrites_{0};
    std::atomic<bool> buzzerOutputKnown_{false}, buzzerOutputOn_{false};
    std::atomic<bool> ready_{false}, active_{false}, effectsActive_{false}, muted_{false}, paused_{false}, musicEnabled_{false};
    std::atomic<MusicScene> scene_{MusicScene::Home};
    std::atomic<unsigned> volume_{15};
    std::atomic<std::uint32_t> epoch_{0}, settledEpoch_{0};
    std::atomic<esp_err_t> error_{ESP_ERR_INVALID_STATE};
    std::uint64_t lastCueMs_[static_cast<unsigned>(AudioCue::Count)]{};
};
} // namespace digivice::device
