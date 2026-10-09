#pragma once
#include "FreeRTOS.h"
struct StaticSemaphore_t { unsigned unused = 0; };
using SemaphoreHandle_t = StaticSemaphore_t*;
int xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout);
int xSemaphoreGive(SemaphoreHandle_t semaphore);
