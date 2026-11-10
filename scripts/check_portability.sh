#!/usr/bin/env bash
# NFR-PORT-1: firmware has no Renode-specific code paths (same ELF on Renode and silicon).
# PRD §6.3: src/core includes only C11 freestanding headers, <string.h>, <stdatomic.h>
# and its own headers, so it builds on the host with no CMSIS/FreeRTOS include paths.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0

if grep -rnE '#[[:space:]]*ifn?def[[:space:]]+RENODE([^A-Za-z0-9_]|$)|defined[[:space:]]*\(?[[:space:]]*RENODE([^A-Za-z0-9_]|$)|__RENODE__' src; then
  echo "check_portability: FAIL Renode-specific conditionals in src/ (NFR-PORT-1)" >&2
  fail=1
fi

allowed=" float.h iso646.h limits.h stdalign.h stdarg.h stdbool.h stddef.h stdint.h stdnoreturn.h string.h stdatomic.h "
if [[ -d src/core ]]; then
  while IFS=: read -r file line text; do
    hdr=$(sed -E 's/.*include[[:space:]]*[<"]([^>"]+)[>"].*/\1/' <<<"$text")
    if [[ $text == *'"'* ]]; then
      [[ -f $(dirname "$file")/$hdr ]] && continue
    else
      [[ $allowed == *" $hdr "* ]] && continue
    fi
    echo "$file:$line: $hdr not allowed in src/core (PRD §6.3)" >&2
    fail=1
  done < <(grep -rnE '^[[:space:]]*#[[:space:]]*include' src/core)
fi

[[ $fail -eq 0 ]] && echo "check_portability: OK"
exit $fail
