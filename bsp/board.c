/* SPDX-License-Identifier: GPL-3.0-or-later */
/* H8i8o board support; see board.h. Register addresses and fields: GD32F10x
 * user manual (RCU, FMC, AFIO, GPIO, FWDGT) and the Cortex-M3 core. */
#include "board.h"

#define REG(addr) (*(volatile uint32_t *)(addr))

/* RCU */
#define RCU_CTL REG(0x40021000u)
#define RCU_CFG0 REG(0x40021004u)
#define RCU_APB2EN REG(0x40021018u)
#define RCU_RSTSCK REG(0x40021024u)
/* ADC0 */
#define ADC0_STAT REG(0x40012400u)
#define ADC0_CTL1 REG(0x40012408u)
#define ADC0_SAMPT0 REG(0x4001240Cu)
#define ADC0_RSQ0 REG(0x4001242Cu)
#define ADC0_RSQ2 REG(0x40012434u)
#define ADC0_RDATA REG(0x4001244Cu)
/* FMC wait states */
#define FMC_WS REG(0x40022000u)
/* AFIO */
#define AFIO_PCF0 REG(0x40010004u)
/* GPIO */
#define GPIOA_CTL0 REG(0x40010800u)
#define GPIOA_CTL1 REG(0x40010804u)
#define GPIOA_ISTAT REG(0x40010808u)
#define GPIOA_OCTL REG(0x4001080Cu)
#define GPIOA_BOP REG(0x40010810u)
#define GPIOA_BC REG(0x40010814u)
#define GPIOB_CTL0 REG(0x40010C00u)
#define GPIOB_CTL1 REG(0x40010C04u)
#define GPIOB_ISTAT REG(0x40010C08u)
#define GPIOB_OCTL REG(0x40010C0Cu)
/* FWDGT */
#define FWDGT_CTL REG(0x40003000u)
#define FWDGT_PSC REG(0x40003004u)
#define FWDGT_RLD REG(0x40003008u)
#define FWDGT_STAT REG(0x4000300Cu)
/* Core */
#define SYST_CSR REG(0xE000E010u)
#define SYST_RVR REG(0xE000E014u)
#define SYST_CVR REG(0xE000E018u)
#define SCB_ICSR REG(0xE000ED04u)
#define SCB_AIRCR REG(0xE000ED0Cu)
#define SCB_SHPR3 REG(0xE000ED20u)
#define DEMCR REG(0xE000EDFCu)
#define DWT_CTRL REG(0xE0001000u)
#define UID_BASE 0x1FFFF7E8u

#define SYSTICK_LOAD (H8I8O_CORE_HZ / 1000u - 1u)

static volatile uint64_t g_ms;
static uint8_t g_outputs;

void SysTick_Handler(void) { g_ms = g_ms + 1u; }

static void clock_init(void)
{
    /* HSI runs from reset. 48 MHz needs one flash wait state (24-48 MHz). */
    FMC_WS = (FMC_WS & ~7u) | 1u;
    /* PLL source HSI/2 (PLLSEL = 0), x12 (PLLMF = 1010b); AHB /1, APB1 /2, APB2 /1. */
    RCU_CFG0 = (RCU_CFG0 & ~((0xFu << 18) | (1u << 27) | (1u << 16) | (0xFu << 4) | (7u << 8) | (7u << 11)))
             | (0xAu << 18) | (4u << 8);
    RCU_CTL |= 1u << 24; /* PLLEN */
    while ((RCU_CTL & (1u << 25)) == 0u) {
    }
    RCU_CFG0 = (RCU_CFG0 & ~3u) | 2u; /* system clock = PLL */
    while (((RCU_CFG0 >> 2) & 3u) != 2u) {
    }
}

