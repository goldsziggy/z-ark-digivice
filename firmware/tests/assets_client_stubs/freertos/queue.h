#pragma once
#include "FreeRTOS.h"
using QueueHandle_t=void*;
QueueHandle_t xQueueCreateStatic(UBaseType_t,UBaseType_t,std::uint8_t*,StaticQueue_t*);
BaseType_t xQueueSend(QueueHandle_t,const void*,TickType_t);
BaseType_t xQueueReceive(QueueHandle_t,void*,TickType_t);
BaseType_t xQueueOverwrite(QueueHandle_t,const void*);
