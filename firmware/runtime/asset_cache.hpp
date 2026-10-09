#pragma once
#include <cstddef>
#include <cstdint>

namespace digivice::assets {
constexpr std::size_t kChunkBytes = 4096;
constexpr std::size_t kMaximumBlobBytes = 128 * 1024;
constexpr std::size_t kSlotBytes = kMaximumBlobBytes + 2 * kChunkBytes;
constexpr std::size_t kSlotCount = 5; // Four working assets and one replacement.
constexpr std::size_t kStorageBytes = kSlotBytes * kSlotCount;
enum class Kind : std::uint8_t { Sprite = 1, Background = 2 };
struct Spec {
    char id[48]{};
    std::uint32_t version = 0;
    std::uint32_t bytes = 0;
    std::uint8_t sha256[32]{};
    Kind kind = Kind::Sprite;
    std::uint16_t width = 0, height = 0;
};
bool validSpec(const Spec& spec);
bool sameSpec(const Spec& a, const Spec& b);
// NOR semantics: erase to FF in aligned sectors, program only 1->0. Errors may
// mean a partial operation. Implementations must not allocate a full blob.
class Storage {
public:
    virtual ~Storage() = default;
    virtual bool read(std::size_t offset, void* bytes, std::size_t length) = 0;
    virtual bool erase(std::size_t offset, std::size_t length) = 0;
    virtual bool program(std::size_t offset, const void* bytes, std::size_t length) = 0;
};
enum class Result { Ok, Missing, Invalid, Full, Io, Integrity, RecoveryRequired, WrongTransfer };
struct Record { Spec spec{}; std::uint32_t generation = 0; bool complete = false; bool present = false; };
struct Transfer { std::uint32_t token = 0, received = 0, total = 0; bool active = false; };

// Only supply Specs from a verified bounded catalog. This class enforces bytes,
// SHA-256, atomic activation and flash bounds, not publisher authentication.
// One owner/task serializes all calls. No heap allocation and no network calls.
class Cache {
public:
    explicit Cache(Storage& storage) : storage_(storage) {}
    Result boot();
    // Protect current partner, wild creature, scene and (optionally) next wild.
    // Protection is explicit each boot; the compiled fallback is outside cache.
    bool protect(const char* const* ids, std::size_t count);
    Result begin(const Spec& spec, std::uint32_t& token);
    // Exactly one aligned 4KiB range, except the final short range. A stale
    // completion cannot append to a newer transfer because tokens differ.
    Result append(std::uint32_t token, std::uint32_t offset,
                  const std::uint8_t* bytes, std::size_t length);
    Result finish(std::uint32_t token);
    Result read(const Spec& spec, std::size_t offset, void* bytes, std::size_t length);
    bool contains(const Spec& spec) const;
    Transfer transfer() const { return transfer_; }
    const Record& record(std::size_t index) const { return records_[index < kSlotCount ? index : 0]; }
    bool recoveryRequired() const { return recovery_; }
private:
    Storage& storage_;
    Record records_[kSlotCount]{};
    char protected_[4][48]{};
    std::size_t protectedCount_ = 0, stage_ = kSlotCount;
    Transfer transfer_{};
    std::uint32_t tokenCounter_ = 0, generation_ = 0;
    bool recovery_ = false, restartStage_ = false;
    bool isProtected(const char* id) const;
    Result digest(std::size_t slot, const Spec& spec);
    Result failIo();
};
const char* resultName(Result result);
} // namespace digivice::assets
