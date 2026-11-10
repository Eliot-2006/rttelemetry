#!/usr/bin/env bash
# Run the CI job steps in ubuntu:24.04 (the CI runner OS, native arch) to prove the Linux path
# locally without pushing (PRD gate G11). The repo is mounted read-write, but build output goes
# to build/ci-docker/ so host and container CMake caches never mix. Pinned tools are cached in
# the named volume `rttelemetry-tools`.
# Usage: scripts/ci-docker.sh [make targets...]
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TARGETS=${*:-test-host test-tsan test-py fw fw-release size-check test-renode}
GIT_COMMON=$(cd "$ROOT" && cd "$(git rev-parse --git-common-dir)" && pwd)  # worktrees point here
mkdir -p "$ROOT/build/ci-docker"

docker run --rm --init \
  -v "$ROOT":/src -v "$ROOT/build/ci-docker":/src/build -v rttelemetry-tools:/opt/tools \
  -v "$GIT_COMMON":"$GIT_COMMON":ro \
  -w /src -e TOOLS_DIR=/opt/tools -e TARGETS="$TARGETS" \
  ubuntu:24.04 bash -euo pipefail -c '
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq
    apt-get install -y -qq --no-install-recommends \
      build-essential clang libclang-rt-dev cmake ninja-build cppcheck git curl ca-certificates \
      xz-utils perl python3 python3-venv python3-dev >/dev/null
    git config --global --add safe.directory "*"
    scripts/bootstrap.sh
    make $TARGETS
  '
