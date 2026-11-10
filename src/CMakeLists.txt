# Firmware images (cross build only). Platform objects are shared by every image.
set(DEV ${PROJECT_SOURCE_DIR}/third_party/cmsis-device-f4)
set(STARTUP ${DEV}/Source/Templates/gcc/startup_stm32f407xx.s)
set(SYSTEM  ${DEV}/Source/Templates/system_stm32f4xx.c)
set_source_files_properties(${STARTUP} ${SYSTEM} PROPERTIES COMPILE_OPTIONS "-w")

add_library(platform OBJECT
  ${STARTUP} ${SYSTEM}
  platform/syscalls.c
  drivers/clock.c drivers/uart.c drivers/tim.c drivers/adc.c drivers/iwdg.c drivers/i2c.c)
target_include_directories(platform PUBLIC
  drivers platform
  ${DEV}/Include
  ${PROJECT_SOURCE_DIR}/third_party/CMSIS_6/CMSIS/Core/Include)
target_compile_definitions(platform PUBLIC STM32F407xx HSE_VALUE=8000000U)

set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/platform/stm32f407vg.ld)

function(add_firmware name)
  add_executable(${name} ${ARGN})
  set_target_properties(${name} PROPERTIES SUFFIX .elf LINK_DEPENDS ${LINKER_SCRIPT})
  target_link_libraries(${name} PRIVATE platform)
  target_link_options(${name} PRIVATE -T${LINKER_SCRIPT} -Wl,-Map=$<TARGET_FILE_DIR:${name}>/${name}.map)
  add_custom_command(TARGET ${name} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${name}> $<TARGET_FILE_DIR:${name}>/${name}.bin
    COMMAND ${CMAKE_SIZE} -A -d $<TARGET_FILE:${name}> > $<TARGET_FILE_DIR:${name}>/${name}.size.txt
    VERBATIM)
endfunction()

add_firmware(platform_gates ${PROJECT_SOURCE_DIR}/tests/platform/gates.c)

# ---- FreeRTOS kernel (vendor code: built without our -Werror warning set) ----
set(RTOS ${PROJECT_SOURCE_DIR}/third_party/FreeRTOS-Kernel)
add_library(freertos STATIC
  ${RTOS}/tasks.c ${RTOS}/queue.c ${RTOS}/list.c ${RTOS}/portable/GCC/ARM_CM4F/port.c)
target_include_directories(freertos PUBLIC ${RTOS}/include ${RTOS}/portable/GCC/ARM_CM4F platform)
target_link_libraries(freertos PUBLIC platform)
target_compile_options(freertos PRIVATE -w)

# ---- telemetry application ----
option(FAULT_INJECTION "Compile fault-injection commands (debug/test builds)" OFF)
execute_process(COMMAND git rev-parse --short=8 HEAD WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
                OUTPUT_VARIABLE GIT_SHA OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT GIT_SHA)
  set(GIT_SHA "00000000")
endif()

add_firmware(telemetry
  app/main.c app/acq.c app/proc.c app/telemetry.c app/health.c app/uplink.c app/fault_inject.c app/imu.c
  platform/fault.c)
target_include_directories(telemetry PRIVATE app)
target_link_libraries(telemetry PRIVATE freertos core)
target_compile_definitions(telemetry PRIVATE
  FAULT_INJECTION=$<BOOL:${FAULT_INJECTION}>
  FW_GIT_SHA="${GIT_SHA}"
  FW_BUILD_RELEASE=$<IF:$<CONFIG:Release>,1,0>)
