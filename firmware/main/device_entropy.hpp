#pragma once
#include "../runtime/entropy_seed.hpp"
namespace digivice::device {
// Main-task startup only: call once after board power-hold initialization and
// before any radio, ADC or audio initialization. Never use during normal play.
// Failure returns unavailable seeds; callers must not create a default world.
entropy::Seeds collectStartupEntropy();
}
