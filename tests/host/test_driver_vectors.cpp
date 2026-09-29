// Golden tests from wmbusmeters: every driver_*.cpp carries test vectors as
//   // Test: <name> <driver> <id> <key|NOKEY>
//   // telegram=|<hex without DLL CRCs>|
//   // {<expected JSON>}
// They are read from the sources at run time, so vectors added upstream are
// picked up automatically. Each telegram is decoded with the vendored
// wmbusmeters code and the JSON compared field by field (timestamps ignored).
//
// Drivers in REQUIRED_DRIVERS must pass. The rest run as a report (set
// WMBUS_VECTORS_STRICT=1 to make them fail the run too).
#include <dirent.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>

#include "esphome/components/wmbus_common/meters.h"
#include "sim/wmbus_encode.h"
#include "test.h"

namespace {
const char *DRIVER_DIR = "components/wmbus_common";
const std::set<std::string> REQUIRED_DRIVERS = {"izar"};

struct Case {
  std::string file, name, driver, id, key;
  std::vector<std::pair<std::string, std::string>> telegrams; // hex, json
};

std::string trim_cr(std::string s) {
  if (!s.empty() && s.back() == '\r')
    s.pop_back();
  return s;
}

std::vector<Case> load_cases() {
  std::vector<std::string> files;
  if (DIR *dir = opendir(DRIVER_DIR)) {
    while (dirent *e = readdir(dir)) {
      std::string f = e->d_name;
      if (f.rfind("driver_", 0) == 0 && f.size() > 4 &&
          f.substr(f.size() - 4) == ".cpp")
        files.push_back(f);
    }
    closedir(dir);
  }
  std::sort(files.begin(), files.end());

  std::vector<Case> cases;
  for (const auto &f : files) {
    std::ifstream in(std::string(DRIVER_DIR) + "/" + f);
    std::string line, pending_telegram;
    Case *current = nullptr;
    while (std::getline(in, line)) {
      line = trim_cr(line);
      if (line.rfind("// Test:", 0) == 0) {
        std::istringstream ss(line.substr(8));
        std::vector<std::string> words;
        for (std::string w; ss >> w;)
          words.push_back(w);
        if (words.size() < 4)
          continue;
        Case c;
        c.file = f;
        c.key = words.back();
        c.id = words[words.size() - 2];
        c.driver = words[words.size() - 3];
        for (size_t i = 0; i + 3 < words.size(); i++)
          c.name += (i ? " " : "") + words[i];
        cases.push_back(c);
        current = &cases.back();
        pending_telegram.clear();
      } else if (current && line.rfind("// telegram=", 0) == 0) {
        // telegram=|<hex>| optionally followed by |+<seconds>
        std::string rest = line.substr(12);
        size_t a = rest.find('|'), b = rest.find('|', a + 1);
        pending_telegram = a == std::string::npos ? rest
                           : rest.substr(a + 1, b == std::string::npos
                                                    ? std::string::npos
                                                    : b - a - 1);
      } else if (current && line.rfind("// {", 0) == 0 &&
                 !pending_telegram.empty()) {
        current->telegrams.push_back({pending_telegram, line.substr(3)});
        pending_telegram.clear();
      } else if (line.rfind("//", 0) != 0) {
        current = nullptr;
      }
    }
  }
  return cases;
}

// Flat JSON object -> key/raw-value map (strings keep their quotes). Nested
// values are kept as raw text.
std::map<std::string, std::string> parse_json(const std::string &s) {
  std::map<std::string, std::string> out;
  size_t i = s.find('{');
  if (i == std::string::npos)
    return out;
  i++;
  auto skip_ws = [&]() {
    while (i < s.size() && isspace((unsigned char)s[i]))
      i++;
  };
  auto read_string = [&]() {
    size_t start = i++;
    while (i < s.size() && s[i] != '"')
      i += s[i] == '\\' ? 2 : 1;
    i++;
    return s.substr(start, i - start);
  };
  while (i < s.size()) {
    skip_ws();
    if (s[i] == '}')
      break;
    if (s[i] == ',') {
      i++;
      continue;
    }
    std::string key = read_string();
    key = key.substr(1, key.size() - 2);
    skip_ws();
    i++; // ':'
    skip_ws();
    std::string value;
    if (s[i] == '"') {
      value = read_string();
    } else {
      size_t start = i;
      int depth = 0;
      while (i < s.size()) {
        char c = s[i];
        if (c == '"') {
          read_string();
          continue;
        }
        if (c == '{' || c == '[')
          depth++;
        else if (c == '}' || c == ']') {
          if (depth == 0)
            break;
          depth--;
        } else if (c == ',' && depth == 0)
          break;
        i++;
      }
      value = s.substr(start, i - start);
      while (!value.empty() && isspace((unsigned char)value.back()))
        value.pop_back();
    }
    out[key] = value;
  }
  return out;
}

bool same_value(const std::string &a, const std::string &b) {
  if (a == b)
    return true;
  char *ea, *eb;
  double da = strtod(a.c_str(), &ea), db = strtod(b.c_str(), &eb);
  if (*ea == '\0' && *eb == '\0' && ea != a.c_str() && eb != b.c_str())
    return std::fabs(da - db) <= 1e-9 * std::max(1.0, std::fabs(db));
  return false;
}

// Not decoded from the telegram: reception metadata, and "_" which older
// vectors lack. mkradio3/mkradio4 infer the year from the current date.
bool ignored_key(const std::string &driver, const std::string &key) {
  if (key.rfind("timestamp", 0) == 0 || key == "device" || key == "rssi_dbm" ||
      key == "_")
    return true;
  return driver.rfind("mkradio", 0) == 0 &&
         key.find("date") != std::string::npos;
}

// Decodes one case; returns a list of differences (empty = pass).
std::vector<std::string> run_case(const Case &c) {
  std::vector<std::string> problems;
  MeterInfo mi;
  std::string key = c.key == "NOKEY" ? "" : c.key;
  if (!mi.parse(c.name, c.driver, c.id + ",", key))
    return {"MeterInfo::parse failed"};
  auto meter = createMeter(&mi);
  if (!meter)
    return {"driver not found"};

  for (const auto &[hex, json] : c.telegrams) {
    sim::Bytes frame = sim::from_hex(hex);
    bool mbus = frame.size() > 4 && frame[0] == 0x68 && frame[3] == 0x68;
    AboutTelegram about("", 0, mbus ? FrameType::MBUS : FrameType::WMBUS);
    std::vector<Address> addresses;
    bool id_match = false;
    Telegram t;
    meter->handleTelegram(about, frame, false, &addresses, &id_match, &t);
    if (!id_match) {
      problems.push_back("telegram not matched to id " + c.id);
      continue;
    }
    std::string actual;
    meter->printMeter(&t, nullptr, nullptr, '\t', &actual, nullptr, nullptr,
                      nullptr, false);
    auto want = parse_json(json), got = parse_json(actual);
    for (const auto &[k, v] : want) {
      if (ignored_key(c.driver, k))
        continue;
      auto it = got.find(k);
      if (it == got.end())
        problems.push_back("missing " + k + " (want " + v + ")");
      else if (!same_value(it->second, v))
        problems.push_back(k + ": got " + it->second + ", want " + v);
    }
    for (const auto &[k, v] : got)
      if (!ignored_key(c.driver, k) && !want.count(k))
        problems.push_back("unexpected " + k + "=" + v);
  }
  return problems;
}
} // namespace

