/* Platform gate firmware (PRD M0, gates G1-G11). Bare metal, no RTOS: proves each emulator
 * behaviour the telemetry firmware depends on, using the production drivers.
 * Kept as a permanent platform regression suite (tests/platform/gates.robot). */
#include <string.h>
#include "drivers.h"

#define G8_MAGIC 0x6A7E0008u
__attribute__((section(".noinit"))) static volatile uint32_t g8_magic;

static volatile uint32_t exti0_ran, exti1_ran, tx_done, adc_done, rx_byte, rx_count;
static volatile uint32_t scan[ADC_NCH];
static uint8_t g5_buf[200];

static void say(const char *s) { uart_write_polled(s, strlen(s)); }
static void say_u(uint32_t v)
{
    char b[11]; int i = 10; b[i] = 0;
    do { b[--i] = (char)('0' + v % 10u); v /= 10u; } while (v);
    say(&b[i]);
}
static void say_x(uint32_t v)
{
    char b[11] = "0x00000000";
    for (int i = 0; i < 8; i++) b[9 - i] = "0123456789ABCDEF"[(v >> (4 * i)) & 0xFu];
    say(b);
}
static void say_i(int32_t v) { if (v < 0) { say("-"); say_u((uint32_t)-v); } else say_u((uint32_t)v); }
static bool spin_until(volatile uint32_t *flag, uint32_t want)
{
    for (uint32_t i = 0; i < 5000000u; i++) if (*flag >= want) return true;
    return false;
}

void EXTI0_IRQHandler(void) { exti0_ran++; }
void EXTI1_IRQHandler(void) { exti1_ran++; }
void uart_tx_done_isr(void) { tx_done++; }
void uart_rx_byte_isr(uint8_t b) { rx_byte = b; rx_count++; }
void DMA2_Stream0_IRQHandler(void) { if (adc_dma_take_complete()) adc_done++; }

/* G9: HardFault with stacked frame. Naked stub picks MSP/PSP from EXC_RETURN. */
extern char gate_udf_site[];
void hardfault_c(const uint32_t *frame)
{
    uint32_t pc = frame[6];
    say("G9 ");
    say(pc == ((uint32_t)gate_udf_site & ~1u) ? "PASS" : "FAIL");
    say(" pc="); say_x(pc); say(" expect="); say_x((uint32_t)gate_udf_site & ~1u);
    say(" cfsr="); say_x(SCB->CFSR); say("\r\nGATES DONE\r\n");
    for (;;) {}
}
__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile("tst lr, #4\n ite eq\n mrseq r0, msp\n mrsne r0, psp\n b hardfault_c\n");
}

static void g4_basepri(void)
{
    NVIC_SetPriority(EXTI0_IRQn, 4); NVIC_SetPriority(EXTI1_IRQn, 6);
    NVIC_EnableIRQ(EXTI0_IRQn); NVIC_EnableIRQ(EXTI1_IRQn);
    __set_BASEPRI(5u << (8u - __NVIC_PRIO_BITS));          /* what FreeRTOS critical sections do */
    __DSB(); __ISB();
    NVIC_SetPendingIRQ(EXTI0_IRQn); NVIC_SetPendingIRQ(EXTI1_IRQn);
    __DSB(); __ISB();
    for (volatile int i = 0; i < 1000; i++) {}
    uint32_t hi_in = exti0_ran, lo_in = exti1_ran;
    __set_BASEPRI(0); __DSB(); __ISB();
    for (volatile int i = 0; i < 1000; i++) {}
    say((hi_in == 1 && lo_in == 0 && exti1_ran == 1) ? "G4 PASS" : "G4 FAIL");
    say(" hi_in_crit="); say_u(hi_in); say(" lo_in_crit="); say_u(lo_in); say(" lo_after="); say_u(exti1_ran); say("\r\n");
}

static void g5_uart_dma(void)
{
    memcpy(g5_buf, "G5 DMA ", 7);
    for (int i = 7; i < 198; i++) g5_buf[i] = (uint8_t)('A' + (i - 7) % 26);
    g5_buf[198] = '\r'; g5_buf[199] = '\n';
    bool started = uart_tx_dma_start(g5_buf, sizeof g5_buf);
    bool ok = started && spin_until(&tx_done, 1);
    for (volatile int i = 0; i < 10000; i++) {}
    say((ok && tx_done == 1 && DMA_ADDR_OK(g5_buf)) ? "G5 PASS" : "G5 FAIL");
    say(" tc="); say_u(tx_done); say("\r\n");
}

