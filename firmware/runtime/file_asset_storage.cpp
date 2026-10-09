#include "file_asset_storage.hpp"

#include <algorithm>
#include <cstring>

namespace digivice::assets {
namespace {
void put32(std::uint8_t* p, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = std::uint8_t(value >> (8 * i));
}
void ownershipHeader(std::uint8_t* bytes) {
    std::memset(bytes, 255, kFileHeaderBytes);
    constexpr char magic[8]{'D','V','A','S','S','E','T','1'};
    std::memcpy(bytes, magic, sizeof(magic));
    put32(bytes + 8, 1); // File-envelope format, independent of game rules.
    put32(bytes + 12, std::uint32_t(kFileHeaderBytes));
    put32(bytes + 16, std::uint32_t(kStorageBytes));
    put32(bytes + 20, std::uint32_t(kChunkBytes));
    put32(bytes + 24, std::uint32_t(kSlotBytes));
    put32(bytes + 28, std::uint32_t(kSlotCount));
    // The entire header is deterministic and compared byte-for-byte on open.
    // Unknown versions/layouts/reserved fields fail closed without rewriting.
}
}

bool FileAssetStorage::fail(const char* diagnostic) {
    ready_ = false;
    diagnostic_ = diagnostic;
    return false;
}
void FileAssetStorage::end() {
    ready_ = false;
    diagnostic_ = "cache file closed";
}
bool FileAssetStorage::checkSize() {
    std::size_t bytes = 0;
    if (!io_.size(bytes)) return fail("cache file unavailable; recovery required");
    if (bytes != kFileStorageBytes) return fail("cache file size changed; file preserved");
    return true;
}
bool FileAssetStorage::fillErased(std::size_t offset, std::size_t length) {
    std::uint8_t erased[kFileIoBytes];
    std::memset(erased, 255, sizeof(erased));
    while (length) {
        const auto n = std::min(length, sizeof(erased));
        if (!io_.write(offset, erased, n)) return fail("cache file write failed; recovery required");
        offset += n;
        length -= n;
    }
    return true;
}
bool FileAssetStorage::syncAndCheck() {
    if (!io_.sync()) return fail("cache file sync failed; recovery required");
    return checkSize();
}
bool FileAssetStorage::begin(bool newlyCreated) {
    ready_ = false;
    std::size_t bytes = 0;
    if (!io_.size(bytes)) return fail("cache file unavailable; recovery required");
    std::uint8_t expected[kFileHeaderBytes];
    ownershipHeader(expected);
    if (newlyCreated) {
        if (bytes != 0) return fail("new cache file was not empty; file preserved");
        // Do not use truncate: newly allocated filesystem bytes are not NOR FF.
        // Leave partial creation for explicit recovery; never delete/recreate it.
        if (!fillErased(0, kFileStorageBytes) || !syncAndCheck()) return false;
        if (!io_.write(0, expected, sizeof(expected)))
            return fail("cache ownership write failed; file preserved");
        if (!syncAndCheck()) return false;
    } else if (bytes != kFileStorageBytes) {
        return fail("existing cache file has wrong size; file preserved");
    }
    std::uint8_t actual[kFileHeaderBytes];
    if (!io_.read(0, actual, sizeof(actual))) return fail("cache ownership read failed; recovery required");
    if (std::memcmp(expected, actual, sizeof(actual)) != 0)
        return fail("foreign, incomplete or damaged cache header; file preserved");
    ready_ = true;
    diagnostic_ = "cache file ready";
    return true;
}
bool FileAssetStorage::checkRange(std::size_t offset, std::size_t length, const void* bytes) {
    if (!ready_) return false;
    if (offset > kStorageBytes || length > kStorageBytes - offset || (!bytes && length)) {
        diagnostic_ = "invalid cache file range";
        return false;
    }
    return checkSize();
}
bool FileAssetStorage::read(std::size_t offset, void* bytes, std::size_t length) {
    if (!checkRange(offset, length, bytes)) return false;
    auto* out = static_cast<std::uint8_t*>(bytes);
    for (std::size_t done = 0; done < length;) {
        const auto n = std::min(kFileIoBytes, length - done);
        if (!io_.read(kFileHeaderBytes + offset + done, out + done, n))
            return fail("cache file read failed; recovery required");
        done += n;
    }
    diagnostic_ = "cache file ready";
    return true;
}
bool FileAssetStorage::erase(std::size_t offset, std::size_t length) {
    if (!ready_) return false;
    if (!length || offset % kChunkBytes || length % kChunkBytes) {
        diagnostic_ = "cache erase must cover aligned sectors";
        return false;
    }
    // A nonnull sentinel satisfies range checking; erase never dereferences it.
    if (!checkRange(offset, length, this)) return false;
    if (!fillErased(kFileHeaderBytes + offset, length) || !syncAndCheck()) return false;
    diagnostic_ = "cache file ready";
    return true;
}
bool FileAssetStorage::program(std::size_t offset, const void* bytes, std::size_t length) {
    if (!checkRange(offset, length, bytes)) return false;
    const auto* source = static_cast<const std::uint8_t*>(bytes);
    std::uint8_t old[kFileIoBytes];
    // Validate the entire request before changing any byte, including requests
    // crossing scratch boundaries. A caller cannot use this as arbitrary pwrite.
    for (std::size_t done = 0; done < length;) {
        const auto n = std::min(sizeof(old), length - done);
        if (!io_.read(kFileHeaderBytes + offset + done, old, n))
            return fail("cache preflight read failed; recovery required");
        for (std::size_t i = 0; i < n; ++i) {
            if ((old[i] & source[done + i]) != source[done + i]) {
                diagnostic_ = "cache program attempted zero-to-one transition";
                return false;
            }
        }
        done += n;
    }
    for (std::size_t done = 0; done < length;) {
        const auto n = std::min(kFileIoBytes, length - done);
        if (!io_.write(kFileHeaderBytes + offset + done, source + done, n))
            return fail("cache program failed; recovery required");
        done += n;
    }
    // Cache calls program(data), program(journal), then program(commit). Each
    // successful call crosses its own fsync boundary before the next can run.
    if (length && !syncAndCheck()) return false;
    diagnostic_ = "cache file ready";
    return true;
}
} // namespace digivice::assets