static void gpio_init(void)
{
    RCU_APB2EN |= (1u << 0) | (1u << 2) | (1u << 3) | (1u << 4); /* AFIO, PA, PB, PC */

    /* JTAG off, SWD on: SWJ_CFG = 010. Never 100 (that disables SWD too). */
    AFIO_PCF0 = (AFIO_PCF0 & ~(7u << 24)) | (2u << 24);

    /* PA0-PA7 (DO) were set by Reset_Handler. PA8 floating; PA9 USART1 TX
     * (AF push-pull, 50 MHz); PA10 USART1 RX, PA11 button, PA15 DI1: inputs
     * with pull-up; PA12 LED push-pull 2 MHz; PA13/14 stay SWD. */
    GPIOA_OCTL |= (1u << 10) | (1u << 11) | (1u << 15);
    board_set_led(false);
    GPIOA_CTL1 = 0x844288B4u;

    /* PB0 (AUX) floating until characterised; PB1-PB2 floating; PB3-PB9
     * (DI2-DI8) inputs with pull-up. */
    GPIOB_OCTL |= 0x3F8u;
    GPIOB_CTL0 = 0x88888444u;
    GPIOB_CTL1 = (GPIOB_CTL1 & ~0xFFu) | 0x88u;

    /* PB15 is the TP-UART2's SAVE output: input with pull-up. */
    GPIOB_OCTL |= 1u << 15;
    GPIOB_CTL1 = (GPIOB_CTL1 & ~(0xFu << 28)) | (0x8u << 28);
}

void board_init(void)
{
    clock_init();
    gpio_init();

    DEMCR |= 1u << 24;  /* trace on, for the cycle counter */
    DWT_CTRL |= 1u;

    /* SysTick above USART1 (priority 0x00 vs 0x40), so time is current in the
     * receive interrupt. */
    SCB_SHPR3 = (SCB_SHPR3 & 0x00FFFFFFu);
    SYST_RVR = SYSTICK_LOAD;
    SYST_CVR = 0u;
    SYST_CSR = 7u;
}

uint32_t board_millis(void) { return (uint32_t)g_ms; }

uint64_t board_micros(void)
{
    uint64_t ms;
    uint32_t value;
    uint32_t pending;
    do {
        ms = g_ms;
        value = SYST_CVR;
        pending = SCB_ICSR & (1u << 26); /* PENDSTSET: wrapped, handler not yet run */
    } while (ms != g_ms);
    if (pending != 0u && value > SYSTICK_LOAD / 2u) {
        ms += 1u;
    }
    return ms * 1000u + (SYSTICK_LOAD - value) / (H8I8O_CORE_HZ / 1000000u);
}

void board_set_outputs(uint8_t active_mask)
{
    g_outputs = active_mask;
#if H8I8O_DO_ACTIVE_HIGH
    const uint8_t high = active_mask;
#else
    const uint8_t high = (uint8_t)~active_mask;
#endif
    /* One write: set the high pins, reset the others. */
    GPIOA_BOP = (uint32_t)high | ((uint32_t)(uint8_t)~high << 16);
}

uint8_t board_outputs(void) { return g_outputs; }

uint8_t board_read_input_levels(void)
{
    const uint32_t a = GPIOA_ISTAT;
    const uint32_t b = GPIOB_ISTAT;
    /* DI1 = PA15, DI2-DI8 = PB3-PB9 */
    return (uint8_t)(((a >> 15) & 1u) | (((b >> 3) & 0x7Fu) << 1));
}

bool board_bus_power_failing(void)
{
    const bool high = (GPIOB_ISTAT & (1u << 15)) != 0u;
#if H8I8O_SAVE_ACTIVE_LOW
    return !high;
#else
    return high;
#endif
}

uint8_t board_read_inputs(void)
{
#if H8I8O_DI_ACTIVE_LOW
    return (uint8_t)~board_read_input_levels();
#else
    return board_read_input_levels();
#endif
}

bool board_button_pressed(void)
{
    const bool high = (GPIOA_ISTAT & (1u << 11)) != 0u;
#if H8I8O_BUTTON_ACTIVE_LOW
    return !high;
#else
    return high;
#endif
}

void board_set_led(bool on)
{
#if H8I8O_LED_ACTIVE_LOW
    const bool high = !on;
#else
    const bool high = on;
#endif
    GPIOA_BOP = high ? (1u << 12) : (1u << (12 + 16));
}