static void g6_adc(void)
{
    adc_init();
    uint32_t sum[ADC_NCH] = {0}, first[ADC_NCH] = {0};
    uint32_t t0 = now_us();
    uint32_t n = 0;
    for (; n < 1000u; n++) {
        uint32_t before = adc_done;
        adc_start_scan(scan);
        if (!spin_until(&adc_done, before + 1)) break;
        for (unsigned c = 0; c < ADC_NCH; c++) { sum[c] += scan[c]; if (n == 0) first[c] = scan[c]; }
    }
    uint32_t dt = now_us() - t0;
    say(n == 1000u ? "G6 DONE" : "G6 FAIL");
    say(" scans="); say_u(n);
    say(" first="); for (unsigned c = 0; c < ADC_NCH; c++) { say_u(first[c]); say(c + 1 < ADC_NCH ? "," : ""); }
    say(" sum="); for (unsigned c = 0; c < ADC_NCH; c++) { say_u(sum[c]); say(c + 1 < ADC_NCH ? "," : ""); }
    say(" us_per_scan="); say_u(n ? dt / n : 0); say("\r\n");
}

static void g7_imu(void)
{
    /* Renode 1.17.0's LSM6DSO does not model INT1_DRDY (divergence D11): the IMU is sampled
     * by period + STATUS_REG polling, so this gate checks the I2C paths and data flow only. */
    i2c_init();
    uint8_t who = 0;
    i2c_status_t s = i2c_read_reg(0x6A, 0x0F, &who, 1);                 /* N=1 path */
    say((s == I2C_OK && who == 0x6C) ? "G7 WHOAMI PASS" : "G7 WHOAMI FAIL");
    say(" st="); say_u(s); say(" who="); say_x(who); say("\r\n");

    i2c_write_reg(0x6A, 0x12, 0x44);   /* CTRL3_C: BDU | IF_INC */
    i2c_write_reg(0x6A, 0x10, 0x40);   /* CTRL1_XL: 104 Hz, +-2 g */
    uint32_t t0 = now_us();
    while (now_us() - t0 < 50000u) {}  /* > 5 sample periods */
    uint8_t raw[6] = {0}, two[2] = {0};
    s = i2c_read_reg(0x6A, 0x28, raw, 6);                                /* OUTX_L_A.., N>2 path */
    i2c_status_t s2 = i2c_read_reg(0x6A, 0x0F, two, 2);                  /* N=2 path */
    say((s == I2C_OK && s2 == I2C_OK && two[0] == 0x6C) ? "G7 PASS" : "G7 FAIL");
    say(" st="); say_u(s); say(" st2="); say_u(s2); say(" who2="); say_x(two[0]);
    for (int a = 0; a < 3; a++) { say(a == 0 ? " ax=" : a == 1 ? " ay=" : " az="); say_i((int16_t)(raw[2 * a] | raw[2 * a + 1] << 8)); }
    say("\r\n");
}

static void g10_rx(void)
{
    uart_rx_enable_irq();
    say("G10 WAIT_RX\r\n");
    bool got = spin_until(&rx_count, 1);
    say(got ? "G10 PASS" : "G10 FAIL");
    say(" rx="); say_x(rx_byte); say("\r\n");
}

int main(void)
{
    bool clk = clock_init();
    uart_init(TELEM_BAUD);
    tim2_timebase_init();
    if (g8_magic == G8_MAGIC) {
        g8_magic = 0;
        say("G8 PASS noinit survived reset\r\n");
        say("G9 ARMED\r\n");
        __asm volatile(".global gate_udf_site\n.thumb_func\ngate_udf_site: udf #0\n");
        for (;;) {}
    }
    say(clk ? "G1 PASS" : "G1 FAIL"); say(" sysclk="); say_u(SystemCoreClock);
    say(" rcc_csr="); say_x(RCC->CSR); say("\r\n");
    say("G2 READY\r\n");
    g4_basepri();
    g5_uart_dma();
    g6_adc();
    g7_imu();
    g10_rx();
    g8_magic = G8_MAGIC;
    iwdg_start(50);
    say("G8 ARMED\r\n");
    for (;;) {}
}
