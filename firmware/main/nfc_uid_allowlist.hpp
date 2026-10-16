#pragma once
#include "../runtime/nfc_cards.hpp"
namespace digivice::nfccards::config {
// Intentionally EMPTY. No arbitrary/example UID ships as a live card binding.
// Generate from explicit owner-selected mappings with tools/configure_nfc_cards.py.
inline constexpr Binding bindings[]={{}};
inline constexpr std::size_t count=0;
static_assert(valid(bindings,count),"Invalid NFC UID allowlist");
}
