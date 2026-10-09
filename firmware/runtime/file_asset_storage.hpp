#pragma once

#include "asset_cache.hpp"

namespace digivice::assets {
// The ownership header is outside Cache's logical offsets. It is written last
// when creating a file, so an interrupted initialization is never auto-reused.
constexpr std::size_t kFileHeaderBytes = 512;
constexpr std::size_t kFileStorageBytes = kFileHeaderBytes + kStorageBytes;
constexpr std::size_t kFileIoBytes = 512;

// The platform owns one regular, writable, exclusively held file descriptor.
// Read/write must complete exactly or return false; failures may change a prefix.
// sync means the platform's fsync equivalent, not a physical brownout guarantee.
// No resize, remove, format, path lookup or descriptor ownership is provided here.
class FileIo {
public:
    virtual ~FileIo() = default;
    virtual bool size(std::size_t& bytes) = 0;
    virtual bool read(std::size_t offset, void* bytes, std::size_t length) = 0;
    virtual bool write(std::size_t offset, const void* bytes, std::size_t length) = 0;
    virtual bool sync() = 0;
};

// One owner/task; fixed allocation. There is no automatic reset or fallback.
// On a failed I/O the file remains untouched by subsequent calls until an
// explicit begin after platform recovery. Cache::boot must then run again.
class FileAssetStorage final : public Storage {
public:
    explicit FileAssetStorage(FileIo& io) : io_(io) {}
    // true ONLY for an empty file just created with exclusive-create semantics.
    // false validates existing ownership/size without writing. Cache::boot owns
    // all cache payload integrity, torn-transfer recovery and safe replacement.
    bool begin(bool newlyCreated);
    void end();
    bool ready() const { return ready_; }
    const char* diagnostic() const { return diagnostic_; }
    bool read(std::size_t offset, void* bytes, std::size_t length) override;
    bool erase(std::size_t offset, std::size_t length) override;
    bool program(std::size_t offset, const void* bytes, std::size_t length) override;
private:
    FileIo& io_;
    bool ready_ = false;
    const char* diagnostic_ = "cache file not opened";
    bool fail(const char* diagnostic);
    bool checkSize();
    bool checkRange(std::size_t offset, std::size_t length, const void* bytes);
    bool fillErased(std::size_t physicalOffset, std::size_t length);
    bool syncAndCheck();
};
} // namespace digivice::assets
