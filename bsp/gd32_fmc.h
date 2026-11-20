/* SPDX-License-Identifier: GPL-3.0-or-later */
/* H8i8o persistence flash: the GD32F103 FMC as KNstaX's knx_flash_hal_t.
 *
 * The region is the last 8 KiB of the 128 KiB part: eight 1 KiB pages, two
 * banks of four, programmed in half-words. FlashLog owns the format; this
 * driver only erases, programs and reads.
 *
 * Erase stalls every flash read for the whole erase, 37-39 ms (measured),
 * and an interrupt taken meanwhile from flash can lock the chip. The driver
 * starts the erase and waits for it from RAM (.ramfunc, about 50 bytes) with
 * interrupts masked, so vectors and handlers can stay in flash
 * (docs/firmware.md). Octets the UART receives meanwhile are lost; the
 * product passes the TP-UART backend to FlashLog::setEraseGuard() so the chip
 * answers BUSY meanwhile. Programming a half-word stalls flash for about
 * 100 us; that loop stays in flash with interrupts enabled (measured safe).
 *
 * Requirement on the image: the linker script places .ramfunc in the
 * initialised data region.
 *
 * Main context only (KNstaX calls the HAL from its loop); not reentrant.
 */
#pragma once

#include "knx/objects/flash_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

#define H8I8O_PERSIST_BASE 0x0801E000u
#define H8I8O_PERSIST_PAGE_SIZE 1024u
#define H8I8O_PERSIST_PAGE_COUNT 8u
#define H8I8O_PERSIST_WRITE_UNIT 2u

/* The HAL for the persistence region. */
knx_flash_hal_t h8i8o_fmc_hal(void);

#ifdef __cplusplus
}
#endif
