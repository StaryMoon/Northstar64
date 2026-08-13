CXX ?= c++
BUILD_DIR ?= .build
CPPFLAGS := -Iinclude -Itests
CXXFLAGS ?= -std=c++20 -O2 -g
WARNINGS := -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow

CORE_SOURCES := \
	src/cpu.cpp \
	src/csr.cpp \
	src/decode.cpp \
	src/elf.cpp \
	src/machine.cpp \
	src/memory.cpp \
	src/privilege.cpp \
	src/sv39.cpp \
	src/trace.cpp \
	src/trap.cpp \
	src/uart.cpp

TEST_SOURCES := \
	tests/test_main.cpp \
	tests/test_cpu.cpp \
	tests/test_csr.cpp \
	tests/test_decode.cpp \
	tests/test_elf.cpp \
	tests/test_memory.cpp \
	tests/test_properties.cpp \
	tests/test_sv39.cpp \
	tests/test_trace.cpp \
	tests/test_uart.cpp

.PHONY: all test check clean sanitizers

all: $(BUILD_DIR)/northstar64

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/northstar64: $(CORE_SOURCES) src/main.cpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(WARNINGS) $^ -o $@

$(BUILD_DIR)/northstar64_tests: $(CORE_SOURCES) $(TEST_SOURCES) | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(WARNINGS) $^ -o $@

test: $(BUILD_DIR)/northstar64_tests
	$(BUILD_DIR)/northstar64_tests

check: $(BUILD_DIR)/northstar64 test
	$(BUILD_DIR)/northstar64 version

sanitizers: CXXFLAGS := -std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
sanitizers: clean test

clean:
	rm -rf $(BUILD_DIR)
