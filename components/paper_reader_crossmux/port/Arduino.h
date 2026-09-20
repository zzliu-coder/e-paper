#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
inline uint32_t millis() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
      .count();
}
inline void delay(unsigned ms) {
#ifdef ESP_PLATFORM
  vTaskDelay(pdMS_TO_TICKS(ms));
#else
  (void)ms;
#endif
}
