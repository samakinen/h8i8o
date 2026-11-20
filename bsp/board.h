/* SPDX-License-Identifier: GPL-3.0-or-later */
/* H8i8o board support: clock, GPIO, time, identity, watchdog, reset.
 *
 * Register level, no vendor HAL. Pin map: docs/hardware.md.
 *
 * Pin polarities are the constants below; nothing else in the firmware knows
 * a pin's polarity. They match the H8i8o V0.8 (docs/hardware.md says which
 * channels were measured).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* DO1-DO8 (PA0-PA7). Active high (inactive = low). */
#ifndef H8I8O_DO_ACTIVE_HIGH
#define H8I8O_DO_ACTIVE_HIGH 1
#endif
/* DI1-DI8. Read with the internal pull-up; active low (a contact pulls the
 * pin to ground). */
#ifndef H8I8O_DI_ACTIVE_LOW
#define H8I8O_DI_ACTIVE_LOW 1
#endif
/* Programming button PA11, internal pull-up, active low. */
#ifndef H8I8O_BUTTON_ACTIVE_LOW
#define H8I8O_BUTTON_ACTIVE_LOW 1
#endif
/* KNX LED PA12, active high. */
#ifndef H8I8O_LED_ACTIVE_HIGH
#define H8I8O_LED_ACTIVE_HIGH 1
#endif

#define H8I8O_CORE_HZ 48000000u
#define H8I8O_CHANNELS 8u

/* Clock to 48 MHz (HSI/2 x 12), GPIO modes, JTAG released with SWD kept
 * (SWJ_CFG = 010), SysTick at 1 ms, DWT cycle counter on. */
void board_init(void);

/* Time since boot. Safe from any context. */
uint32_t board_millis(void);
uint64_t board_micros(void);

/* Outputs: bit n is DOn+1, 1 = active. */
void board_set_outputs(uint8_t active_mask);
uint8_t board_outputs(void);

/* Inputs: bit n is DIn+1, 1 = active, raw (not debounced). */
uint8_t board_read_inputs(void);
/* Raw pin levels, bit n is DIn+1, 1 = high. For diagnostics. */
uint8_t board_read_input_levels(void);

bool board_button_pressed(void);
void board_set_led(bool on);

/* 32 bits derived from the 96-bit unique ID. */
uint32_t board_serial_number(void);

/* What caused the last reset (RCU_RSTSCK flags, then cleared). */
uint32_t board_reset_cause(void);
const char *board_reset_cause_text(uint32_t cause);

/* Free watchdog: start with about 1 s, feed from the main loop only. */
void board_watchdog_start(void);
void board_watchdog_feed(void);

void board_restart(void) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif
