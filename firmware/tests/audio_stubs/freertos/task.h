#pragma once
#include "FreeRTOS.h"
using TaskHandle_t=void*;using TaskFunction_t=void(*)(void*);
BaseType_t xTaskCreate(TaskFunction_t,const char*,std::uint32_t,void*,UBaseType_t,TaskHandle_t*);
void vTaskDelay(TickType_t);
