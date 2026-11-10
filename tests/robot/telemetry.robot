*** Comments ***
PRD §19.3 integration tests on Renode (virtual time; results are [EMU], not hardware).
Each test boots telemetry.elf with an ADC feed scenario, captures USART2 to a file and decodes it
with tools/telemetry.

*** Settings ***
Resource          common.resource
Test Setup        Reset Emulation
Test Teardown     Test Teardown

*** Test Cases ***
IT-BOOT-1 Boot frame
    Start Telemetry    nominal
    Run For    0.1
    Capture
    Boot Should Be    0    reason=0    max_t_us=100000

IT-HEALTH-1 Health cadence and tick rate (G3)
    Start Telemetry    nominal
    Run For    5.05
    Capture
    Health Cadence Should Be    5    period_us=1000000    tol_us=1000

IT-ACQ-1 1 kHz nominal feed, 10 s
    Start Telemetry    nominal
    Run For    10
    Stop And Flush
    Capture
    No Rejects
    ${n}=    Samples Should Be Contiguous    ${{ 1000 - ${TIM_DT_OFFSET} }}
    Should Be True    abs(${n} - 10e6 / (1000 - ${TIM_DT_OFFSET})) <= 3    ${n} samples
    Samples Should Match Feed    nominal
    Conservation Should Be Exact

IT-ACQ-2 Sample rates
    Start Telemetry    nominal
    Run For    0.1
    FOR    ${hz}    IN    100    250    500    2000
        Send Command    set-rate    ${hz}
        Run For    1
        Capture
        ${t0}=    Last Ack T Us    ${0x82}
        ${n}=    Samples Should Be Contiguous    ${{ 1000000 // ${hz} - ${TIM_DT_OFFSET} }}    after_t_us=${t0}
        Should Be True    ${n} >= ${hz} * 0.9    ${n} samples at ${hz} Hz
        ${ovr}=    Health Value    dropped_overrun
        Should Be Equal As Integers    ${ovr}    0
    END
    Stop And Flush
    Capture
    Conservation Should Be Exact

IT-ACQ-3 Batch flush at 100 Hz
    Start Telemetry    nominal
    Run For    0.1
    Send Command    set-rate    100
    Run For    2
    Capture
    ${l1}=    Health Value    l1_max_us
    Should Be True    ${l1} <= 21000    L1 max ${l1} us
    ${sizes}=    Batch Sizes    20
    Should Be True    max(${sizes}) < 10    batch sizes ${sizes}

IT-CMD-1 Commands
    Start Telemetry    nominal
    Run For    0.2
    Send Command    ping
    Send Command    get-status
    Send Command    set-rate    333
    Send Command    reset-counters
    Run For    0.03
    Capture
    Ack Should Be    ${0x80}    0
    Ack Should Be    ${0x81}    0
    Ack Should Be    ${0x82}    3
    Ack Should Be    ${0x83}    0
    ${h}=    Health Count
    Should Be True    ${h} >= 1    GET_STATUS produced no HEALTH within 30 ms
    Send Command    reboot
    Run For    0.2
    Capture
    Ack Should Be    ${0x84}    0
    Boot Should Be    1    reason=1

IT-IMU-1 IMU polled at 200 Hz alongside the ADC path
    [Documentation]    D11: no DRDY in Renode 1.17.0, so imu_task polls every 5 ms.
    ...                +-4 g: 8192 LSB/g; 500 dps: 17.5 mdps/LSB; temperature 256 LSB/C from 25 C.
    Start Telemetry    nominal
    Run For    2.05
    Capture
    Imu Samples Should Be    4096,-2048,8192    571,-1143,14286    1280    380
    ${st}=    Health Value    imu_status
    Should Be Equal As Integers    ${st}    1
    ${d}=    Health Value    imu_dropped
    Should Be Equal As Integers    ${d}    0
    Samples Should Match Feed    nominal

IT-ACCT-1 Exact conservation after stop
    Start Telemetry    nominal
    Run For    3
    Stop And Flush
    Capture
    Conservation Should Be Exact

FI-03 Range fault flags
    Start Telemetry    range_fault
    Run For    1.2
    Capture
    Samples Should Match Feed    range_fault
    ${seqs}=    Flagged Seqs    0    1
    Should Be Equal    ${seqs}    ${{ list(range(300, 310)) }}

FI-04 Stuck fault onset
    Start Telemetry    stuck_fault
    Run For    1.2
    Capture
    Samples Should Match Feed    stuck_fault
    ${seqs}=    Flagged Seqs    1    2
    Should Be Equal    ${seqs}    ${{ list(range(449, 500)) }}

FI-04b Rate-of-change fault
    Start Telemetry    roc_fault
    Run For    1.2
    Capture
    Samples Should Match Feed    roc_fault
    ${seqs}=    Flagged Seqs    2    4
    Should Be Equal    ${seqs}    ${{ [600, 601] }}

IT-GOLD-1 Deterministic output, 3 runs
    [Documentation]    Golden file: tests/golden/nominal.jsonl (git_sha excluded). Firmware changes alter
    ...                timing fields; regenerate with `make golden` and review the diff.
    FOR    ${i}    IN RANGE    3
        Reset Emulation
        Start Telemetry    nominal
        Run For    5
        Capture
        Save Jsonl    ${OUTPUT DIR}/gold_${i}.jsonl
    END
    ${a}=    Get File    ${OUTPUT DIR}/gold_0.jsonl
    ${b}=    Get File    ${OUTPUT DIR}/gold_1.jsonl
    ${c}=    Get File    ${OUTPUT DIR}/gold_2.jsonl
    Should Be Equal    ${a}    ${b}    run 1 differs from run 0
    Should Be Equal    ${a}    ${c}    run 2 differs from run 0
    IF    ${UPDATE_GOLDEN}
        Copy File    ${OUTPUT DIR}/gold_0.jsonl    ${ROOT}/tests/golden/nominal.jsonl
    END
    ${g}=    Get File    ${ROOT}/tests/golden/nominal.jsonl
    Should Be Equal    ${a}    ${g}    output differs from tests/golden/nominal.jsonl
