#pragma once
#include "FreeRTOS.h"
#include <cstddef>
struct StaticQueue_t {std::uint8_t* bytes=nullptr;std::size_t slots=0,size=0,read=0,count=0;};
using QueueHandle_t=StaticQueue_t*;
QueueHandle_t xQueueCreateStatic(UBaseType_t,UBaseType_t,std::uint8_t*,StaticQueue_t*);
BaseType_t xQueueSend(QueueHandle_t,const void*,TickType_t);
BaseType_t xQueueReceive(QueueHandle_t,void*,TickType_t);
BaseType_t xQueueOverwrite(QueueHandle_t,const void*);
BaseType_t xQueueReset(QueueHandle_t);
