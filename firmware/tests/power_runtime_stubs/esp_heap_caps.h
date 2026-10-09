#pragma once
#include <cstddef>
#include <cstdint>
constexpr std::uint32_t MALLOC_CAP_8BIT = 1u << 2;
constexpr std::uint32_t MALLOC_CAP_SPIRAM = 1u << 10;
void* heap_caps_malloc(std::size_t bytes, std::uint32_t capabilities);
void heap_caps_free(void* allocation);