TEST(vectors_found_in_sources) {
  auto cases = load_cases();
  EXPECT(cases.size() >= 150);
  size_t izar = std::count_if(cases.begin(), cases.end(),
                              [](const Case &c) { return c.driver == "izar"; });
  EXPECT(izar >= 4);
}

TEST(vectors_required_drivers) {
  for (const auto &c : load_cases()) {
    if (!REQUIRED_DRIVERS.count(c.driver))
      continue;
    auto problems = run_case(c);
    for (const auto &p : problems)
      wmbus_test::fail(__FILE__, __LINE__, c.name + " (" + c.driver + "): " + p);
  }
}

TEST(vectors_all_drivers_report) {
  bool strict = std::getenv("WMBUS_VECTORS_STRICT") != nullptr;
  bool verbose = std::getenv("WMBUS_VECTORS_VERBOSE") != nullptr;
  int total = 0, passed = 0;
  std::map<std::string, int> failing_drivers;
  for (const auto &c : load_cases()) {
    total++;
    std::vector<std::string> problems;
    try {
      problems = run_case(c);
    } catch (const std::exception &e) {
      problems = {std::string("exception: ") + e.what()};
    }
    if (problems.empty()) {
      passed++;
      continue;
    }
    failing_drivers[c.driver]++;
    if (verbose || strict)
      for (const auto &p : problems)
        std::fprintf(stderr, "    %s %s (%s): %s\n",
                     strict ? "FAIL" : "note", c.name.c_str(),
                     c.driver.c_str(), p.c_str());
    if (strict)
      wmbus_test::failures()++;
  }
  std::printf("        driver vectors: %d/%d pass", passed, total);
  if (!failing_drivers.empty()) {
    std::printf("; differences in:");
    for (const auto &[d, n] : failing_drivers)
      std::printf(" %s(%d)", d.c_str(), n);
    std::printf(" (WMBUS_VECTORS_VERBOSE=1 for details)");
  }
  std::printf("\n");
}
