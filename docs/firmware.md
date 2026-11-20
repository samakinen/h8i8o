# Firmware internals

How the firmware is built and how it runs. For what it does on the bus, see
[application.md](application.md); for the board, [hardware.md](hardware.md).

## Source layout

| Path | Content |
|---|---|
| `app/product.hpp` | The product: communication objects, parameters, identity. Both the firmware and the ETS product file are built from it |
| `app/main.cpp` | Start-up, the main loop, the input and output logic, RTT diagnostics |
| `bsp/board.c` | Clock, GPIO, time base, unique ID, watchdog, reset. Pin polarities are the constants in `board.h` |
| `bsp/usart1.c` | USART1 to the TP-UART2, as KNstaX's `knx_uart_hal_t`, with an octet trace |
| `bsp/gd32_fmc.c` | The flash driver for persistence, as KNstaX's `knx_flash_hal_t` |
| `bsp/rtt.c` | A minimal SEGGER RTT implementation: one channel out, one in |
| `startup/` | Vector table, `Reset_Handler`, newlib system calls, linker script |
| `external/KNstaX` | The KNX stack, as a git submodule |
| `tools/knxprod` | Host build of the ETS product file from `app/product.hpp` |

Everything is register-level C and C++23; there is no vendor HAL. KNstaX is built with its
`tp1-small` profile: TP1 only, 128 group addresses, 160 associations, APDU length 15.

## Execution model

No RTOS: one main loop and two interrupts.

- **USART1 receive.** Feeds every octet from the TP-UART2 to the KNstaX TP-UART backend,
  which assembles frames and answers each group telegram addressed to the device with
  `U_AckInformation` before the chip's deadline.
- **SysTick, 1 ms.** The time base. Higher priority than USART1, so time stamps in the
  receive path are current.
- **Main loop.** Runs the KNstaX stack, the flash log's background step, the input scan and
  debounce (every millisecond), the programming button, pending status telegrams, the RTT
  command channel and the watchdog, then sleeps in `WFI` until the next interrupt.

## Start-up

1. `Reset_Handler` drives DO1–DO8 low (inactive) before anything else: before the clock
   set-up, `.data` and C++ constructors. It also keeps the debug clocks on in sleep and
   freezes the watchdog while the core is halted.
2. The clock goes to 48 MHz (internal 8 MHz oscillator, /2 × 12, one flash wait state).
   USART1 runs at 19200 baud from it (divider 2500, exact).
3. The flash log is mounted, and any erase that a reset interrupted is finished before the
   stack's first save.
4. KNstaX starts: the TP-UART2 is reset and given the stored individual address, and the
   stored tables and parameters are loaded.
5. The heap is locked (no allocation after start), and the watchdog starts with about 1 s
   timeout. It is fed only from the main loop.

The device is on the bus about 15 ms after reset.

## Memory

| Region | Address | Size | Content |
|---|---|---:|---|
| Flash | `0x0800_0000` | 120 KiB | The image |
| Flash | `0x0801_E000` | 8 KiB | Persistence: two banks of four 1 KiB pages |
| RAM | `0x2000_0000` | 20 KiB | `.data` (with the flash erase routine), `.bss`, heap, 2.5 KiB stack at the top |

Use in this release: image 96,512 B (79 % of the 120 KiB region), static RAM 5,584 B,
heap after start about 10.5 KiB. The linker script stops the image from growing into the
persistence pages, and `_sbrk` refuses to grow the heap into the stack reserve.

## Persistence

KNstaX's `FlashLog` keeps a log-structured store in the persistence pages: a CRC on every
record, and compaction into the other bank when one fills. It holds the individual address,
the group address and association tables and the parameters. Output states are not stored.

A record that a reset or power cut interrupts is detected by its CRC and ignored; the
previous value stays valid. Saves take up to about 13 ms. A compaction copies every live
record and can stall the main loop for up to about 140 ms; interrupts keep running. Writes
happen mostly during an ETS download.

## Flash erases and the bus

On this MCU the CPU cannot fetch from flash while a page erases (37–39 ms), and an interrupt
taken from flash during an erase locks the chip. The firmware therefore:

- starts the erase and waits for it from a small routine in RAM, with interrupts masked, so
  no vector or handler is fetched from flash meanwhile;
- puts the TP-UART2 into busy mode for the erase (KNstaX's `FlashEraseGuard`), so the chip
  answers the telegrams it would acknowledge with BUSY, and the senders repeat them.

Octets that arrive during the erase are lost, but the repetition delivers the telegram. For
those 40 ms every receiver on the line sees the telegram twice, and it arrives a little
late. Erases follow compactions, so they happen mostly during an ETS download.
Programming (writing half-words) is short and runs from flash with interrupts enabled.

## Identity

| Item | Value |
|---|---|
| Manufacturer ID | 0x00FA (KNstaX's example ID; see the README) |
| Serial number | 0x00FA followed by 32 bits hashed (FNV-1a) from the MCU's 96-bit unique ID |
| Mask version | 0x07B0 (System B) |
| Application | number 1, version 2 |

## Diagnostics over RTT

The firmware logs over SEGGER RTT through the debug probe, and takes one-character
commands. `tools/scripts/rtt_console.sh` records the log and sends commands; any RTT client
on OpenOCD's RTT server works as well.

```
tools/scripts/rtt_console.sh build/logs/session.log 30 "1:a 2:s"
```

| Command | Action |
|---|---|
| `p` | Toggle programming mode, as the button does |
| `s` | Statistics: TP-UART frames, acknowledges, confirmations, errors, erases in busy mode, USART counters, heap high-water mark |
| `i` | Raw input pin levels (DI1 first) and the button |
| `1`–`8` | Toggle that output locally and send its status |
| `a` | Individual address, KNX serial number, lifecycle state |
| `R` | Restart |

Log lines start with the time since boot in seconds. Besides the commands' output the log
shows the reset cause and the stored address at start, lifecycle changes, parameter
downloads, input and output changes, and raw input level changes.

**Host link trace.** Every octet between the MCU and the TP-UART2 is logged, one line per
burst: `rx <ms>: …` from the chip, `tx <ms>: …` to it, where `<ms>` is the burst's start
time modulo 16,384. The once-a-second `U_State` poll is left out. A burst ends after 2 ms
without an octet, so a frame can be split over two lines.

Expected at every start: one unexpected octet and one USART receive error, from the
framing error the TP-UART2 produces while it resets.

## Changing the firmware

- **Pin polarities** are in `bsp/board.h`. Nothing else in the firmware depends on them.
- **Objects and parameters** are declared in `app/product.hpp`. When they change:
  - raise `applicationVersion`, so that ETS sees a new application program;
  - raise `PersistencePolicy::schemaVersion` when ports are added, removed or reordered,
    because the stored object layout is positional;
  - rebuild the product file ([tools/knxprod/README.md](../tools/knxprod/README.md)).
- **Another KNstaX checkout**: configure with `-DKNSTAX_DIR=<path>`. The default is the
  submodule.
