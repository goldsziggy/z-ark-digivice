#pragma once
#include "../runtime/asset_cache.hpp"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <atomic>

namespace digivice::assets {
enum class DownloadPhase : std::uint8_t { Disabled, Idle, Queued, Catalog, Downloading, Complete, Failed, Cancelled };
enum class DownloadError : std::uint8_t { None, Config, Network, Catalog, Signature, NotListed, Rollback, Cache, Cancelled };
struct DownloadStatus {
    DownloadPhase phase = DownloadPhase::Disabled;
    DownloadError error = DownloadError::None;
    Result cacheResult = Result::Ok;
    Spec spec{};
    std::uint32_t generation = 0, received = 0, total = 0;
    bool busy = false;
};

// Application-lifetime object. Public calls belong to one owner task. begin()
// requires a successfully booted Cache; after success, never call that Cache
// directly. Only this worker and the nonblocking read accessors serialize it.
// No task is deleted on timeout/cancellation. The fixed development key is
// PUBLIC: this feature is compile-time disabled unless explicitly opted in.
class DeviceAssetsClient {
public:
    DeviceAssetsClient() = default;
    DeviceAssetsClient(const DeviceAssetsClient&) = delete;
    DeviceAssetsClient& operator=(const DeviceAssetsClient&) = delete;
    static bool developmentAssetsEnabled();
    esp_err_t begin(Cache* cache);
    // Origin is copied; no credentials are accepted. Endpoint validation uses
    // the same net::validateEndpoint plus compile-time private-HTTP permission.
    // Protects requested ID as well as the supplied IDs, at most four total.
    // Busy requests are refused; no automatic downloads or accumulating queue.
    bool request(const char* origin, bool allowPrivateHttp, const char* id,
                 const char* const* protectedIds = nullptr, std::size_t count = 0);
    void cancel(); // Reject late network replies; committed chunks remain resumable.
    // Owner-only shutdown barrier. Pausing refuses new downloads AND reads;
    // cancellation never closes another task's socket/file or discards its job.
    // Keep paused while touching backing storage outside this client.
    void pause(bool paused);
    bool paused() const { return paused_; } // Owner-task state, for temporary read-only barriers.
    bool quiescent() const; // Worker has returned, released locks and cleaned HTTP.
    void tick(); // Drain fixed progress queue without waiting.
    DownloadStatus status() const;
    bool cachedSpec(const char* id, Spec& result); // false = busy, missing or disabled.
    bool tryRead(const Spec& spec, std::size_t offset, void* bytes, std::size_t length, Result& result);
private:
    DownloadStatus status_{};
    bool paused_ = false;
#if CONFIG_DIGIVICE_DEVELOPMENT_ASSETS
    struct Job { std::uint32_t generation; char origin[193]; char id[48]; char protectedIds[4][48]; std::uint8_t protectedCount; };
    static void worker(void* context);
    void run(const Job& job);
    void publish(const Job& job, DownloadPhase phase, DownloadError error = DownloadError::None, Result cacheResult = Result::Ok);
    bool current(const Job& job) const { return generation_.load() == job.generation; }
    Cache* cache_ = nullptr;
    QueueHandle_t jobs_ = nullptr, updates_ = nullptr;
    SemaphoreHandle_t cacheMutex_ = nullptr;
    StaticQueue_t jobQueue_{}, updateQueue_{};
    StaticSemaphore_t mutexStorage_{};
    std::uint8_t jobStorage_[sizeof(Job)]{}, updateStorage_[sizeof(DownloadStatus)]{};
    TaskHandle_t worker_ = nullptr;
    std::atomic<std::uint32_t> generation_{0};
    std::atomic<bool> pending_{false};
    char envelope_[8193]{}, payload_[6145]{};
    std::uint8_t chunk_[kChunkBytes]{};
    Spec catalog_[24]{}, selected_{};
    std::size_t catalogCount_ = 0;
    std::uint32_t catalogRelease_ = 0, highestRelease_ = 0, received_ = 0;
    char catalogOrigin_[193]{};
#endif
};
const char* downloadPhaseName(DownloadPhase phase);
const char* downloadErrorName(DownloadError error);
} // namespace digivice::assets
