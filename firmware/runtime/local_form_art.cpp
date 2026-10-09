#include "local_form_art.hpp"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace digivice::sprite {
LocalFormArt::LocalFormArt(const char* directory) {
    if (!directory) return;
    std::size_t length = 0;
    while (length < sizeof(directory_) && directory[length]) ++length;
    if (length && length < sizeof(directory_)) std::memcpy(directory_, directory, length + 1);
}
void LocalFormArt::close() {
    sprite_.close();
    if (descriptor_ >= 0) ::close(descriptor_);
    descriptor_ = -1; bytes_ = 0; formId_ = 0;
}
Result LocalFormArt::open(std::uint32_t formId) {
    close();
    if (!*directory_ || !formId || formId > 512) {
        diagnostic_ = "invalid exact-form path request"; return Result::InvalidReader;
    }
    char path[112];
    const auto length = std::snprintf(path, sizeof(path), "%s/DSF%05u.DVA", directory_, static_cast<unsigned>(formId));
    if (length < 0 || static_cast<std::size_t>(length) >= sizeof(path)) {
        diagnostic_ = "private exact-form path exceeds bound"; return Result::InvalidReader;
    }
    int flags = O_RDONLY;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    descriptor_ = ::open(path, flags);
    if (descriptor_ < 0) {
        diagnostic_ = errno == ENOENT ? "exact-form private file missing" : "private file unavailable";
        return Result::Io;
    }
    struct stat stat{};
    if (::fstat(descriptor_, &stat) != 0 || !S_ISREG(stat.st_mode) || stat.st_size < 32 ||
        static_cast<std::uint64_t>(stat.st_size) > kMaximumBlobBytes) {
        close(); diagnostic_ = "private file type/length invalid"; return Result::InvalidLength;
    }
    bytes_ = static_cast<std::size_t>(stat.st_size);
    const auto result = sprite_.open({this, read, bytes_});
    if (result != Result::Ok) {
        close(); diagnostic_ = "private DVA validation failed; fallback required"; return result;
    }
    formId_ = formId;
    diagnostic_ = "exact-form private DVA verified; local unsigned source";
    return Result::Ok;
}
bool LocalFormArt::read(void* context, std::size_t offset, void* output, std::size_t length) {
    auto& self = *static_cast<LocalFormArt*>(context);
    if (self.descriptor_ < 0 || !output || !length || length > kReadChunkBytes ||
        offset > self.bytes_ || length > self.bytes_ - offset) return false;
    auto* target = static_cast<std::uint8_t*>(output);
    unsigned interrupted = 0;
    while (length) {
        const auto count = ::pread(self.descriptor_, target, length, static_cast<off_t>(offset));
        if (count < 0 && errno == EINTR && interrupted++ < 3) continue;
        if (count <= 0 || static_cast<std::size_t>(count) > length) return false;
        target += count; offset += static_cast<std::size_t>(count); length -= static_cast<std::size_t>(count);
    }
    return true;
}
Result LocalFormArt::decode(Animation animation, std::size_t frame, std::uint16_t* pixels,
                            std::size_t pixelCapacity, std::uint8_t* mask, std::size_t maskBytes) {
    const auto result = sprite_.decode(animation, frame, pixels, pixelCapacity, mask, maskBytes);
    if (result == Result::Io) {
        close(); diagnostic_ = "private file read failed; fallback required";
    }
    return result;
}
} // namespace digivice::sprite
