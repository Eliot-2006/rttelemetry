*** Comments ***
PRD M0 platform gates. Runs tests/platform/gates.c on the project Renode overlay and checks
every emulator behaviour the telemetry firmware relies on. All waits are virtual-time.

*** Settings ***
Suite Setup       Setup
Suite Teardown    Teardown
Test Teardown     Test Teardown
Resource          ${RENODEKEYWORDS}

*** Variables ***
${ROOT}           ${CURDIR}/../..
${ELF}            ${ROOT}/build/fw-debug/src/platform_gates.elf
${DATA}           ${ROOT}/renode/data/gates

*** Keywords ***
Create Machine
    Execute Command           $elf=@${ELF}
    Execute Command           include @${ROOT}/renode/telemetry.resc
    FOR    ${ch}    IN    1    8    16    17
        Execute Command       sysbus.adc1 FeedSample @${DATA}/ch${ch}.dat ${ch}
    END
    # G7: one accelerometer sample (0.5, -0.25, 1.0 g)
    Execute Command           sysbus.i2c1.imu FeedAccelerationSample 0.5 -0.25 1.0
    Create Terminal Tester    sysbus.usart2    defaultPauseEmulation=True

Read Timer2
    ${v}=    Execute Command    sysbus ReadDoubleWord 0x40000024
    RETURN    ${{ int($v.strip(), 16) }}

*** Test Cases ***
Platform Gates
    Create Machine
    Wait For Line On Uart     G1 PASS sysclk=168000000
    Wait For Line On Uart     G4 PASS
    Wait For Line On Uart     G5 DMA ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZ    treatAsRegex=False
    Wait For Line On Uart     G5 PASS tc=1
    Wait For Line On Uart     G6 DONE scans=1000 first=0,4095,1000,1500 sum=499500,3595500,1002997,1500000
    Wait For Line On Uart     G7 WHOAMI PASS
    Wait For Line On Uart     G7 PASS
    Wait For Line On Uart     G10 WAIT_RX
    Write Char On Uart        X
    Wait For Line On Uart     G10 PASS rx=0x00000058
    Wait For Line On Uart     G8 ARMED
    Wait For Line On Uart     G8 PASS noinit survived reset
    Wait For Line On Uart     G9 PASS
    Wait For Line On Uart     GATES DONE
    # G2: TIM2 runs at 1 MHz of virtual time (overlay timer clock override works)
    ${t0}=    Read Timer2
    Execute Command           emulation RunFor "00:00:01.000"
    ${t1}=    Read Timer2
    ${delta}=    Evaluate    (${t1} - ${t0}) & 0xFFFFFFFF
    Log To Console            G2 TIM2 delta over 1 s virtual = ${delta}
    Should Be True            abs(${delta} - 1000000) <= 1000
