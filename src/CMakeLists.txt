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
