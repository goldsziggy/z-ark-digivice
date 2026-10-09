#include "background_decode.hpp"

namespace digivice::assets {
namespace {
unsigned big16(const std::uint8_t* p) { return unsigned(p[0])*256+p[1]; }
bool sizeSupported(unsigned width, unsigned height) { return width == height && (width == 412 || width == 480); }
}
bool inspectBackgroundJpeg(const std::uint8_t* data, std::size_t bytes, JpegInfo& info) {
    info = {};
    if (!data || bytes < 32 || bytes > kMaximumJpegBytes || data[0] != 0xff || data[1] != 0xd8 ||
        data[bytes-2] != 0xff || data[bytes-1] != 0xd9) return false;
    JpegInfo candidate{};
    bool frame = false, quant = false, huffman = false;
    std::size_t offset = 2;
    for (unsigned markers = 0; markers < 128 && offset < bytes-2; ++markers) {
        if (data[offset++] != 0xff) return false;
        while (offset < bytes && data[offset] == 0xff) ++offset;
        if (offset >= bytes) return false;
        const unsigned marker = data[offset++];
        if (marker == 0 || marker == 0xd8 || marker == 0xd9 || (marker >= 0xd0 && marker <= 0xd7) ||
            offset > bytes-2) return false;
        const auto length = big16(data+offset);
        if (length < 2 || length > bytes-offset) return false;
        const auto* segment = data+offset+2;
        const auto payload = length-2;
        if (marker == 0xc0) {
            if (frame || payload != 15 || segment[0] != 8 || segment[5] != 3) return false;
            candidate = {big16(segment+3), big16(segment+1)};
            if (!sizeSupported(candidate.width,candidate.height)) return false;
            for (unsigned component = 0; component < 3; ++component) {
                const auto* c = segment+6+component*3;
                if (c[0] != component+1 || c[2] > 3) return false;
                if (component && c[1] != 0x11) return false;
                if (!component && c[1] != 0x11 && c[1] != 0x12 && c[1] != 0x21 && c[1] != 0x22) return false;
            }
            frame = true;
        } else if (marker == 0xdb) {
            if (!payload || payload%65) return false;
            for (unsigned i = 0; i < payload; i += 65) if (segment[i] > 3) return false;
            quant = true;
        } else if (marker == 0xc4) {
            std::size_t i = 0;
            while (i < payload) {
                if (payload-i < 17 || (segment[i] & 0xee)) return false; // DC/AC, table0/1 only.
                unsigned count = 0;
                for (unsigned n = 1; n <= 16; ++n) count += segment[i+n];
                if (!count || count > 256 || count > payload-i-17) return false;
                i += 17+count;
            }
            huffman = true;
        } else if (marker == 0xda) {
            if (!frame || !quant || !huffman || payload != 10 || segment[0] != 3 ||
                segment[7] != 0 || segment[8] != 63 || segment[9] != 0) return false;
            for (unsigned i = 0; i < 3; ++i)
                if (segment[1+i*2] != i+1 || (segment[2+i*2] & 0xee)) return false;
            if (offset+length >= bytes-2) return false;
            info = candidate; return true;
        } else if (marker == 0xdd) {
            if (payload != 2) return false;
        } else if (!((marker >= 0xe0 && marker <= 0xef) || marker == 0xfe)) {
            // Progressive, arithmetic coding, unusual components and extension
            // modes are outside the bounded local-background decoder contract.
            return false;
        }
        offset += length;
    }
    return false;
}
bool writeBackgroundBlock(const JpegInfo& info, unsigned left, unsigned top,
    unsigned right, unsigned bottom, const std::uint8_t* rgb, std::size_t rgbBytes,
    std::uint16_t* target, std::size_t capacity, std::size_t& pixelsWritten) {
    pixelsWritten = 0;
    if (!sizeSupported(info.width,info.height) || !rgb || !target || capacity < kBackgroundPixels ||
        left > right || top > bottom || right >= info.width || bottom >= info.height ||
        right-left >= 16 || bottom-top >= 16) return false;
    const unsigned width = right-left+1, height = bottom-top+1;
    if (rgbBytes != static_cast<std::size_t>(width)*height*3) return false;
    const auto x0 = (left*kBackgroundSide+info.width-1)/info.width;
    const auto x1 = ((right+1)*kBackgroundSide+info.width-1)/info.width;
    const auto y0 = (top*kBackgroundSide+info.height-1)/info.height;
    const auto y1 = ((bottom+1)*kBackgroundSide+info.height-1)/info.height;
    for (unsigned y = y0; y < y1; ++y) {
        const auto sourceY = y*info.height/kBackgroundSide-top;
        for (unsigned x = x0; x < x1; ++x) {
            const auto sourceX = x*info.width/kBackgroundSide-left;
            const auto* p = rgb+(sourceY*width+sourceX)*3;
            const auto r = unsigned(p[0])*3/5, g = unsigned(p[1])*3/5, b = unsigned(p[2])*3/5;
            target[y*kBackgroundSide+x] = static_cast<std::uint16_t>(((r>>3)<<11) | ((g>>2)<<5) | (b>>3));
            ++pixelsWritten;
        }
    }
    return true;
}
} // namespace digivice::assets
