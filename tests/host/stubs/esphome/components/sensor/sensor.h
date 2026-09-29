// Test stub for esphome/components/sensor/sensor.h: records published states.
#pragma once
#include <vector>

#include "esphome/core/component.h"

namespace esphome {
namespace sensor {
class Sensor {
public:
  void publish_state(float state) {
    this->state = state;
    this->published.push_back(state);
  }
  float state{0.0f};
  std::vector<float> published;
};
} // namespace sensor
} // namespace esphome

#define LOG_SENSOR(prefix, type, obj)                                          \
  do {                                                                         \
    (void)(obj);                                                               \
    ESP_LOGCONFIG(TAG, "%s%s", prefix, type);                                  \
  } while (0)
