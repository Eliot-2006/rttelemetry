# Scheduling analysis (M8)

`tools/rta.py` checks that the FreeRTOS task set meets its deadlines, using
utilization and exact response-time analysis (RTA). M8 compares its predictions
with measured response times. The analysis is **safe** only if, for every task,
predicted ≥ observed.

## Model and assumptions

- **Single core, fixed priorities, fully preemptive.** This matches
  `configUSE_PREEMPTION 1`. A higher number means a higher priority, as in
  FreeRTOS.
- **Equal priorities interfere with each other.** FreeRTOS time-slices tasks
  of the same priority, so each counts as higher priority for the other. This
  is conservative. PRD §8.1 gives every task a unique priority.
- **ISRs preempt every task.** Each ISR is modelled as periodic work with
  period `T` (or minimum inter-arrival time for sporadic IRQs) and execution
  time `C`. It interferes with every task as `⌈w/T⌉·C`. The SysTick handler
  appears explicitly as an ISR entry with period `tick_us`. The `tick_us` field
  itself is informational. Kernel-masked sections (BASEPRI) and PendSV
  context-switch cost are not modelled separately. Fold them into the measured
  WCETs.
- **Blocking `B`.** Shared resources use FreeRTOS mutexes with priority
  inheritance (`stats_mutex`). Task *i* can be blocked at most once per job.
  The bound is the **longest critical section of any lower-priority task on a
  mutex that task *i* or a higher-priority task also locks**. The lowest-priority
  task has `B = 0`. Binary semaphores give no inheritance, so this bound does
  not hold for them. That is the M8 `PRIO_INV` variant A experiment.
- **Release jitter `J`.** `J` is the latest a job can be released after its
  nominal arrival. One example is a notify delayed by a masked ISR. Another is a
  `vTaskDelayUntil` release that is quantized to the tick.
- **Deadlines** default to the period. A deadline shorter than the period is
  supported.

### Equations

    U  = Σ C/T over tasks and ISRs
    LL = n(2^(1/n) − 1)                      n = number of tasks + ISRs
    w  = C_i + B_i + Σ_{j∈hp(i)} ⌈(w + J_j)/T_j⌉·C_j + Σ_{isr k} ⌈w/T_k⌉·C_k
    R_i = w + J_i                            (Burns & Wellings jitter form)
    slack_i = D_i − R_i

Iteration starts at `w = C_i + B_i`. It stops at a fixed point, or as soon as
`R_i > D_i`, in which case the task is reported **UNSCHEDULABLE**.

`U ≤ LL` is a *sufficient* test only, and only for rate-monotonic priorities
with `D = T`. If it fails, the exact RTA decides. Our priorities are
deadline-monotonic (PRD §8.1), so the RTA result is the one that counts.

## Running it

    python3 tools/rta.py tools/tasksets/telemetry_nominal.json [--include-optional]
    python3 tools/rta.py tasks.json --observed observed.json --json out.json
    cd tools && python3 -m unittest test_rta -v

- `--include-optional` adds the entries marked `"optional": true`, which are the
  P1 IMU task and its ISRs.
- `--observed` takes `{"task_name": max_observed_response_us}`. The report then
  shows the observed value and the ratio predicted/observed. Any task where
  observed > predicted is flagged `UNSAFE`.
- Exit codes:
  - `0`: OK.
  - `1`: the analysis is unsafe (observed > predicted).
  - `2`: the task set is unschedulable.
- The unit tests use textbook task sets from Burns & Wellings and Buttazzo. Each
  test's comment says whether its expected values are published or
  hand-derived.

## Plugging in measured WCETs (M8)

`tools/tasksets/telemetry_nominal.json` uses **placeholder** values for every
`wcet_us` and `blocking_us`. The ISR values are the PRD §7.1 budgets. The task
values are guesses. No measured numbers exist yet. To replace them:

1. Run the stress and fault-injection suite with per-task job instrumentation.
   It records release, start, finish, max execution, and max response, using
   TIM2 µs in `[EMU]` and DWT `CYCCNT` in `[HW]`.
2. For each task, set `wcet_us` to the **max execution time** observed. Use
   preemption-excluded time, which is what the instrumentation records. Do the
   same for each ISR, using its max entry-to-exit time. Set `blocking_us` to the
   longest measured critical section of any lower-priority task on a shared mutex.
3. Write a copy of the task set with these values, labelled `[EMU]` or `[HW]`.
   Also write the measured max response times as `observed.json`.
4. Run `rta.py <measured set> --observed observed.json`. M8 passes when every
   task is schedulable and none is `UNSAFE`. Report the predicted/observed
   ratios. A ratio well above 1 is expected, because RTA assumes the worst-case
   phasing of all releases.

Measured execution times are maxima from a finite run, not proven WCETs. Treat
the result as evidence, not as proof.

## As built (M7)

The IMU is polled (D11): `imu_task` runs every 5 ms at priority 6 and busy-waits on a 14-byte I²C
burst, about 400 µs at 400 kHz `[CALC]`. The task set models that, with no EXTI or I²C interrupts.
Every WCET is still a placeholder. Renode is not cycle-accurate (D8), so emulated execution times
would not give meaningful WCETs. Measured values need hardware (DWT cycle counter), see
`docs/hardware.md`.
