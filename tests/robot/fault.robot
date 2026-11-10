*** Comments ***
PRD §19.4 fault-injection catalog and IT-STRESS-1 / IT-RELEASE-1 on Renode ([EMU] results).
FI_ARM ids: 1 STALL_PROC, 2 TX_DELAY, 3 HANG_TASK, 4 UDF, 5 STACK_OVF, 6 ASSERT, 7 DELAY_DMA_ISR.
Reset reasons: 1 CMD, 2 WDT_TASK_STALL, 3 HARDFAULT, 4 STACK_OVF, 5 ASSERT.

*** Settings ***
Resource          common.resource
Test Setup        Reset Emulation
Test Teardown     Test Teardown

*** Variables ***
${ELF_RELEASE}    ${ROOT}/build/fw-release/src/telemetry.elf

*** Keywords ***
Should Have Booted Once
    ${b}=    Boots
    Length Should Be    ${b}    1    unexpected reset: ${b}

Event Should Have Occurred
    [Arguments]    ${id}
    ${ids}=    Event Ids
    Should Contain    ${ids}    ${{ int(${id}) }}

Run Until Samples After Reboot
    [Documentation]    Step 100 ms at a time until the second boot delivers samples. Returns the
    ...                virtual seconds elapsed; fails if that exceeds ${limit_s}.
    [Arguments]    ${limit_s}
    FOR    ${i}    IN RANGE    1    ${{ int(round(float(${limit_s}) * 10)) + 1 }}
        Run For    0.1
        Capture
        ${n}=    Samples After Reboot
        IF    ${n} > 0    RETURN    ${{ ${i} / 10 }}
    END
    Fail    no samples within ${limit_s} s of the fault

*** Test Cases ***
FI-01 Processing stall overflows the ring, then drains
    Start Telemetry    nominal
    Run For    0.5
    Send Command    fi-arm    1    300
    Run For    1
    Stop And Flush
    Capture
    Should Have Booted Once
    ${drops}=    Health Value    dropped_ring
    # 300 ms stall at 999 us/sample (D15) = 300 samples; the ring absorbs RING_CAP = 128.
    Should Be True    abs(${drops} - (300 - 128)) <= 10    dropped_ring ${drops}
    Event Should Have Occurred    3
    Event Should Have Occurred    4
    Conservation Should Be Exact

FI-02 Slow link exhausts the frame pool, not the ring
    Start Telemetry    nominal
    Run For    0.5
    Send Command    fi-arm    2    200
    Run For    1
    Send Command    fi-clear
    Run For    0.5
    Stop And Flush
    Capture
    Should Have Booted Once
    ${pool}=    Health Value    dropped_txpool
    ${ring}=    Health Value    dropped_ring
    Should Be True    ${pool} > 0    dropped_txpool ${pool}
    Should Be Equal As Integers    ${ring}    0
    Event Should Have Occurred    5
    Event Should Have Occurred    6
    Conservation Should Be Exact

FI-05 Malformed uplink frames
    Start Telemetry    nominal
    Run For    0.2
    ${chunks}=    Malformed Uplink Chunks    1
    # 180 chunks of 5 small frames, then 100 oversize frames (260-400 B) one per chunk; the
    # emulated UART delivers RX bytes at a finite rate, so oversize chunks get more time.
    FOR    ${c}    IN    @{chunks}[:180]
        Execute Command    ${c}
        Run For    0.01
    END
    FOR    ${c}    IN    @{chunks}[180:]
        Execute Command    ${c}
        Run For    0.05
    END
    Send Command    ping
    Send Command    get-status
    Run For    0.1
    Capture
    Should Have Booted Once
    Malformed Counts Should Match    base_ok=0    base_bad=0
    Ack Should Be    ${0x80}    0

FI-06 Hung task is reset by the watchdog and named
    Start Telemetry    nominal
    Run For    0.5
    Send Command    fi-arm    3    2
    ${t}=    Run Until Samples After Reboot    2.0
    ${b}=    Boot Should Be    1    reason=2
    Should Be Equal    ${b}[task]    cmd
    Log    telemetry resumed ${t} s after the hang was armed [EMU]

FI-06b Watchdog reset during an in-flight ADC scan
    [Documentation]    Armed at 1.0 s, the IWDG reset lands mid-scan (1.9998 s virtual). Renode keeps the
    ...                ADC-to-DMA request state across the reset (D16), so the first scan after boot stops
    ...                short. The TIM3 scan timeout must abort it, count it as an overrun, and restart.
    Start Telemetry    nominal
    Run For    1.0
    Send Command    fi-arm    3    2
    ${t}=    Run Until Samples After Reboot    2.0
    Run For    1.0
    Stop And Flush
    Capture
    Boot Should Be    1    reason=2
    ${ovr}=    Health Value    dropped_overrun
    Should Be True    ${ovr} <= 3    ${ovr} overruns after the reset
    Conservation Should Be Exact

