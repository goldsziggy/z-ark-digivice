#pragma once
#include "sprite.hpp"
#include <cstdint>

namespace digivice::sprite {
// User-selected private SD files, not signed downloads. One exact numeric path,
// one descriptor and the existing32px decoder. No index scan or asset heap.
// Main-task owner; the mount must outlive this object. Close before unmount.
class LocalFormArt {
public:
    explicit LocalFormArt(const char* directory = "/sdcard");
    ~LocalFormArt() { close(); }
    LocalFormArt(const LocalFormArt&) = delete;
    LocalFormArt& operator=(const LocalFormArt&) = delete;
    Result open(std::uint32_t formId);
    void close();
    bool isOpen() const { return sprite_.isOpen(); }
    std::uint32_t formId() const { return formId_; }
    const Info& info() const { return sprite_.info(); }
    const char* diagnostic() const { return diagnostic_; }
    Result decode(Animation animation, std::size_t frame,
                  std::uint16_t* pixels, std::size_t pixelCapacity,
                  std::uint8_t* mask = nullptr, std::size_t maskBytes = 0);
private:
    static bool read(void* context, std::size_t offset, void* output, std::size_t length);
    Sprite sprite_{};
    char directory_[96]{};
    int descriptor_ = -1;
    std::size_t bytes_ = 0;
    std::uint32_t formId_ = 0;
    const char* diagnostic_ = "private exact-form art not opened";
};
} // namespace digivice::sprite
