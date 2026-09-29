// Test stub for freertos/task.h (see FreeRTOS.h).
#pragma once
#include "FreeRTOS.h"
#include "esphome/core/hal.h"

inline BaseType_t xTaskCreatePinnedToCore(TaskFunction_t, const char *name,
                                          uint32_t stack, void *,
                                          UBaseType_t priority,
                                          TaskHandle_t *handle, int core) {
  auto task = std::make_unique<FakeTask>();
  task->name = name;
  task->stack = stack;
  task->priority = priority;
  task->core = core;
  if (handle != nullptr)
    *handle = task.get();
  freertos_fake::current_task() = task.get();
  freertos_fake::tasks().push_back(std::move(task));
  return pdPASS;
}

inline BaseType_t xTaskCreate(TaskFunction_t fn, const char *name,
                              uint32_t stack, void *arg, UBaseType_t priority,
                              TaskHandle_t *handle) {
  return xTaskCreatePinnedToCore(fn, name, stack, arg, priority, handle, -1);
}

inline void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t *woken) {
  if (task != nullptr)
    task->notifications++;
  if (woken != nullptr)
    *woken = pdTRUE;
}

// Blocks in 1 ms ticks (advancing the fake clock, so scheduled chip events
// can fire and notify the task) until notified or the timeout expires.
inline uint32_t ulTaskNotifyTake(BaseType_t clear_on_exit, TickType_t ticks) {
  TaskHandle_t task = freertos_fake::current_task();
  if (task == nullptr)
    return 0;
  for (TickType_t t = 0; task->notifications == 0 && t < ticks; t++)
    esphome::testing::advance_time(1000);
  if (task->notifications == 0) {
    task->timeouts++;
    return 0;
  }
  uint32_t value = task->notifications;
  task->notifications = clear_on_exit ? 0 : value - 1;
  return value;
}
