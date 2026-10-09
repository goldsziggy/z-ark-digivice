#pragma once
#include <cstdint>
using BaseType_t=int; using TickType_t=unsigned; using UBaseType_t=unsigned;
constexpr int pdPASS=1;
constexpr TickType_t pdMS_TO_TICKS(unsigned ms) { return ms/10; }
struct portMUX_TYPE { int unused; };
#define portMUX_INITIALIZER_UNLOCKED {0}
void fakeEnter(portMUX_TYPE*); void fakeExit(portMUX_TYPE*);
#define portENTER_CRITICAL(lock) fakeEnter(lock)
#define portEXIT_CRITICAL(lock) fakeExit(lock)
