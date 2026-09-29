// Test stub for esphome/core/application.h.
#pragma once
#include <string>

namespace esphome {
class Application {
public:
  const std::string &get_friendly_name() const { return this->friendly_name_; }

protected:
  std::string friendly_name_{"wmbus-test"};
};

inline Application App;
} // namespace esphome
