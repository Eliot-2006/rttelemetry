# Thin task runner over CMake presets. CI calls exactly these targets.
TOOLS_DIR ?= $(HOME)/.cache/rttelemetry-tools
ENV := . $(TOOLS_DIR)/env.sh
# libFuzzer needs LLVM clang (Apple clang lacks it): Homebrew LLVM on macOS, system clang on Linux.
FUZZ_CC ?= $(firstword $(wildcard /opt/homebrew/opt/llvm/bin/clang) clang)
FUZZ_SECONDS ?= 60
JOBS ?= $(shell getconf _NPROCESSORS_ONLN)

.PHONY: bootstrap fw fw-release test-host test-tsan fuzz test-py test-platform test clean

bootstrap:
	TOOLS_DIR=$(TOOLS_DIR) scripts/bootstrap.sh

fw:
	$(ENV) && cmake --preset fw-debug >/dev/null && cmake --build build/fw-debug -j$(JOBS)

fw-release:
	$(ENV) && cmake --preset fw-release >/dev/null && cmake --build build/fw-release -j$(JOBS)

test-host:
	cmake --preset host >/dev/null && cmake --build build/host -j$(JOBS) && ctest --test-dir build/host --output-on-failure -LE stress

test-tsan:
	cmake --preset host-tsan >/dev/null && cmake --build build/host-tsan -j$(JOBS) && ctest --test-dir build/host-tsan --output-on-failure -L stress

fuzz:
	cmake --preset host-fuzz -DCMAKE_C_COMPILER=$(FUZZ_CC) >/dev/null && cmake --build build/host-fuzz -j$(JOBS)
	for f in build/host-fuzz/tests/fuzz/fuzz_*; do \
	  n=$$(basename $$f); mkdir -p build/host-fuzz/corpus/$$n; \
	  $$f -max_total_time=$(FUZZ_SECONDS) -print_final_stats=1 build/host-fuzz/corpus/$$n tests/fuzz/corpus/$$n || exit 1; done

test-py:
	$(ENV) && cd tools && python -m unittest discover -s . -p 'test_*.py'

# Renode integration: platform gates (PRD M0). Virtual-time only; results in build/robot.
test-platform: fw
	mkdir -p build/robot/platform && $(ENV) && cd build/robot/platform && "$$RENODE_TEST" $(CURDIR)/tests/platform/gates.robot

test: test-host test-tsan test-py

clean:
	cmake -E rm -rf build
