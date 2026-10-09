#include "usb_sd_transfer.hpp"

#include "mbedtls/sha256.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace digivice::assets {
namespace {
constexpr char kMetadata[] = "DVXFER.MET";
constexpr char kTemporary[] = "DVXFER.TMP";
constexpr std::size_t kPath = 112, kRecord = 128;
constexpr unsigned kInterruptedRetries = 3;

class File {
public:
    explicit File(int fd) : fd_(fd) {}
    ~File() { if (fd_ >= 0) ::close(fd_); }
    int get() const { return fd_; }
    bool close() { const int fd = fd_; fd_ = -1; return fd >= 0 && ::close(fd) == 0; }
private:
    int fd_;
};

int openFile(const char* path, int flags) {
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    return ::open(path, flags, 0600);
}
const char* inspect(const char* path, std::uint32_t& size) {
    struct stat st{};
#if defined(ESP_PLATFORM)
    const int result = ::stat(path, &st); // FAT has no symlinks.
#else
    const int result = ::lstat(path, &st);
#endif
    if (result != 0) return errno == ENOENT ? "MISSING" : "IO";
    if (!S_ISREG(st.st_mode) || st.st_size < 0 ||
        static_cast<std::uint64_t>(st.st_size) > UsbSdTransfer::kMaximumFileBytes) return "CONFLICT";
    size = static_cast<std::uint32_t>(st.st_size);
    return nullptr;
}
bool descriptorSize(int fd, std::uint32_t expected) {
    struct stat st{};
    return ::fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size == expected;
}
bool ioExact(int fd, void* bytes, std::size_t length, bool write) {
    auto* data = static_cast<std::uint8_t*>(bytes);
    unsigned retries = 0;
    while (length) {
        const auto count = write ? ::write(fd, data, length) : ::read(fd, data, length);
        if (count < 0 && errno == EINTR && retries++ < kInterruptedRetries) continue;
        if (count <= 0 || static_cast<std::size_t>(count) > length) return false;
        data += count; length -= static_cast<std::size_t>(count);
    }
    return true;
}
bool digest(const void* bytes, std::size_t size, std::uint8_t* result) {
    return mbedtls_sha256(static_cast<const unsigned char*>(bytes), size, result, 0) == 0;
}
int nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}
bool unhex(const char* text, std::uint8_t* bytes, std::size_t length) {
    if (std::strlen(text) != length * 2) return false;
    for (std::size_t i = 0; i < length; ++i) {
        const int high = nibble(text[i * 2]), low = nibble(text[i * 2 + 1]);
        if (high < 0 || low < 0) return false;
        bytes[i] = static_cast<std::uint8_t>(high * 16 + low);
    }
    return true;
}
void hex(const std::uint8_t* bytes, char* out) {
    constexpr char digits[] = "0123456789abcdef";
    for (unsigned i = 0; i < 32; ++i) { out[i * 2] = digits[bytes[i] >> 4]; out[i * 2 + 1] = digits[bytes[i] & 15]; }
    out[64] = 0;
}
bool number(const char* text, std::uint32_t& value) {
    if (!*text) return false;
    value = 0;
    for (; *text; ++text) {
        if (*text < '0' || *text > '9') return false;
        const auto digit = static_cast<unsigned>(*text - '0');
        if (value > (UsbSdTransfer::kMaximumFileBytes - digit) / 10) return false;
        value = value * 10 + digit;
    }
    return true;
}
void identityResponse(char* response, std::size_t capacity, const char* verb,
                      const char* name, std::uint32_t size, const std::uint8_t* sha) {
    char value[65]; hex(sha, value);
    std::snprintf(response, capacity, "SDPUT %s name=%s size=%u sha256=%s", verb, name, static_cast<unsigned>(size), value);
}
} // namespace

