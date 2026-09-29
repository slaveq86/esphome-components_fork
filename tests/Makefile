# Host (Linux) build of the wM-Bus components + tests, no ESP32 needed.
#
#   make -C tests            build and run all host tests
#   make -C tests run T=izar only tests whose name contains "izar"
#   make -C tests SAN=       build without sanitizers (faster)
#   make -C tests clean
#
# The component sources are compiled unmodified from ../components against the
# fake ESPHome/FreeRTOS headers in host/stubs.

SHELL    := /bin/bash
CXX      ?= g++
SAN      ?= -fsanitize=address,undefined -fsanitize-recover=address,undefined \
            -fno-omit-frame-pointer -D_GLIBCXX_SANITIZE_VECTOR
CXXFLAGS ?= -std=gnu++20 -O1 -g -Wall -Wno-unused-variable -Wno-unused-function \
            -Wno-sign-compare -Wno-unused-but-set-variable -Wno-format-security
BUILD    := build/$(if $(SAN),asan,plain)
COMP     := ../components
INCLUDES := -Ihost/stubs -I$(BUILD)/include -Ihost -Ihost/sim

# Only the parts that run without real hardware. SX1276 needs esp_timer and
# CC1101 is not simulated yet, so those transceivers are left out.
COMPONENT_SRCS := $(wildcard $(COMP)/wmbus_common/*.cpp) \
  $(addprefix $(COMP)/wmbus_radio/,decode3of6.cpp packet.cpp transceiver.cpp \
    transceiver_sx1262.cpp component.cpp) \
  $(addprefix $(COMP)/wmbus_meter/,wmbus_meter.cpp base_sensor.cpp \
    sensor/sensor.cpp text_sensor/text_sensor.cpp)
TEST_SRCS := $(wildcard host/*.cpp host/sim/*.cpp)

COMPONENT_OBJS := $(patsubst $(COMP)/%.cpp,$(BUILD)/components/%.o,$(COMPONENT_SRCS))
TEST_OBJS      := $(patsubst host/%.cpp,$(BUILD)/tests/%.o,$(TEST_SRCS))
LINKS          := $(addprefix $(BUILD)/include/esphome/components/, \
                    wmbus_common wmbus_radio wmbus_meter)
BIN            := $(BUILD)/wmbus_tests
# Rebuild everything when the compiler flags change.
STAMP          := $(BUILD)/.flags-$(shell echo '$(CXX) $(CXXFLAGS) $(SAN)' | md5sum | cut -c1-8)

.PHONY: all run clean
all: run

# Sanitizers run in recover mode so every test runs. ASan reports are counted
# by the test binary (per test); UBSan and leak reports fail the run here.
run: $(BIN)
	@cd .. && ASAN_OPTIONS=halt_on_error=0 UBSAN_OPTIONS=print_stacktrace=1 \
	  LSAN_OPTIONS=suppressions=$(CURDIR)/lsan.supp:print_suppressions=0 \
	  tests/$(BIN) $(T) 2>&1 | tee tests/$(BUILD)/last_run.log; \
	status=$${PIPESTATUS[0]}; \
	if grep -qE 'runtime error:|ERROR: LeakSanitizer' tests/$(BUILD)/last_run.log; then \
	  echo "Sanitizer errors reported, see tests/$(BUILD)/last_run.log"; exit 1; \
	fi; exit $$status

$(BIN): $(COMPONENT_OBJS) $(TEST_OBJS)
	$(CXX) $(SAN) -o $@ $^

# Components include each other as "esphome/components/<name>/...".
$(BUILD)/include/esphome/components/%:
	@mkdir -p $(dir $@)
	ln -sfn $(abspath $(COMP)/$*) $@

$(STAMP):
	@mkdir -p $(BUILD) && rm -f $(BUILD)/.flags-* && touch $@

# packet.cpp passes `(size_t *)&dummy` (an int) to checkWMBusFrame. On the
# 32-bit ESP32 size_t is 4 bytes and that is harmless; on a 64-bit host it is
# an 8-byte, misaligned write into a 4-byte stack slot. So ASan's stack checks
# are off for packet.cpp and UBSan's alignment check for wmbus.cpp (where the
# write happens); everything else stays on.
$(BUILD)/components/wmbus_radio/packet.o: SAN_EXTRA := $(if $(SAN),--param asan-stack=0)
$(BUILD)/components/wmbus_common/wmbus.o: SAN_EXTRA := $(if $(SAN),-fno-sanitize=alignment)

$(BUILD)/components/%.o: $(COMP)/%.cpp $(STAMP) | $(LINKS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(SAN) $(SAN_EXTRA) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD)/tests/%.o: host/%.cpp $(STAMP) | $(LINKS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(SAN) $(INCLUDES) -MMD -MP -c $< -o $@

clean:
	rm -rf build

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
