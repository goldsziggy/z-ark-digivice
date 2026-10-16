#pragma once
// Synthetic transport fixtures ONLY; never included by either firmware build.
#include "nfc_cards.hpp"
namespace digivice::nfccards::config {
inline constexpr Binding bindings[]={
    {{0xc0,0xde,0,1},4,0xD101},{{0xc0,0xde,0,2},4,0xD102},
    {{4,0xc0,0xde,0,0,0,3},7,0xD103},
    {{4,0xc0,0xde,0,0,0,0,0,0,4},10,0xD104},
    {{0xc0,0xde,0,5},4,0xD106},{{0xc0,0xde,0,6},4,0xD101}
};
inline constexpr std::size_t count=sizeof(bindings)/sizeof(bindings[0]);
static_assert(valid(bindings,count));
}
