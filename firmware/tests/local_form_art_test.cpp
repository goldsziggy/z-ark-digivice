#include "local_form_art.hpp"
#include "fallback_asset.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

using namespace digivice::sprite;
namespace fallback = digivice::runtime::fallback;
unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL %u: %s\n",__LINE__,#x); std::exit(1); } } while(false)
void writeFile(const std::string& path, const void* data, std::size_t length) {
    auto* file = std::fopen(path.c_str(), "wb"); CHECK(file);
    CHECK(std::fwrite(data, 1, length, file) == length); CHECK(std::fclose(file) == 0);
}
void decodeAll(LocalFormArt& art) {
    std::uint16_t pixels[kMaximumPixels + 1]{};
    std::uint8_t mask[kMaximumMaskBytes + 1]{};
    pixels[kMaximumPixels] = 0xabcd; mask[kMaximumMaskBytes] = 0xed;
    for (unsigned a = 0; a < kAnimationCount; ++a)
        for (unsigned frame = 0; frame < art.info().clips[a].frames; ++frame) {
            CHECK(art.decode(static_cast<Animation>(a), frame, pixels, kMaximumPixels, mask, kMaximumMaskBytes) == Result::Ok);
            CHECK(pixels[kMaximumPixels] == 0xabcd && mask[kMaximumMaskBytes] == 0xed);
        }
}
int main(int argc, char** argv) {
    char directory[] = "/tmp/digivice-private-art-XXXXXX"; CHECK(::mkdtemp(directory));
    const auto first = std::string(directory) + "/DSF00018.DVA";
    const auto second = std::string(directory) + "/DSF00019.DVA";
    LocalFormArt art(directory);
    CHECK(art.open(0) == Result::InvalidReader && !art.isOpen());
    CHECK(art.open(513) == Result::InvalidReader && !art.isOpen());
    CHECK(art.open(18) == Result::Io && art.formId() == 0);
    writeFile(first, fallback::mote, fallback::mote_bytes);
    CHECK(art.open(18) == Result::Ok && art.formId() == 18); decodeAll(art);
    // An absent exact form must not borrow the previous form's pixels/identity.
    CHECK(art.open(19) == Result::Io && !art.isOpen() && art.formId() == 0);
    CHECK(::symlink(first.c_str(), second.c_str()) == 0);
    CHECK(art.open(19) == Result::Io && !art.isOpen());
    CHECK(::unlink(second.c_str()) == 0);
    CHECK(::mkdir(second.c_str(), 0700) == 0);
    CHECK(art.open(19) == Result::InvalidLength && !art.isOpen());
    CHECK(::rmdir(second.c_str()) == 0);
    auto corrupt = fallback::mote[100] ^ 1;
    auto* changed = std::fopen(first.c_str(), "r+b"); CHECK(changed);
    CHECK(std::fseek(changed, 100, SEEK_SET) == 0 && std::fputc(corrupt, changed) != EOF && std::fclose(changed) == 0);
    CHECK(art.open(18) != Result::Ok && !art.isOpen());
    writeFile(first, fallback::mote, fallback::mote_bytes);
    CHECK(art.open(18) == Result::Ok);
    CHECK(::truncate(first.c_str(), 0) == 0);
    std::uint16_t pixels[kMaximumPixels]{};
    CHECK(art.decode(Animation::Idle, 0, pixels, kMaximumPixels) == Result::Io && !art.isOpen() && art.formId() == 0);
    CHECK(::unlink(first.c_str()) == 0 && ::rmdir(directory) == 0);
    if (argc == 2) {
        LocalFormArt prepared(argv[1]);
        for (unsigned id = 18; id <= 20; ++id) {
            CHECK(prepared.open(id) == Result::Ok && prepared.formId() == id && prepared.info().width == 32 && prepared.info().height == 32);
            decodeAll(prepared);
        }
        CHECK(prepared.open(21) == Result::Io && !prepared.isOpen());
    }
    std::printf("PASS local exact-form art: %u checks; reader=%zuB, one frame<=%zuB plus%zuB mask; no display/SD hardware\n",
        checks, sizeof(LocalFormArt), kMaximumFrameBytes, kMaximumMaskBytes);
}
