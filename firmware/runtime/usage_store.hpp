#pragma once
#include <cstddef>
#include <cstdint>

namespace digivice::usage {
constexpr std::size_t kRecordBytes = 32;
constexpr std::uint32_t kCheckpointSteps = 64;
constexpr std::uint64_t kCheckpointMs = 30000;
struct Record { std::uint8_t bytes[kRecordBytes]{}; };
enum class Read { Missing, Present, Failed };
class Backend {
public:
    virtual ~Backend() = default;
    virtual Read read(unsigned slot, Record& record) = 0;
    virtual bool write(unsigned slot, const Record& record) = 0;
};
// Main-task owner. The sampler supplies a cumulative count starting at zero for
// this boot; pause/recenter never reset it. No historic simulated steps imported.
class Store {
public:
    explicit Store(Backend& backend) : backend_(backend) {}
    bool restore(std::uint64_t nowMs);
    std::uint32_t observe(std::uint32_t bootSteps);
    bool checkpoint(std::uint64_t nowMs, bool force = false);
    std::uint64_t total() const { return total_; }
    std::uint64_t savedTotal() const { return savedTotal_; }
    std::uint32_t session() const { return observed_; }
    bool writable() const { return writable_; }
    bool dirty() const { return total_ != savedTotal_; }
    const char* diagnostic() const { return diagnostic_; }
private:
    Backend& backend_;
    std::uint64_t total_ = 0, savedTotal_ = 0, generation_ = 0, lastSaveMs_ = 0;
    std::uint32_t observed_ = 0;
    int active_ = -1;
    bool writable_ = false;
    const char* diagnostic_ = "step storage not restored";
};
} // namespace digivice::usage
