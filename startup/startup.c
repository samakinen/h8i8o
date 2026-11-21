/* SPDX-License-Identifier: GPL-3.0-or-later */
/* H8i8o startup: vector table, safe output levels, .data/.bss, constructors, main.
 *
 * Reset_Handler drives DO1-DO8 (PA0-PA7) to their inactive level before
 * anything else runs: before clocks, .data or C++ constructors. The inactive
 * level comes from board.h (H8I8O_DO_ACTIVE_HIGH).
 *
 * The debug clocks are kept on in sleep and the watchdog is frozen while the
 * core is halted, so the probe can attach at any time (without DBG_SLEEP the
 * core in WFI stops the clock the debug port needs).
 */
#include <stdint.h>

#include "board.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern void (*__preinit_array_start[])(void);
extern void (*__preinit_array_end[])(void);
extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);
int main(void);

#define REG(addr) (*(volatile uint32_t *)(addr))

void Reset_Handler(void)
{
    /* 1. Outputs inactive. The port clock must run for the writes to land. */
    REG(0x40021018u) |= (1u << 2);        /* RCU_APB2EN: PAEN */
#if H8I8O_DO_ACTIVE_HIGH
    REG(0x40010814u) = 0x00FFu;           /* GPIOA_BC: PA0-PA7 low */
#else
    REG(0x40010810u) = 0x00FFu;           /* GPIOA_BOP: PA0-PA7 high */
#endif
    REG(0x40010800u) = 0x22222222u;       /* GPIOA_CTL0: PA0-PA7 push-pull outputs, 2 MHz */

    /* 2. Debug: clocks on in sleep/stop/standby, watchdog frozen while halted. */
    REG(0xE0042004u) |= (1u << 0) | (1u << 1) | (1u << 2) | (1u << 8);

    /* 3. C runtime. .data holds .ramfunc (the flash erase wait). */
    for (uint32_t *s = &_sidata, *d = &_sdata; d < &_edata;) {
        *d++ = *s++;
    }
    for (uint32_t *d = &_sbss; d < &_ebss;) {
        *d++ = 0;
    }
    for (void (**f)(void) = __preinit_array_start; f < __preinit_array_end; ++f) {
        (*f)();
    }
    for (void (**f)(void) = __init_array_start; f < __init_array_end; ++f) {
        (*f)();
    }
    main();
    for (;;) {
    }
}

/* A fault is logged by the next boot (board_reset_cause()); the watchdog or
 * the debugger takes it from here. */
static void Default_Handler(void)
{
    for (;;) {
    }
}

#define W __attribute__((weak, alias("Default_Handler")))
void NMI_Handler(void) W;
void HardFault_Handler(void) W;
void MemManage_Handler(void) W;
void BusFault_Handler(void) W;
void UsageFault_Handler(void) W;
void SVC_Handler(void) W;
void DebugMon_Handler(void) W;
void PendSV_Handler(void) W;
void SysTick_Handler(void) W;
void USART1_IRQHandler(void) W;
void EXTI15_10_IRQHandler(void) W;

/* Every interrupt defaults; the used ones are named after the range on purpose. */
#pragma GCC diagnostic ignored "-Woverride-init"

#define USART1_IRQN 37u
#define EXTI15_10_IRQN 40u /* EXTI lines 10-15; PB15 is the TP-UART2's SAVE */
#define IRQ_COUNT 43u /* GD32F103 medium density */

__attribute__((section(".isr_vector"), used))
void (*const vector_table[16u + IRQ_COUNT])(void) = {
    (void (*)(void))&_estack, Reset_Handler,
    NMI_Handler, HardFault_Handler, MemManage_Handler, BusFault_Handler, UsageFault_Handler,
    0, 0, 0, 0,
    SVC_Handler, DebugMon_Handler, 0, PendSV_Handler, SysTick_Handler,
    [16u ... 16u + IRQ_COUNT - 1u] = Default_Handler,
    [16u + USART1_IRQN] = USART1_IRQHandler,
    [16u + EXTI15_10_IRQN] = EXTI15_10_IRQHandler,
};