FI-12 I2C NACKs: retried, then offline and recovered
    Start Telemetry    nominal
    Run For    0.3
    Send Command    fi-arm    8    2
    Run For    0.2
    Send Command    get-status
    Run For    0.05
    Capture
    ${e}=    Health Value    i2c_errors
    ${st}=    Health Value    imu_status
    Should Be Equal As Integers    ${e}    2    two NACKs absorbed by the retries
    Should Be Equal As Integers    ${st}    1
    Send Command    fi-arm    8    3
    Run For    0.1
    Send Command    get-status
    Run For    0.05
    Capture
    ${st}=    Health Value    imu_status
    Should Be Equal As Integers    ${st}    2    OFFLINE after 3 failed attempts
    Event Should Have Occurred    15
    Run For    1.2
    Stop And Flush
    Capture
    ${st}=    Health Value    imu_status
    ${e}=    Health Value    i2c_errors
    Should Be Equal As Integers    ${st}    1    re-probe after 1 s brought it back
    Should Be Equal As Integers    ${e}    5
    Event Should Have Occurred    16
    Should Have Booted Once
    ${ring}=    Health Value    dropped_ring
    ${ovr}=    Health Value    dropped_overrun
    Should Be Equal As Integers    ${ring}    0
    Should Be Equal As Integers    ${ovr}    0
    Conservation Should Be Exact

FI-07 Undefined instruction: HardFault with the exact PC
    Start Telemetry    nominal
    Run For    0.3
    Send Command    fi-arm    4    0
    Run For    0.3
    Capture
    ${b}=    Boot Should Be    1    reason=3
    ${site}=    Symbol Address    ${ELF}    fi_udf_site
    Should Be Equal As Integers    ${b}[pc]    ${site}
    Should Be Equal As Integers    ${b}[cfsr]    ${0x10000}

FI-08 Stack overflow in a sacrificial task
    Start Telemetry    nominal
    Run For    0.3
    Send Command    fi-arm    5    0
    Run For    0.5
    Capture
    ${b}=    Boot Should Be    1    reason=4
    Should Be Equal    ${b}[task]    fi

FI-09 Failed assertion records file and line
    Start Telemetry    nominal
    Run For    0.3
    Send Command    fi-arm    6    0
    Run For    0.3
    Capture
    ${b}=    Boot Should Be    1    reason=5
    ${line}=    Source Line    ${ROOT}/src/app/fault_inject.c    case FAULT_ASSERT: configASSERT\\(0\\)
    Should Be Equal As Integers    ${b}[detail]    ${line}
    Should End With    ${b}[file]    fault_inject.c

FI-10 Slow DMA ISR causes counted ADC overruns
    Start Telemetry    nominal
    Run For    0.3
    Send Command    fi-arm    7    1500
    Run For    0.5
    Send Command    fi-clear
    Run For    0.2
    Stop And Flush
    Capture
    Should Have Booted Once
    ${ovr}=    Health Value    dropped_overrun
    ${gaps}=    Seq Gaps
    Should Be True    ${ovr} > 0
    Should Be Equal As Integers    ${gaps}    ${ovr}
    Conservation Should Be Exact

IT-STRESS-1 8 kHz with stalls, slow link and uplink spam, 60 s
    Start Telemetry    nominal
    Run For    0.2
    Send Command    set-rate    8000
    FOR    ${i}    IN RANGE    6
        Send Command    fi-arm    1    150
        Run For    3
        Send Command    fi-arm    2    40
        FOR    ${j}    IN RANGE    20
            Send Command    ping
            Run For    0.1
        END
        Send Command    fi-clear
        Run For    5
    END
    Stop And Flush
    Capture
    Should Have Booted Once
    Conservation Should Be Exact
    Stack Margins Should Be At Least    25    384,384,384,384,${IMU_STACK},128

IT-RELEASE-1 Release build rejects fault injection
    Set Test Variable    ${ELF}    ${ELF_RELEASE}
    Start Telemetry    nominal
    Run For    0.2
    Send Command    fi-arm    4    0
    Send Command    ping
    Run For    0.1
    Capture
    Should Have Booted Once
    Ack Should Be    ${0x90}    4
    Ack Should Be    ${0x80}    0
    ${b}=    Boot Should Be    0    reason=0
    Should Be Equal As Integers    ${b}[fault_injection]    0
    Should Be Equal As Integers    ${b}[build_type]    1
