/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Minimal SEGGER RTT: one up-buffer (log) and one down-buffer (commands),
 * served by OpenOCD's `rtt setup / rtt start / rtt server start`.
 * Non-blocking: what does not fit the up-buffer is dropped and counted. */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void rtt_init(void);
void rtt_write(const char *data, uint32_t length);
/* One octet from the host, or -1 when there is none. */
int rtt_read_char(void);
uint32_t rtt_dropped(void);

#ifdef __cplusplus
}
#endif
