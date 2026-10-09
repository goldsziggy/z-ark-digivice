#pragma once
#include <cstddef>
#include <cstdint>
using BaseType_t=int;using UBaseType_t=unsigned;using TickType_t=unsigned;
constexpr int pdTRUE=1,pdPASS=1;constexpr unsigned portMAX_DELAY=~0u;
struct StaticQueue_t {};struct StaticSemaphore_t {};
