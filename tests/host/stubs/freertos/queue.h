// Test stub for freertos/queue.h (see FreeRTOS.h).
#pragma once
#include "FreeRTOS.h"

inline QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size) {
  auto queue = std::make_unique<FakeQueue>();
  queue->length = length;
  queue->item_size = item_size;
  QueueHandle_t handle = queue.get();
  freertos_fake::queues().push_back(std::move(queue));
  return handle;
}

inline BaseType_t xQueueSend(QueueHandle_t queue, const void *item,
                             TickType_t) {
  if (queue->items.size() >= queue->length)
    return errQUEUE_FULL;
  auto *bytes = static_cast<const uint8_t *>(item);
  queue->items.emplace_back(bytes, bytes + queue->item_size);
  return pdTRUE;
}

inline BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t) {
  if (queue->items.empty())
    return pdFALSE;
  std::memcpy(item, queue->items.front().data(), queue->item_size);
  queue->items.pop_front();
  return pdTRUE;
}

inline UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue) {
  return queue->items.size();
}
