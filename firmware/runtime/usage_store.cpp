#include "usage_store.hpp"
#include <cstring>

namespace digivice::usage {
namespace {
std::uint32_t crc(const std::uint8_t* bytes, std::size_t length) {
    std::uint32_t value = UINT32_MAX;
    for (std::size_t i = 0; i < length; ++i) {
        value ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit) value = (value >> 1) ^ (0xedb88320u & (0u - (value & 1u)));
    }
    return ~value;
}
void put(std::uint8_t* out, std::uint64_t value, unsigned length) {
    for (unsigned i = 0; i < length; ++i) out[i] = static_cast<std::uint8_t>(value >> (i * 8));
}
std::uint64_t get(const std::uint8_t* in, unsigned length) {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < length; ++i) value |= std::uint64_t(in[i]) << (i * 8);
    return value;
}
Record encode(std::uint64_t generation, std::uint64_t total) {
    Record record;
    std::memcpy(record.bytes, "DUSE", 4); record.bytes[4] = 1; record.bytes[6] = kRecordBytes;
    put(record.bytes + 8, generation, 8); put(record.bytes + 16, total, 8);
    put(record.bytes + 28, crc(record.bytes, 28), 4);
    return record;
}
bool decode(const Record& record, std::uint64_t& generation, std::uint64_t& total) {
    const auto* b = record.bytes;
    if (std::memcmp(b, "DUSE", 4) || b[4] != 1 || b[5] || b[6] != kRecordBytes || b[7] ||
        get(b + 24, 4) || get(b + 28, 4) != crc(b, 28)) return false;
    generation = get(b + 8, 8); total = get(b + 16, 8);
    return generation != 0;
}
}
bool Store::restore(std::uint64_t nowMs) {
    writable_ = false; active_ = -1; observed_ = 0;
    total_ = savedTotal_ = generation_ = 0; lastSaveMs_ = nowMs;
    bool damaged = false, valid[2]{};
    std::uint64_t generations[2]{}, totals[2]{};
    for (unsigned i = 0; i < 2; ++i) {
        Record record; const auto status = backend_.read(i, record);
        if (status == Read::Missing) continue;
        valid[i] = status == Read::Present && decode(record, generations[i], totals[i]);
        damaged |= !valid[i];
    }
    if (valid[0] && valid[1]) {
        const unsigned newer = generations[1] > generations[0] ? 1 : 0;
        damaged |= generations[0] == generations[1] ? totals[0] != totals[1] : totals[newer] < totals[1 - newer];
    }
    if (valid[0] || valid[1]) {
        active_ = valid[1] && (!valid[0] || generations[1] > generations[0]) ? 1 : 0;
        total_ = savedTotal_ = totals[active_]; generation_ = generations[active_];
    }
    if (damaged) { diagnostic_ = "step records preserved; storage recovery required"; return false; }
    writable_ = true;
    diagnostic_ = active_ < 0 ? "new physical step total; earlier simulated steps excluded" : "physical step total restored";
    return true;
}
std::uint32_t Store::observe(std::uint32_t bootSteps) {
    if (!writable_) return 0;
    if (bootSteps < observed_) { writable_ = false; diagnostic_ = "step sampler reset; reboot to recover"; return 0; }
    const auto delta = bootSteps - observed_;
    if (total_ > UINT64_MAX - delta) { writable_ = false; diagnostic_ = "lifetime step counter exhausted"; return 0; }
    observed_ = bootSteps; total_ += delta;
    return delta;
}
bool Store::checkpoint(std::uint64_t nowMs, bool force) {
    if (!writable_) return false;
    if (!dirty()) return true;
    if (!force && total_ - savedTotal_ < kCheckpointSteps && nowMs >= lastSaveMs_ && nowMs - lastSaveMs_ < kCheckpointMs) return true;
    if (generation_ == UINT64_MAX) { writable_ = false; diagnostic_ = "step record generation exhausted"; return false; }
    const auto candidate = encode(generation_ + 1, total_);
    const unsigned next = active_ == 0 ? 1 : 0;
    Record verified;
    if (!backend_.write(next, candidate) || backend_.read(next, verified) != Read::Present ||
        std::memcmp(candidate.bytes, verified.bytes, kRecordBytes)) {
        writable_ = false; diagnostic_ = "step checkpoint uncertain; records retained; reboot to recover"; return false;
    }
    active_ = static_cast<int>(next); ++generation_; savedTotal_ = total_; lastSaveMs_ = nowMs;
    diagnostic_ = "physical steps committed and read back";
    return true;
}
} // namespace digivice::usage