UsbSdTransfer::UsbSdTransfer(const char* root, Cooperate cooperate, void* context)
    : cooperate_(cooperate), context_(context) {
    if (root && std::strlen(root) < sizeof(root_) && *root) std::strcpy(root_, root);
}
bool UsbSdTransfer::pause() {
    if (busy_) return false;
    active_ = false; return true;
}
bool UsbSdTransfer::cooperate() const { return !cooperate_ || cooperate_(context_); }
bool UsbSdTransfer::path(const char* name, char* out) const {
    if (!*root_) return false;
    const int length = std::snprintf(out, kPath, "%s/%s", root_, name);
    return length > 0 && static_cast<std::size_t>(length) < kPath;
}
bool UsbSdTransfer::allowedName(const char* name) {
    if (!name) return false;
    const char* const fixed[] = {"MEADOW.JPG", "FOREST.JPG", "BEACH.JPG", "RUINS.JPG", "CAVERN.JPG", "SNOW.JPG", "VOLCANIC.JPG", "DIGITAL.JPG", "INDEX.JSON", "ATTRIB.TXT"};
    for (auto* item : fixed) if (std::strcmp(name, item) == 0) return true;
    if (std::strlen(name) != 12 || std::strncmp(name, "DSF", 3) || std::strcmp(name + 8, ".DVA")) return false;
    unsigned form = 0;
    for (unsigned i = 3; i < 8; ++i) { if (name[i] < '0' || name[i] > '9') return false; form = form * 10 + static_cast<unsigned>(name[i] - '0'); }
    return form >= 1 && form <= 512;
}
bool UsbSdTransfer::same(const Identity& a, const Identity& b) {
    return a.size == b.size && std::strcmp(a.name, b.name) == 0 && std::memcmp(a.sha, b.sha, sizeof(a.sha)) == 0;
}
const char* UsbSdTransfer::loadMetadata(Identity& value) const {
    char file[kPath]; if (!path(kMetadata, file)) return "PATH";
    std::uint32_t size = 0;
    if (auto* error = inspect(file, size)) return error;
    if (size != kRecord) return "STAGING_CONFLICT";
    std::uint8_t record[kRecord]{}, check[32];
    File fd(openFile(file, O_RDONLY));
    if (fd.get() < 0 || !descriptorSize(fd.get(), kRecord) || !ioExact(fd.get(), record, sizeof(record), false) || !fd.close()) return "IO";
    if (std::memcmp(record, "DVUSB001", 8) || !digest(record, 96, check) || std::memcmp(record + 96, check, 32)) return "STAGING_CONFLICT";
    for (unsigned i = 57; i < 96; ++i) if (record[i]) return "STAGING_CONFLICT";
    std::memcpy(value.name, record + 12, 13);
    if (!std::memchr(value.name, 0, 13) || !allowedName(value.name)) return "STAGING_CONFLICT";
    value.size = record[8] | static_cast<std::uint32_t>(record[9]) << 8 | static_cast<std::uint32_t>(record[10]) << 16 | static_cast<std::uint32_t>(record[11]) << 24;
    if (!value.size || value.size > kMaximumFileBytes) return "STAGING_CONFLICT";
    std::memcpy(value.sha, record + 25, 32);
    return nullptr;
}
const char* UsbSdTransfer::createMetadata(const Identity& value) const {
    std::uint8_t record[kRecord]{};
    std::memcpy(record, "DVUSB001", 8);
    for (unsigned i = 0; i < 4; ++i) record[8 + i] = static_cast<std::uint8_t>(value.size >> (i * 8));
    std::memcpy(record + 12, value.name, std::strlen(value.name));
    std::memcpy(record + 25, value.sha, 32);
    if (!digest(record, 96, record + 96)) return "HASH";
    char file[kPath]; if (!path(kMetadata, file)) return "PATH";
    File fd(openFile(file, O_WRONLY | O_CREAT | O_EXCL));
    if (fd.get() < 0) return errno == EEXIST ? "STAGING_CONFLICT" : "IO";
    // A torn metadata record is retained and rejected; never guess ownership.
    if (!ioExact(fd.get(), record, sizeof(record), true) || ::fsync(fd.get()) != 0 || !fd.close()) return "IO";
    return nullptr;
}
const char* UsbSdTransfer::hashFile(const char* name, std::uint32_t size, std::uint8_t* result) const {
    char file[kPath]; if (!path(name, file)) return "PATH";
    std::uint32_t actual = 0;
    if (auto* error = inspect(file, actual)) return error;
    if (actual != size) return "CONFLICT";
    File fd(openFile(file, O_RDONLY));
    if (fd.get() < 0 || !descriptorSize(fd.get(), size)) return "IO";
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    const char* error = mbedtls_sha256_starts(&sha, 0) == 0 ? nullptr : "HASH";
    std::uint8_t buffer[kChunkBytes];
    std::uint32_t remaining = size;
    while (!error && remaining) {
        const std::size_t count = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        if (!ioExact(fd.get(), buffer, count, false)) error = "IO";
        else if (mbedtls_sha256_update(&sha, buffer, count) != 0) error = "HASH";
        else if (!cooperate()) error = "INTERRUPTED";
        remaining -= static_cast<std::uint32_t>(count);
    }
    if (!error && mbedtls_sha256_finish(&sha, result) != 0) error = "HASH";
    mbedtls_sha256_free(&sha);
    if (!error && !descriptorSize(fd.get(), size)) error = "IO";
    if (!fd.close() && !error) error = "IO";
    return error;
}
const char* UsbSdTransfer::verify(const Identity& value) const {
    std::uint8_t actual[32];
    if (auto* error = hashFile(value.name, value.size, actual)) return error;
    return std::memcmp(actual, value.sha, 32) == 0 ? nullptr : "CONFLICT";
}
const char* UsbSdTransfer::stagedSize(std::uint32_t& size) const {
    char file[kPath]; return path(kTemporary, file) ? inspect(file, size) : "PATH";
}
const char* UsbSdTransfer::cleanupMetadata(const Identity& value) const {
    Identity stored;
    if (auto* error = loadMetadata(stored)) return std::strcmp(error, "MISSING") == 0 ? nullptr : error;
    if (!same(value, stored)) return "STAGING_CONFLICT";
    char file[kPath]; if (!path(kMetadata, file)) return "PATH";
    return ::unlink(file) == 0 ? nullptr : "IO";
}
const char* UsbSdTransfer::begin(const Identity& value, char* response, std::size_t capacity) {
    if (active_ && !same(identity_, value)) return "BUSY";
    const char* existing = verify(value);
    if (!existing) {
        // A prior publish may have completed before a reply or metadata cleanup.
        // Only matching metadata with no remaining temp is cleaned here.
        Identity stored;
        const char* metadata = loadMetadata(stored);
        if (!metadata && same(stored, value)) {
            std::uint32_t bytes = 0;
            const char* staged = stagedSize(bytes);
            if (staged && std::strcmp(staged, "MISSING") == 0) {
                if (auto* error = cleanupMetadata(value)) return error;
            }
        }
        identity_ = value; offset_ = value.size; active_ = false; completed_ = true;
        identityResponse(response, capacity, "EXISTS", value.name, value.size, value.sha);
        return nullptr;
    }
    if (std::strcmp(existing, "MISSING") != 0) return existing;
    Identity stored;
    const char* metadata = loadMetadata(stored);
    std::uint32_t bytes = 0;
    const char* staged = stagedSize(bytes);
    if (metadata && std::strcmp(metadata, "MISSING") != 0) return metadata;
    if (staged && std::strcmp(staged, "MISSING") != 0) return staged;
    if (!metadata && !same(stored, value)) return "PENDING_OTHER";
    if (metadata) {
        if (!staged) return "STAGING_CONFLICT"; // Orphan/foreign temp stays untouched.
        if (auto* error = createMetadata(value)) return error;
    }
    if (staged) {
        char file[kPath]; if (!path(kTemporary, file)) return "PATH";
        File fd(openFile(file, O_WRONLY | O_CREAT | O_EXCL));
        if (fd.get() < 0) return errno == EEXIST ? "STAGING_CONFLICT" : "IO";
        if (::fsync(fd.get()) != 0 || !fd.close()) return "IO";
        bytes = 0;
    }
    if (bytes > value.size) return "STAGING_CONFLICT";
    std::uint8_t prefix[32];
    if (auto* error = hashFile(kTemporary, bytes, prefix)) return error;
    identity_ = value; offset_ = bytes; active_ = true; completed_ = false;
    char expectedHex[65], prefixHex[65]; hex(value.sha, expectedHex); hex(prefix, prefixHex);
    std::snprintf(response, capacity, "SDPUT READY name=%s size=%u offset=%u sha256=%s prefixSha256=%s",
        value.name, static_cast<unsigned>(value.size), static_cast<unsigned>(offset_), expectedHex, prefixHex);
    return nullptr;
}
const char* UsbSdTransfer::chunk(std::uint32_t offset, const std::uint8_t* data, std::size_t bytes) {
    if (!active_) return "NO_SESSION";
    if (!bytes || bytes > kChunkBytes || offset > identity_.size || bytes > identity_.size - offset) return "BOUNDS";
    Identity stored;
    if (auto* error = loadMetadata(stored)) return error;
    if (!same(identity_, stored)) return "STAGING_CONFLICT";
    std::uint32_t current = 0;
    if (auto* error = stagedSize(current)) return error;
    if (current > identity_.size || offset > current || (offset < current && bytes > current - offset)) return "OFFSET";
    char file[kPath]; if (!path(kTemporary, file)) return "PATH";
    File fd(openFile(file, O_RDWR));
    if (fd.get() < 0 || !descriptorSize(fd.get(), current) || ::lseek(fd.get(), offset, SEEK_SET) != offset) return "IO";
    if (offset < current) {
        std::uint8_t previous[kChunkBytes];
        if (!ioExact(fd.get(), previous, bytes, false)) return "IO";
        if (std::memcmp(previous, data, bytes) != 0) return "CHUNK_CONFLICT";
    } else {
        // Do not use pwrite: pinned IDF5.3.6 leaks its VFS lock on ENOSPC.
        if (!ioExact(fd.get(), const_cast<std::uint8_t*>(data), bytes, true)) return "IO";
        current += static_cast<std::uint32_t>(bytes);
    }
    // Retries also synchronize before acknowledging an already present chunk.
    if (::fsync(fd.get()) != 0 || !fd.close()) return "IO";
    offset_ = current; return nullptr;
}
const char* UsbSdTransfer::finish() {
    if (!active_ && !completed_) return "NO_SESSION";
    const char* existing = verify(identity_);
    if (!existing) {
        // Idempotent lost-reply retry. Never remove an unexpected leftover temp.
        std::uint32_t unused = 0;
        const char* staged = stagedSize(unused);
        if (!staged || std::strcmp(staged, "MISSING") != 0) return "STAGING_CONFLICT";
        if (auto* error = cleanupMetadata(identity_)) return error;
        active_ = false; completed_ = true; offset_ = identity_.size; return nullptr;
    }
    if (std::strcmp(existing, "MISSING") != 0) return existing;
    if (!active_) return "NO_SESSION";
    Identity stored;
    if (auto* error = loadMetadata(stored)) return error;
    if (!same(stored, identity_)) return "STAGING_CONFLICT";
    std::uint8_t sha[32];
    if (auto* error = hashFile(kTemporary, identity_.size, sha)) return error;
    if (std::memcmp(sha, identity_.sha, 32)) return "HASH_MISMATCH";
    char source[kPath], target[kPath];
    if (!path(kTemporary, source) || !path(identity_.name, target)) return "PATH";
#if defined(ESP_PLATFORM)
    // ESP-IDF VFS delegates to f_rename, which refuses an existing destination.
    // FAT metadata updates are NOT guaranteed atomic under sudden power loss.
    if (::rename(source, target) != 0) return errno == EEXIST ? "CONFLICT" : "IO";
#else
    // POSIX rename overwrites; link instead gives exclusive publication on host.
    if (::link(source, target) != 0) return errno == EEXIST ? "CONFLICT" : "IO";
    if (::unlink(source) != 0) return "IO";
#endif
    if (auto* error = cleanupMetadata(identity_)) return error;
    active_ = false; completed_ = true; offset_ = identity_.size; return nullptr;
}

