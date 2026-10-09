#include "audio_settings.hpp"
#include <cstring>

namespace digivice::sound {
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
bool samePreferences(const Preferences& a, const Preferences& b) {
    return a.volume==b.volume && a.muted==b.muted && a.music==b.music;
}
bool encodeSettings(const Preferences& preferences, SettingsRecord& out) {
    if(preferences.volume>kMaxVolume)return false;
    SettingsRecord record;std::memcpy(record.bytes,"DAUD",4);put(record.bytes+4,1);
    record.bytes[8]=preferences.volume;record.bytes[9]=preferences.muted;record.bytes[10]=preferences.music;
    put(record.bytes+12,crc(record.bytes,12));out=record;return true;
}
bool decodeSettings(const SettingsRecord& record, Preferences& out) {
    if(std::memcmp(record.bytes,"DAUD",4)||get(record.bytes+4)!=1||record.bytes[8]>kMaxVolume||
       record.bytes[9]>1||record.bytes[10]>1||record.bytes[11]!=0||get(record.bytes+12)!=crc(record.bytes,12))return false;
    out={record.bytes[8],record.bytes[9]!=0,record.bytes[10]!=0};return true;
}
} // namespace digivice::sound
