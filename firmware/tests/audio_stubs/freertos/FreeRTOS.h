#pragma once
#include <cstdint>
using BaseType_t=int;using TickType_t=unsigned;using UBaseType_t=unsigned;
constexpr int pdPASS=1,pdTRUE=1,pdFALSE=0;
constexpr TickType_t pdMS_TO_TICKS(unsigned ms){return ms/10;}
