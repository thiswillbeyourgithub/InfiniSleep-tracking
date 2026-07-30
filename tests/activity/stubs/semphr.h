#pragma once
using SemaphoreHandle_t = void*;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { static int m; return &m; }
inline int xSemaphoreTake(SemaphoreHandle_t, unsigned long) { return 1; }
inline int xSemaphoreGive(SemaphoreHandle_t) { return 1; }
