#pragma once
#include "FreeRTOS.h"
using QueueHandle_t = void*;
int xQueueReset(QueueHandle_t);
int xQueueReceive(QueueHandle_t, void*, TickType_t);
