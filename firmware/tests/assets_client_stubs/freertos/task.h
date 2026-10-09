#pragma once
#include "FreeRTOS.h"
using TaskHandle_t=void*;
BaseType_t xTaskCreate(void(*)(void*),const char*,std::uint32_t,void*,UBaseType_t,TaskHandle_t*);
