/* SPDX-License-Identifier: GPL-3.0-or-later */
/* USART1 (PA9 TX, PA10 RX) to the TP-UART2, as KNstaX's knx_uart_hal_t.
 *
 * Receive: one interrupt per octet, which calls the stack's callback with the
 * octet, its error flags and a microsecond time stamp. Transmit: a small ring
 * emptied by the TXE interrupt. write() masks interrupts while it copies, so
 * a write from the main context and one from the receive callback never
 * interleave (uart_hal.h, concurrency model).
 *
 * No MCU pin is known to reach the TP-UART2's RESET or SAVE, so set_reset
 * and read_save are NULL.
 */
#pragma once

#include "knx/physical/uart_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

#define H8I8O_USART1_TX_RING 32u

knx_uart_hal_t h8i8o_usart1_hal(void);

/* Diagnostics: octets received (and how many had an error flag), octets
 * sent, and writes refused because the ring was full or the port closed. */
typedef struct {
    uint32_t rx_octets;
    uint32_t rx_errors;
    uint32_t tx_octets;
    uint32_t tx_refused;
} h8i8o_usart1_stats_t;

h8i8o_usart1_stats_t h8i8o_usart1_stats(void);

/* Octet trace (diagnostics over RTT): every octet on the host link, in order.
 * Entries are 16 bits:
 *   0x8000 | dir << 14 | ms & 0x3FFF   start of a burst (dir 1 = to the chip);
 *                                      a burst ends when the direction changes
 *                                      or after 2 ms without an octet
 *   0x00nn                             one octet of the burst
 * Read from the main loop; the interrupt writes. When the ring is full, new
 * entries are dropped and counted. */
#define H8I8O_USART1_TRACE_ENTRIES 768u
#define H8I8O_USART1_TRACE_MARK 0x8000u
#define H8I8O_USART1_TRACE_TX 0x4000u

size_t h8i8o_usart1_trace_read(uint16_t *out, size_t max);
uint32_t h8i8o_usart1_trace_dropped(void);

#ifdef __cplusplus
}
#endif
