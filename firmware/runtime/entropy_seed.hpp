#pragma once
#include <cstddef>
#include <cstdint>

namespace digivice::entropy {
// Gameplay diversification, not a key derivation or authentication scheme.
// Factory identity is public; randomness comes from the supplied entropy words.
struct Material { std::uint8_t identity[6]{}; std::uint32_t words[4]{}; };
struct Seeds {
    std::uint32_t profile=0,world=0,pacing=0,offers=0;
    bool ready() const { return profile && world && pacing && offers; }
};
inline std::uint32_t derive(const Material& material,std::uint32_t domain) {
    std::uint32_t value=2166136261u;
    const auto add=[&](std::uint8_t byte){value=(value^byte)*16777619u;};
    for(unsigned i=0;i<4;++i)add(static_cast<std::uint8_t>(domain>>(8*i)));
    for(auto byte:material.identity)add(byte);
    for(auto word:material.words)for(unsigned i=0;i<4;++i)add(static_cast<std::uint8_t>(word>>(8*i)));
    value^=value>>16;value*=0x7feb352du;value^=value>>15;value*=0x846ca68bu;value^=value>>16;
    return value ? value : domain;
}
inline Seeds seeds(const Material& material) {
    return {derive(material,0x50524f46u),derive(material,0x574f524cu),derive(material,0x50414345u),derive(material,0x4f464652u)};
}
} // namespace digivice::entropy
