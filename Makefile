# Thin task runner over CMake presets. CI calls exactly these targets.
TOOLS_DIR ?= $(HOME)/.cache/rttelemetry-tools
ENV := . $(TOOLS_DIR)/env.sh
# libFuzzer needs LLVM clang (Apple clang lacks it): Homebrew LLVM on macOS, system clang on Linux.
FUZZ_CC ?= $(firstword $(wildcard /opt/homebrew/opt/llvm/bin/clang) clang)
FUZZ_SECONDS ?= 60
# Apple clang 15's ASan aborts at startup on macOS 27, so on macOS host tests use Homebrew LLVM.
# Elsewhere CMake honours CC (CI runs a GCC and a Clang pass).
HOST_CC ?= $(if $(filter Darwin,$(shell uname -s)),$(wildcard /opt/homebrew/opt/llvm/bin/clang))
HOST_CC_FLAG = $(if $(HOST_CC),-DCMAKE_C_COMPILER=$(HOST_CC))
JOBS ?= $(shell getconf _NPROCESSORS_ONLN)

.PHONY: bootstrap fw fw-release test-host test-tsan fuzz test-py test-platform test coverage clean

bootstrap:
	TOOLS_DIR=$(TOOLS_DIR) scripts/bootstrap.sh

fw:
	$(ENV) && cmake --preset fw-debug >/dev/null && cmake --build build/fw-debug -j$(JOBS)

fw-release:
	$(ENV) && cmake --preset fw-release >/dev/null && cmake --build build/fw-release -j$(JOBS)

test-host:
	cmake --preset host $(HOST_CC_FLAG) >/dev/null && cmake --build build/host -j$(JOBS) && ctest --test-dir build/host --output-on-failure -LE stress

test-tsan:
	cmake --preset host-tsan $(HOST_CC_FLAG) >/dev/null && cmake --build build/host-tsan -j$(JOBS) && ctest --test-dir build/host-tsan --output-on-failure -L stress

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

# src/core coverage (PRD §19.2 gate: >= 90 % lines, >= 80 % branches). Measured on a separate
# sanitizer-free build: ASan/UBSan checks show up as extra (never-taken) gcov branches.
# gcov flavour follows the C compiler (CC); override with GCOV=...
coverage:
	cmake -S . -B build/host-cov -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCOVERAGE=ON >/dev/null && cmake --build build/host-cov -j$(JOBS)
	find build/host-cov -name '*.gcda' -delete
	ctest --test-dir build/host-cov --output-on-failure -LE stress
	$(ENV) && cc=$$(sed -n 's/^CMAKE_C_COMPILER:FILEPATH=//p' build/host-cov/CMakeCache.txt); \
	case "$$cc" in *clang*) d=$$(dirname "$$cc"); g="$$d/llvm-cov gcov"; [ -x "$$d/llvm-cov" ] || g="xcrun llvm-cov gcov";; \
	  *) g=gcov; command -v gcov >/dev/null || g="xcrun llvm-cov gcov";; esac; \
	mkdir -p build/host/coverage && gcovr -r . --filter 'src/core/' build/host-cov \
	  --gcov-executable "$${GCOV:-$$g}" --html-details build/host/coverage/index.html \
	  --txt build/host/coverage/coverage.txt --fail-under-line 90 --fail-under-branch 80 --print-summary

clean:
	cmake -E rm -rf build

# --- CI quality gates (PRD §15.4, §21.2, §25) ---
.PHONY: size-check static-analysis ci-docker test-renode golden demo bench-emu soak

# Memory budgets + no-heap on every firmware image (run after fw fw-release).
size-check:
	$(ENV) && elfs=$$(ls build/fw-debug/src/*.elf build/fw-release/src/*.elf) && \
	  python tools/size_budget.py $$elfs && \
	  for e in $$elfs; do scripts/check_no_heap.sh $$e || exit 1; done

CPPCHECK_DIRS = $(wildcard src/core src/app src/drivers)
static-analysis:
	cppcheck -q --std=c11 --enable=warning,style,performance,portability --error-exitcode=1 \
	  --inline-suppr --suppress=missingIncludeSystem '--suppress=*:third_party/*' -D__GNUC__ -D__ARM_ARCH_PROFILE=\'M\' -D__ARM_ARCH_7EM__ -DSTM32F407xx -DHSE_VALUE=8000000U \
	  -Isrc/core -Isrc/app -Isrc/drivers -Isrc/platform \
	  -Ithird_party/cmsis-device-f4/Include -Ithird_party/CMSIS_6/CMSIS/Core/Include \
	  $(CPPCHECK_DIRS)

# Linux CI path in ubuntu:24.04 via Docker (PRD gate G11) without pushing.
ci-docker:
	scripts/ci-docker.sh

# All Renode suites: platform gates plus tests/robot/*.robot (results in build/robot).
ROBOT_SUITES = $(wildcard tests/robot/*.robot)
test-renode: test-platform fw-release
	$(if $(ROBOT_SUITES),mkdir -p build/robot/suites && $(ENV) && cd build/robot/suites && "$$RENODE_TEST" $(addprefix $(CURDIR)/,$(ROBOT_SUITES)))

# Regenerate tests/golden/nominal.jsonl after an intentional firmware change; review the diff.
golden: fw
	mkdir -p build/robot/golden && $(ENV) && cd build/robot/golden && "$$RENODE_TEST" $(CURDIR)/tests/robot/telemetry.robot --variable UPDATE_GOLDEN:True

# PRD §27: emulated end-to-end demo with a live dashboard (no hardware).
demo: fw
	$(ENV) && cd tools && python -m telemetry.demo

# Benchmarks (PRD §20.2) without hardware; results in bench/results, report in docs/results.md.
bench-emu: fw
	$(ENV) && python bench/bench.py --only B-01,B-05,B-09,B-10,B-11,B-12 && python bench/report.py
soak: fw
	$(ENV) && python bench/bench.py --only B-07 && python bench/report.py
# Every bench (~45 min, mostly the 30 min soak).
bench-all: fw
	$(ENV) && python bench/bench.py && python bench/report.py
