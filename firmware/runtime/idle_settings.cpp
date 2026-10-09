#include "idle_settings.hpp"
#include <cstring>

namespace digivice::idle {
namespace {
void put(std::uint8_t* p, std::uint32_t value) {
    for (unsigned i=0;i<4;++i) p[i]=static_cast<std::uint8_t>(value>>(8*i));
}
std::uint32_t get(const std::uint8_t* p) {
    std::uint32_t value=0;for(unsigned i=0;i<4;++i)value|=std::uint32_t(p[i])<<(8*i);return value;
}
std::uint32_t crc(const std::uint8_t* p, std::size_t length) {
    std::uint32_t value=~0u;
    for(std::size_t i=0;i<length;++i){value^=p[i];for(unsigned b=0;b<8;++b)value=(value>>1)^(0xedb88320u&(0u-(value&1u)));}
    return ~value;
}
}
bool encodeSettings(std::uint32_t seconds, SettingsRecord& out) {
    if (!validTimeout(seconds)) return false;
    SettingsRecord record;std::memcpy(record.bytes,"DIDL",4);
    put(record.bytes+4,1);put(record.bytes+8,seconds);put(record.bytes+12,crc(record.bytes,12));
    out=record;return true;
}
bool decodeSettings(const SettingsRecord& record, std::uint32_t& seconds) {
    if (std::memcmp(record.bytes,"DIDL",4)||get(record.bytes+4)!=1||
        get(record.bytes+12)!=crc(record.bytes,12)||!validTimeout(get(record.bytes+8))) return false;
    seconds=get(record.bytes+8);return true;
}
} // namespace digivice::idle
