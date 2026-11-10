#!/usr/bin/env bash
# Fail if a linked ELF defines or references a heap allocator (PRD §15.1, NFR-MEM-1).
# Our own trapping `_sbrk` stub is allowed; newlib's `_sbrk_r` only appears if malloc was pulled in.
set -euo pipefail
[[ $# -eq 1 ]] || { echo "usage: $0 <elf>" >&2; exit 2; }

hits=$(arm-none-eabi-nm "$1" | awk '$NF ~ /^(malloc|free|calloc|realloc|_malloc_r|_free_r|_calloc_r|_realloc_r|pvPortMalloc|vPortFree|_sbrk_r)$/')
if [[ -n $hits ]]; then
  echo "check_no_heap: FAIL $1 contains heap symbols:" >&2
  echo "$hits" >&2
  exit 1
fi
echo "check_no_heap: OK $1"
