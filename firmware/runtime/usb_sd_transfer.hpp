#pragma once

#include <cstddef>
#include <cstdint>

namespace digivice::assets {

// Owner-task-only, synchronous USB line protocol. The caller must exclude other
// card writers/renderers and supply the mounted card root; never mounts/formats.
// Every operation closes its descriptor before returning. Hashing cooperates
// after each <=512-byte block; false cancels without removing staged files.
class UsbSdTransfer {
public:
    static constexpr std::size_t kMaximumFileBytes = 2 * 1024 * 1024;
    static constexpr std::size_t kChunkBytes = 512;
    static constexpr std::size_t kResponseBytes = 384;
    using Cooperate = bool (*)(void*);
    explicit UsbSdTransfer(const char* root, Cooperate cooperate = nullptr, void* context = nullptr);
    bool handle(const char* line, char* response, std::size_t capacity);
    bool active() const { return active_; }
    bool quiescent() const { return !busy_; }
    bool pause(); // Logical close only; preserves owned partial files.
    static bool allowedName(const char* name);

private:
    struct Identity {
        char name[13]{};
        std::uint32_t size = 0;
        std::uint8_t sha[32]{};
    } identity_;
    char root_[96]{};
    Cooperate cooperate_ = nullptr;
    void* context_ = nullptr;
    bool active_ = false, busy_ = false, completed_ = false;
    std::uint32_t offset_ = 0;
    bool path(const char* name, char* out) const;
    bool cooperate() const;
    const char* loadMetadata(Identity& value) const;
    const char* createMetadata(const Identity& value) const;
    const char* hashFile(const char* name, std::uint32_t size, std::uint8_t* digest) const;
    const char* verify(const Identity& value) const;
    const char* stagedSize(std::uint32_t& size) const;
    const char* begin(const Identity& value, char* response, std::size_t capacity);
    const char* chunk(std::uint32_t offset, const std::uint8_t* data, std::size_t bytes);
    const char* finish();
    const char* cleanupMetadata(const Identity& value) const;
    static bool same(const Identity& a, const Identity& b);
};
} // namespace digivice::assets
