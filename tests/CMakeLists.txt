# Host-only tests (top-level CMakeLists adds this directory for non-cross builds).
set(UNITY_DIR ${PROJECT_SOURCE_DIR}/third_party/Unity/src)
add_library(unity STATIC ${UNITY_DIR}/unity.c)
target_include_directories(unity SYSTEM PUBLIC ${UNITY_DIR})
target_compile_options(unity PRIVATE -w)

# ---- unit tests (UT-*) ----
foreach(t ring crc16 cobs frame msg cmd plaus hist bytes cmd_vectors)
  add_executable(test_${t} unit/test_${t}.c)
  target_link_libraries(test_${t} PRIVATE core unity)
  add_test(NAME ut_${t} COMMAND test_${t})
endforeach()
target_compile_definitions(test_cmd_vectors PRIVATE VECTORS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/vectors")

# ---- cross-implementation vectors (XV): committed output must match the generator ----
add_executable(gen_vectors vectors/gen_vectors.c)
target_link_libraries(gen_vectors PRIVATE core)
set(XV_DIR ${CMAKE_CURRENT_BINARY_DIR}/vectors_check)
file(MAKE_DIRECTORY ${XV_DIR})
add_test(NAME xv_generate COMMAND gen_vectors ${XV_DIR})
add_test(NAME xv_vectors_up_to_date COMMAND diff -r ${XV_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/vectors/out)
set_tests_properties(xv_generate PROPERTIES FIXTURES_SETUP xv)
set_tests_properties(xv_vectors_up_to_date PROPERTIES FIXTURES_REQUIRED xv)

# ---- concurrency stress (ST-RING-1); run under TSan via `make test-tsan` ----
find_package(Threads REQUIRED)
add_executable(ring_tsan stress/ring_tsan.c)
target_link_libraries(ring_tsan PRIVATE core Threads::Threads)
add_test(NAME st_ring_fast COMMAND ring_tsan 10000000)
add_test(NAME st_ring_slow_consumer COMMAND ring_tsan 1000000 slow)
set_tests_properties(st_ring_fast st_ring_slow_consumer PROPERTIES LABELS stress)

# ---- fuzzers (FZ-1, FZ-2); `make fuzz` ----
if(FUZZ)
  foreach(f fuzz_frame fuzz_cmd)
    add_executable(${f} fuzz/${f}.c)
    target_link_libraries(${f} PRIVATE core)
    target_compile_options(${f} PRIVATE -fsanitize=fuzzer $<$<BOOL:${APPLE}>:-fno-sanitize=function>)
    target_link_options(${f} PRIVATE -fsanitize=fuzzer)
    set_target_properties(${f} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/fuzz)
  endforeach()
endif()
