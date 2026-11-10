*** Settings ***
Library           OperatingSystem
Library           TelemetryLibrary.py
Resource          ${RENODEKEYWORDS}

*** Variables ***
${ROOT}           ${CURDIR}/../..
${ELF}            ${ROOT}/build/fw-debug/src/telemetry.elf
# D15: Renode 1.17.0 STM32_Timer raises the update event every ARR ticks (silicon: ARR + 1), so an
# emulated sample period is 1 us shorter than nominal. Firmware keeps the silicon-correct ARR.
${TIM_DT_OFFSET}  1
# imu_task stack in words (HEALTH order: proc, tx, cmd, health, imu, idle).
${IMU_STACK}      384
${UPDATE_GOLDEN}  ${False}

*** Keywords ***
Start Telemetry
    [Documentation]    Boot ${ELF} with ADC feed scenario renode/data/${scenario}/, capturing USART2.
    [Arguments]    ${scenario}
    ${name}=    Evaluate    re.sub(r'\\W+', '_', $TEST_NAME)    modules=re
    Set Test Variable    ${CAP}    ${OUTPUT DIR}/${name}.bin
    Remove File    ${CAP}
    Execute Command    $elf=@${ELF}
    Execute Command    include @${ROOT}/renode/telemetry.resc
    FOR    ${ch}    IN    1    8    16    17
        Execute Command    sysbus.adc1 FeedSample @${ROOT}/renode/data/${scenario}/ch${ch}.dat ${ch} -1
    END
    # Constant IMU input (0.5, -0.25, 1.0) g, (10, -20, 250) dps, 30 C; enough samples for 500 s at 200 Hz.
    Execute Command    sysbus.i2c1.imu FeedAccelerationSample 0.5 -0.25 1.0 100000
    Execute Command    sysbus.i2c1.imu FeedAngularRateSample 10 -20 250 100000
    Execute Command    sysbus.i2c1.imu Temperature 30
    Execute Command    sysbus.usart2 CreateFileBackend @${CAP} true

Run For
    [Arguments]    ${seconds}
    Execute Command    emulation RunFor "${seconds}"

Capture
    Load Capture    ${CAP}

Send Command
    [Documentation]    Inject one uplink command frame into USART2 RX (queued; delivered as the emulation runs).
    [Arguments]    ${name}    @{args}
    ${cmd}=    Command Inject    ${name}    @{args}
    Execute Command    ${cmd}

Stop And Flush
    [Documentation]    SET_RATE 0 stops sampling; proc flushes the partial batch within FLUSH_US;
    ...                GET_STATUS then reports final counters (IT-ACCT-1 precondition).
    Send Command    set-rate    0
    Run For    0.1
    Send Command    get-status
    Run For    0.15
