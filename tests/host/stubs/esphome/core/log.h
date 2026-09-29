// Test stub for esphome/core/log.h: every log call is recorded so tests can
// assert on it; set WMBUS_TEST_LOG=1 to also print the lines.
#pragma once
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace esphome {
namespace testing {
struct LogLine {
  char level;
  std::string tag;
  std::string msg;
};

inline std::vector<LogLine> &log_lines() {
  static std::vector<LogLine> lines;
  return lines;
}

inline void log_printf(char level, const char *tag, const char *fmt, ...) {
  char buf[2048];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  log_lines().push_back({level, tag, buf});
  static const bool echo = std::getenv("WMBUS_TEST_LOG") != nullptr;
  if (echo)
    std::fprintf(stderr, "    [%c][%s] %s\n", level, tag, buf);
}

// True if any recorded line of this level contains `needle`.
inline bool log_contains(char level, const std::string &needle) {
  for (const auto &line : log_lines())
    if (line.level == level && line.msg.find(needle) != std::string::npos)
      return true;
  return false;
}
} // namespace testing
} // namespace esphome

#define ESP_LOGE(tag, ...) ::esphome::testing::log_printf('E', tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) ::esphome::testing::log_printf('W', tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) ::esphome::testing::log_printf('I', tag, __VA_ARGS__)
#define ESP_LOGD(tag, ...) ::esphome::testing::log_printf('D', tag, __VA_ARGS__)
#define ESP_LOGCONFIG(tag, ...) ::esphome::testing::log_printf('C', tag, __VA_ARGS__)
#define ESP_LOGV(tag, ...) ::esphome::testing::log_printf('V', tag, __VA_ARGS__)
#define ESP_LOGVV(tag, ...) ::esphome::testing::log_printf('v', tag, __VA_ARGS__)

#define esph_log_e(tag, ...) ESP_LOGE(tag, __VA_ARGS__)
#define esph_log_w(tag, ...) ESP_LOGW(tag, __VA_ARGS__)
#define esph_log_i(tag, ...) ESP_LOGI(tag, __VA_ARGS__)
#define esph_log_d(tag, ...) ESP_LOGD(tag, __VA_ARGS__)
#define esph_log_config(tag, ...) ESP_LOGCONFIG(tag, __VA_ARGS__)
#define esph_log_v(tag, ...) ESP_LOGV(tag, __VA_ARGS__)
#define esph_log_vv(tag, ...) ESP_LOGVV(tag, __VA_ARGS__)

#define LOG_PIN(prefix, pin)                                                   \
  do {                                                                         \
    if ((pin) != nullptr)                                                      \
      ESP_LOGCONFIG(TAG, "%s(pin)", prefix);                                   \
  } while (0)
