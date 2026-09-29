#include <cstring>
#include <exception>
#include <string>

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "test.h"

#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/asan_interface.h>
// Count an ASan report (recover mode) as a failure of the running test.
static void on_asan_report(const char *) {
  wmbus_test::fail(__FILE__, __LINE__, "AddressSanitizer report (see above)");
}
#endif

static bool run_one(const wmbus_test::Case &test_case) {
  esphome::testing::log_lines().clear();
  esphome::testing::deferred().clear();
  int before = wmbus_test::failures();
  try {
    test_case.fn();
  } catch (const std::exception &e) {
    wmbus_test::fail(__FILE__, __LINE__,
                     std::string("uncaught exception: ") + e.what());
  }
  bool ok = wmbus_test::failures() == before;
  wmbus_test::failures() = before;
  return ok;
}

int main(int argc, char **argv) {
#if defined(__SANITIZE_ADDRESS__)
  __asan_set_error_report_callback(on_asan_report);
#endif
  const char *filter = argc > 1 ? argv[1] : nullptr;
  int run = 0, failed = 0, xfailed = 0;
  // Known bugs first: ASan reports each code location once, so a bug that
  // every T1 frame hits is attributed to its KNOWN_BUG test, not a random one.
  for (bool known : {true, false}) {
    for (auto &test_case : wmbus_test::registry()) {
      if ((test_case.known_bug != nullptr) != known)
        continue;
      if (filter != nullptr && std::strstr(test_case.name, filter) == nullptr)
        continue;
      bool ok = run_one(test_case);
      run++;
      if (known && !ok) {
        xfailed++;
        std::printf("  xfail %s  (known bug: %s)\n", test_case.name,
                    test_case.known_bug);
      } else if (known && ok) {
        failed++;
        std::printf("  XPASS %s  (fixed? move it out of KNOWN_BUG)\n",
                    test_case.name);
      } else {
        failed += ok ? 0 : 1;
        std::printf("%s %s\n", ok ? "  ok   " : "  FAIL ", test_case.name);
      }
    }
  }
  std::printf("\n%d tests, %d failed, %d known bugs\n", run, failed, xfailed);
  return failed == 0 && run > 0 ? 0 : 1;
}