void board_unique_id(uint8_t out[12])
{
    const volatile uint32_t *uid = (const volatile uint32_t *)UID_BASE;
    for (uint32_t w = 0; w < 3u; ++w) {
        const uint32_t word = uid[w];
        for (uint32_t i = 0; i < 4u; ++i) {
            out[w * 4u + i] = (uint8_t)(word >> (8u * i));
        }
    }
}

void board_adc_noise(uint16_t *samples, uint32_t count)
{
    /* ADC clock: APB2 48 MHz / 4 = 12 MHz (ADCPSC = 01b; at most 14 MHz). */
    RCU_CFG0 = (RCU_CFG0 & ~(3u << 14)) | (1u << 14);
    RCU_APB2EN |= 1u << 9; /* ADC0EN */
    /* ADCON, with the temperature sensor and VREFINT on (TSVREN); the regular
     * group starts on SWRCST (ETSRC = 111b, ETERC). */
    ADC0_CTL1 = (1u << 0) | (1u << 23) | (7u << 17) | (1u << 20);
    const uint32_t t0 = (uint32_t)board_micros();
    while ((uint32_t)board_micros() - t0 < 20u) { /* ADC power-up and sensor start-up */
    }
    ADC0_SAMPT0 &= ~((7u << 18) | (7u << 21)); /* channels 16 and 17: 1.5 cycles */
    ADC0_RSQ0 &= ~(0xFu << 20);                /* one conversion */
    for (uint32_t i = 0; i < count; ++i) {
        ADC0_RSQ2 = (i & 1u) ? 17u : 16u;
        ADC0_STAT = 0u;
        ADC0_CTL1 |= 1u << 22; /* SWRCST */
        while ((ADC0_STAT & (1u << 1)) == 0u) { /* EOC */
        }
        samples[i] = (uint16_t)(ADC0_RDATA & 0xFFFu);
    }
    ADC0_CTL1 = 0u; /* off */
    RCU_APB2EN &= ~(1u << 9);
}

uint32_t board_serial_number(void)
{
    const volatile uint32_t *uid = (const volatile uint32_t *)UID_BASE;
    /* FNV-1a over the 12 octets: spreads the lot and wafer fields. */
    uint32_t hash = 2166136261u;
    for (uint32_t w = 0; w < 3u; ++w) {
        uint32_t word = uid[w];
        for (uint32_t i = 0; i < 4u; ++i) {
            hash ^= word & 0xFFu;
            hash *= 16777619u;
            word >>= 8;
        }
    }
    return hash;
}

uint32_t board_reset_cause(void)
{
    const uint32_t flags = RCU_RSTSCK & 0xFC000000u;
    RCU_RSTSCK |= 1u << 24; /* RSTFC: clear the flags */
    return flags;
}

const char *board_reset_cause_text(uint32_t cause)
{
    if (cause & (1u << 29)) return "watchdog";
    if (cause & (1u << 28)) return "software";
    if (cause & (1u << 30)) return "window watchdog";
    if (cause & (1u << 31)) return "low power";
    if (cause & (1u << 27)) return "power on";
    if (cause & (1u << 26)) return "reset pin";
    return "unknown";
}

void board_watchdog_start(void)
{
    /* Start first: that switches IRC40K on, and the PSC and RLD updates
     * (STAT PUD, RUD) only complete on that clock. Until they do, the reset
     * values run: /4 and 4095, about 0.4 s. Cannot be stopped. */
    FWDGT_CTL = 0xCCCCu;
    FWDGT_CTL = 0x5555u;            /* unlock PSC and RLD */
    FWDGT_PSC = 3u;                 /* IRC40K / 32 = 1250 Hz */
    FWDGT_RLD = 1250u;              /* about 1 s (IRC40K is 30-60 kHz) */
    for (uint32_t spin = 0; (FWDGT_STAT & 3u) != 0u && spin < 1000000u; ++spin) {
    }
    FWDGT_CTL = 0xAAAAu;
}

void board_watchdog_feed(void) { FWDGT_CTL = 0xAAAAu; }

void board_restart(void)
{
    __asm volatile("dsb");
    SCB_AIRCR = 0x05FA0004u; /* SYSRESETREQ */
    __asm volatile("dsb");
    for (;;) {
    }
}
