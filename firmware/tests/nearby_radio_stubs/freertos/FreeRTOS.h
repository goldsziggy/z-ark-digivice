#pragma once
#include <cstdint>
using BaseType_t=int;using UBaseType_t=unsigned;using TickType_t=std::uint32_t;
constexpr BaseType_t pdTRUE=1,pdFALSE=0;
struct portMUX_TYPE {bool held=false;};
#define portMUX_INITIALIZER_UNLOCKED {}
void testEnter(portMUX_TYPE*);void testExit(portMUX_TYPE*);
#define portENTER_CRITICAL(p) testEnter(p)
#define portEXIT_CRITICAL(p) testExit(p)
