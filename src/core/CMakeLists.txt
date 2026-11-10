# Portable core: C11 + <stdint.h> <stddef.h> <stdbool.h> <string.h> <stdatomic.h> only (PRD §6.3).
add_library(core STATIC ring.c crc16.c cobs.c frame.c msg.c cmd.c plaus.c hist.c)
target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(core PRIVATE -Wconversion -Wsign-conversion)
if(FUZZ)
  target_compile_options(core PRIVATE -fsanitize=fuzzer-no-link)
  # Xcode ld64 (<= 15) rejects the relocations UBSan's `function` check emits next to
  # libFuzzer instrumentation ("invalid r_symbolnum"). The check is irrelevant here (C, no
  # indirect calls through mismatched pointer types), so drop just that one on Apple.
  if(APPLE)
    target_compile_options(core PRIVATE -fno-sanitize=function)
  endif()
endif()
