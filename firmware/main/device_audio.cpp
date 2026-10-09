#include "device_audio.hpp"

#include "board_hal.hpp"
#include "esp_timer.h"
#include "sdkconfig.h"

#include <algorithm>
#include <cstring>

namespace digivice::device {
namespace {
std::uint64_t nowMs() { return static_cast<std::uint64_t>(esp_timer_get_time()/1000); }
constexpr unsigned frames = 256, descriptors = 3;
}
esp_err_t Audio::begin() {
#if !defined(CONFIG_DIGIVICE_AUDIO_MOTION) || !CONFIG_DIGIVICE_AUDIO_MOTION
    error_ = ESP_ERR_NOT_SUPPORTED; return error_;
#else
    if (ready_) return ESP_OK;
    if (channel_ || queue_ || board::selectedProfile().id != board::ProfileId::Waveshare146) {
        error_ = ESP_ERR_INVALID_STATE; return error_;
    }
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel.dma_desc_num = descriptors;
    channel.dma_frame_num = frames;
    channel.auto_clear = true;
    error_ = i2s_new_channel(&channel, &channel_, nullptr);
    if (error_ != ESP_OK) return error_;
    i2s_std_config_t config{};
    config.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kAudioSampleRate);
    config.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    // Waveshare pinned PCM5101.h: SCLK48/LCLK38/DOUT47, no MCLK or RX.
    // Duplicate mono into both slots so the DAC output wiring is immaterial.
    config.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    config.gpio_cfg.bclk = GPIO_NUM_48;
    config.gpio_cfg.ws = GPIO_NUM_38;
    config.gpio_cfg.dout = GPIO_NUM_47;
    config.gpio_cfg.din = I2S_GPIO_UNUSED;
    error_ = i2s_channel_init_std_mode(channel_, &config);
    if (error_ == ESP_OK) {
        queue_ = xQueueCreate(4, sizeof(Request));
        if (!queue_) error_ = ESP_ERR_NO_MEM;
    }
    if (error_ == ESP_OK && xTaskCreate(taskEntry, "digivice-audio", 4096, this, 3, &task_) != pdPASS)
        error_ = ESP_ERR_NO_MEM;
    if (error_ != ESP_OK) {
        if (queue_) vQueueDelete(queue_);
        queue_ = nullptr;
        (void)i2s_del_channel(channel_); channel_ = nullptr;
        return error_;
    }
    ready_ = true;
    return ESP_OK;
#endif
}
bool Audio::play(AudioCue cue) {
    const auto index = static_cast<unsigned>(cue);
    if (!ready_ || stopRequested() || index >= static_cast<unsigned>(AudioCue::Count)) return false;
    const auto now = nowMs();
    const unsigned cooldown = cuePriority(cue) >= 4 ? 400 : 80;
    if (lastCueMs_[index] && now-lastCueMs_[index] < cooldown) return false;
    const Request request{cue, now, epoch_.load()};
    // Outcomes flush queued navigation. Never wait on the gameplay task.
    if (cuePriority(cue) >= 4) xQueueReset(queue_);
    if (xQueueSend(queue_, &request, 0) != pdTRUE) return false;
    lastCueMs_[index] = now;
    return true;
}
void Audio::setMuted(bool muted) {
    muted_ = muted;
    if (muted) { ++epoch_; if (queue_) xQueueReset(queue_); }
}
void Audio::setVolume(std::uint8_t percent) {
    volume_ = std::min(static_cast<unsigned>(percent), 50U);
    if (!volume_) { ++epoch_; if (queue_) xQueueReset(queue_); }
}
void Audio::pause(bool paused) {
    paused_ = paused;
    if (paused) { ++epoch_; if (queue_) xQueueReset(queue_); }
}
void Audio::taskEntry(void* context) { static_cast<Audio*>(context)->run(); }
void Audio::run() {
    CueSynth synth;
    std::int16_t pcm[frames*2]{};
    bool enabled = false;
    unsigned priority = 0, tail = 0;
    std::uint32_t activeEpoch = 0;
    for (;;) {
        Request request{};
        const bool received = xQueueReceive(queue_, &request, enabled ? 0 : pdMS_TO_TICKS(20)) == pdTRUE;
        if (stopRequested() || (enabled && activeEpoch != epoch_.load())) {
            synth = {}; tail = 0;
        } else if (received && request.epoch == epoch_.load() && nowMs()-request.createdMs <= 350 &&
                   (synth.finished() || cuePriority(request.cue) >= priority)) {
            synth.start(request.cue); priority = cuePriority(request.cue);
            tail = descriptors+1; activeEpoch = request.epoch;
        }
        if (synth.finished() && tail == 0) {
            if (enabled) {
                const auto error = i2s_channel_disable(channel_);
                if (error != ESP_OK) {
                    error_ = error; ready_ = false;
                    vTaskDelay(pdMS_TO_TICKS(20));
                    continue; // Never acknowledge shutdown while I2S may run.
                }
                enabled = false;
            }
            active_ = false;
            settledEpoch_ = epoch_.load();
            continue;
        }
        active_ = true;
        if (!enabled) {
            // Refill every descriptor before restart; a mute in the middle of
            // a cue must not replay old DMA samples when sound is enabled.
            std::memset(pcm, 0, sizeof(pcm));
            esp_err_t error = ESP_OK;
            for (unsigned i = 0; i < descriptors; ++i) {
                std::size_t loaded = 0;
                error = i2s_channel_preload_data(channel_, pcm, sizeof(pcm), &loaded);
                if (error != ESP_OK || loaded != sizeof(pcm)) {
                    if (error == ESP_OK) error = ESP_ERR_INVALID_SIZE;
                    break;
                }
            }
            if (error == ESP_OK) error = i2s_channel_enable(channel_);
            if (error != ESP_OK) {
                error_ = error; ready_ = false; synth = {}; tail = 0;
                xQueueReset(queue_); continue;
            }
            enabled = true;
        }
        const bool wasFinished = synth.finished();
        for (unsigned i = 0; i < frames; ++i) pcm[i*2] = pcm[i*2+1] = synth.sample(volume_.load());
        if (wasFinished && tail) --tail;
        std::size_t written = 0;
        const auto error = i2s_channel_write(channel_, pcm, sizeof(pcm), &written, 30);
        if (error != ESP_OK || written != sizeof(pcm)) {
            error_ = error == ESP_OK ? ESP_ERR_INVALID_SIZE : error;
            ready_ = false; synth = {}; tail = 0; xQueueReset(queue_);
        }
    }
}
} // namespace digivice::device
