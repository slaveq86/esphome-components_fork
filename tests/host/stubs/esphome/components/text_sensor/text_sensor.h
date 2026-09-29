// Test stub for esphome/components/text_sensor/text_sensor.h.
#pragma once
#include <string>
#include <vector>

#include "esphome/core/component.h"

namespace esphome {
namespace text_sensor {
class TextSensor {
public:
  void publish_state(const std::string &state) {
    this->state = state;
    this->published.push_back(state);
  }
  std::string state;
  std::vector<std::string> published;
};
} // namespace text_sensor
} // namespace esphome

#define LOG_TEXT_SENSOR(prefix, type, obj)                                     \
  do {                                                                         \
    (void)(obj);                                                               \
    ESP_LOGCONFIG(TAG, "%s%s", prefix, type);                                  \
  } while (0)
