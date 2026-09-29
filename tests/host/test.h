// Minimal test framework: TEST(name) { EXPECT(...); } registers a test case;
// test_main.cpp runs them all (or those whose name contains argv[1]).
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace wmbus_test {
struct Case {
  const char *name;
  std::function<void()> fn;
  const char *known_bug; // non-null: expected to fail (why)
};

inline std::vector<Case> &registry() {
  static std::vector<Case> cases;
  return cases;
}

inline int &failures() {
  static int count = 0;
  return count;
}

struct Registrar {
  Registrar(const char *name, std::function<void()> fn,
            const char *known_bug = nullptr) {
    registry().push_back({name, std::move(fn), known_bug});
  }
};

inline void fail(const char *file, int line, const std::string &what) {
  failures()++;
  std::fprintf(stderr, "    FAIL %s:%d: %s\n", file, line, what.c_str());
}

template <typename T> std::string show(const T &v) {
  if constexpr (std::is_same_v<T, uint8_t>) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "0x%02x", v);
    return buf;
  } else if constexpr (std::is_same_v<T, int8_t>) {
    return std::to_string(int(v));
  } else if constexpr (std::is_enum_v<T>) {
    return std::to_string(static_cast<long long>(v));
  } else {
    std::ostringstream os;
    os << v;
    return os.str();
  }
}
} // namespace wmbus_test

#define WMBUS_CAT2(a, b) a##b
#define WMBUS_CAT(a, b) WMBUS_CAT2(a, b)
#define TEST(name)                                                             \
  static void WMBUS_CAT(test_fn_, name)();                                     \
  static ::wmbus_test::Registrar WMBUS_CAT(test_reg_, name)(                   \
      #name, WMBUS_CAT(test_fn_, name));                                       \
  static void WMBUS_CAT(test_fn_, name)()

// A test for a confirmed bug in upstream code. It is expected to fail and is
// reported as "xfail"; if it passes, the run fails so the marker gets removed.
#define KNOWN_BUG(name, why)                                                   \
  static void WMBUS_CAT(test_fn_, name)();                                     \
  static ::wmbus_test::Registrar WMBUS_CAT(test_reg_, name)(                   \
      #name, WMBUS_CAT(test_fn_, name), why);                                  \
  static void WMBUS_CAT(test_fn_, name)()

#define EXPECT(expr)                                                           \
  do {                                                                         \
    if (!(expr))                                                               \
      ::wmbus_test::fail(__FILE__, __LINE__, "EXPECT(" #expr ")");             \
  } while (0)

#define EXPECT_EQ(actual, expected)                                            \
  do {                                                                         \
    const auto &a_ = (actual);                                                 \
    const auto &e_ = (expected);                                               \
    if (!(a_ == e_))                                                           \
      ::wmbus_test::fail(__FILE__, __LINE__,                                   \
                         #actual " == " #expected ": got " +                   \
                             ::wmbus_test::show(a_) + ", expected " +          \
                             ::wmbus_test::show(e_));                          \
  } while (0)

#define EXPECT_NEAR(actual, expected, eps)                                     \
  do {                                                                         \
    double a_ = (actual), e_ = (expected);                                     \
    if (!(std::fabs(a_ - e_) <= (eps)))                                        \
      ::wmbus_test::fail(__FILE__, __LINE__,                                   \
                         #actual " ~= " #expected ": got " +                   \
                             std::to_string(a_) + ", expected " +              \
                             std::to_string(e_));                              \
  } while (0)

// Stops the current test (e.g. before dereferencing an empty optional).
#define REQUIRE(expr)                                                          \
  do {                                                                         \
    if (!(expr)) {                                                             \
      ::wmbus_test::fail(__FILE__, __LINE__, "REQUIRE(" #expr ")");            \
      return;                                                                  \
    }                                                                          \
  } while (0)
