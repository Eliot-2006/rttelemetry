# Thin task runner over CMake presets. CI calls exactly these targets.
TOOLS_DIR ?= $(HOME)/.cache/rttelemetry-tools
ENV := . $(TOOLS_DIR)/env.sh
# libFuzzer needs LLVM clang (Apple clang lacks it): Homebrew LLVM on macOS, system clang on Linux.
FUZZ_CC ?= $(firstword $(wildcard /opt/homebrew/opt/llvm/bin/clang) clang)
FUZZ_SECONDS ?= 60
JOBS ?= $(shell getconf _NPROCESSORS_ONLN)

.PHONY: bootstrap fw fw-release test-host test-tsan fuzz test-py test coverage clean

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