bool UsbSdTransfer::handle(const char* line, char* response, std::size_t capacity) {
    if (!line || (std::strncmp(line, "sdput ", 6) != 0 && std::strcmp(line, "sdput") != 0)) return false;
    if (!response || capacity < kResponseBytes) return false;
    if (busy_) { std::snprintf(response, capacity, "SDPUT ERROR code=BUSY"); return true; }
    busy_ = true;
    struct Guard { bool& busy; ~Guard() { busy = false; } } guard{busy_};
    const char* error = nullptr;
    char command[1100];
    const std::size_t length = std::strlen(line);
    if (length >= sizeof(command)) error = "SYNTAX";
    char* args[6]{};
    unsigned count = 0;
    if (!error) {
        std::memcpy(command, line, length + 1);
        char* next = command;
        while (*next) {
            if (count == 6 || *next == ' ') { error = "SYNTAX"; break; }
            args[count++] = next;
            while (*next && *next != ' ') ++next;
            if (*next) { *next++ = 0; if (!*next) error = "SYNTAX"; }
        }
    }
    if (!error && count == 2 && std::strcmp(args[1], "abort") == 0) {
        active_ = false; std::snprintf(response, capacity, "SDPUT PAUSED");
    } else if (!error && count == 2 && std::strcmp(args[1], "status") == 0) {
        Identity stored;
        error = loadMetadata(stored);
        if (error && std::strcmp(error, "MISSING") == 0) {
            std::uint32_t size = 0; error = stagedSize(size);
            if (error && std::strcmp(error, "MISSING") == 0) { error = nullptr; std::snprintf(response, capacity, "SDPUT IDLE"); }
            else if (!error) error = "STAGING_CONFLICT";
        } else if (!error) {
            std::uint32_t size = 0; error = stagedSize(size);
            if (error && std::strcmp(error, "MISSING") == 0) { error = nullptr; size = 0; }
            if (!error && size > stored.size) error = "STAGING_CONFLICT";
            if (!error) { char sha[65]; hex(stored.sha, sha); std::snprintf(response, capacity, "SDPUT PENDING name=%s size=%u offset=%u sha256=%s", stored.name, static_cast<unsigned>(stored.size), static_cast<unsigned>(size), sha); }
        }
    } else if (!error && count == 5 && (std::strcmp(args[1], "begin") == 0 || std::strcmp(args[1], "verify") == 0)) {
        Identity value;
        if (!allowedName(args[2]) || !number(args[3], value.size) || !value.size || !unhex(args[4], value.sha, 32)) error = "SYNTAX";
        else {
            std::strcpy(value.name, args[2]);
            if (std::strcmp(args[1], "begin") == 0) error = begin(value, response, capacity);
            else if (!(error = verify(value))) identityResponse(response, capacity, "VERIFIED", value.name, value.size, value.sha);
        }
    } else if (!error && count == 4 && std::strcmp(args[1], "chunk") == 0) {
        std::uint32_t offset = 0; std::uint8_t bytes[kChunkBytes];
        const std::size_t chars = std::strlen(args[3]);
        if (!number(args[2], offset) || !chars || chars > sizeof(bytes) * 2 || (chars & 1) || !unhex(args[3], bytes, chars / 2)) error = "SYNTAX";
        else if (!(error = chunk(offset, bytes, chars / 2))) std::snprintf(response, capacity, "SDPUT ACK offset=%u", static_cast<unsigned>(offset_));
    } else if (!error && count == 2 && std::strcmp(args[1], "finish") == 0) {
        if (!(error = finish())) identityResponse(response, capacity, "DONE", identity_.name, identity_.size, identity_.sha);
    } else if (!error) error = "SYNTAX";
    if (error) std::snprintf(response, capacity, "SDPUT ERROR code=%s", error);
    return true;
}
} // namespace digivice::assets
