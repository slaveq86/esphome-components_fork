// Test stub for FreeRTOS: single-threaded. Tasks are recorded but never run;
// tests call Radio::receive_frame() themselves in place of the RX task.
// Task notifications are a counter that the fake ISR increments.
#pragma once
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <vector>

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;
typedef void (*TaskFunction_t)(void *);

#define pdFALSE 0
#define pdTRUE 1
#define pdPASS pdTRUE
#define pdFAIL pdFALSE
#define errQUEUE_FULL 0
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define portMAX_DELAY ((TickType_t)0xffffffffUL)
#define portNUM_PROCESSORS 2
#define portYIELD_FROM_ISR(x) (void)(x)

struct FakeTask {
  std::string name;
  uint32_t stack;
  UBaseType_t priority;
  int core;
  uint32_t notifications = 0;
  // ulTaskNotifyTake() calls that found no notification (i.e. timed out).
  uint32_t timeouts = 0;
};
typedef FakeTask *TaskHandle_t;

struct FakeQueue {
  size_t length;
  size_t item_size;
  std::deque<std::vector<uint8_t>> items;
};
typedef FakeQueue *QueueHandle_t;

namespace freertos_fake {
inline std::vector<std::unique_ptr<FakeTask>> &tasks() {
  static std::vector<std::unique_ptr<FakeTask>> t;
  return t;
}
inline std::vector<std::unique_ptr<FakeQueue>> &queues() {
  static std::vector<std::unique_ptr<FakeQueue>> q;
  return q;
}
// The task whose notifications ulTaskNotifyTake() consumes.
inline TaskHandle_t &current_task() {
  static TaskHandle_t t = nullptr;
  return t;
}
} // namespace freertos_fake
