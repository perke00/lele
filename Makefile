# ATLAS DSP core - build/test without CMake (used in the cloud sandbox).
# Windows/Linux CMake builds: see docs/BUILD_WINDOWS.md.
CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wconversion -Wno-sign-conversion
BUILD    := build
TEST_SRC := $(wildcard tests/*.cpp)
HEADERS  := $(wildcard src/dsp/*.h tests/*.h)

.PHONY: all test sanitize clean
all: test

$(BUILD)/atlas_tests: $(TEST_SRC) $(HEADERS)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(TEST_SRC) -o $@

test: $(BUILD)/atlas_tests
	./$(BUILD)/atlas_tests

# Same tests under AddressSanitizer + UBSan (catches out-of-bounds, UB, leaks).
$(BUILD)/atlas_tests_san: $(TEST_SRC) $(HEADERS)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $(TEST_SRC) -o $@

sanitize: $(BUILD)/atlas_tests_san
	./$(BUILD)/atlas_tests_san

clean:
	rm -rf $(BUILD)
