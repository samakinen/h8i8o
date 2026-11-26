# Firmware internals

How the firmware is built and how it runs. For what it does on the bus, see
[application.md](application.md); for the board, [hardware.md](hardware.md).

## Source layout

| Path | Content |
|---|---|
| `app/product.hpp` | The product: communication objects, parameters, object views, identity. Both the firmware and the ETS product file are built from it |
| `app/logic/` | The channel functions: what an input sends and what an output does. No KNstaX, no hardware; host-tested in `test/` |
| `app/main.cpp` | Start-up, the main loop, the glue between the channel logic, the bus and the pins, persistence of output states and counters, RTT diagnostics |
| `Kconfig`, `variants/` | The application's build options and the build variants ([design](design/channel-functions.md#35-build-variants-kconfig)) |
| `cmake/h8i8o_variant.cmake` | Splits a variant into its KNstaX and H8i8o halves and resolves both |
| `test/` | Host tests of `app/logic` |
| `bsp/board.c` | Clock, GPIO, time base, unique ID, watchdog, reset. Pin polarities are the constants in `board.h` |
| `bsp/usart1.c` | USART1 to the TP-UART2, as KNstaX's `knx_uart_hal_t`, with an optional octet trace |
| `bsp/gd32_fmc.c` | The flash driver for persistence, as KNstaX's `knx_flash_hal_t` |
| `bsp/rtt.c` | A minimal SEGGER RTT implementation: one channel out, one in |
| `startup/` | Vector table, `Reset_Handler` (with the stack paint), newlib system calls, linker script |
| `knx.defconfig` | KNstaX configuration on top of its profile: capacities and APDU length |
| `product.lock` | The application version and content of each variant's ETS product ([ets-product.md](ets-product.md)) |
| `external/KNstaX` | The KNX stack, as a git submodule |

Everything is register-level C and C++23; there is no vendor HAL. KNstaX is built with its
`tp1-small` profile, `knx.defconfig` and the build variant on top: TP1 only, 64 group
addresses, 96 associations, APDU length 55 (extended frames, which KNX Data Secure needs), and,
in the `standard` variant, KNX Data Secure with the software AES and fixed key tables (64 group
keys, 16 senders, 2 point-to-point keys), which brings in the extended memory services and the
group object diagnostics ETS uses with a secure device. The firmware build also builds the
product file, on the host with this configuration, so it declares what the firmware
implements ([ets-product.md](ets-product.md));
[data-secure.md](data-secure.md) describes the Data Secure part.

### Build variants

Not every function fits next to Data Secure in the 120 KiB of flash, so the firmware is
built in variants. A variant is one file, `variants/<name>.defconfig`, with KNstaX options
(`CONFIG_KNX_*`: Data Secure on or off) and the application's (`CONFIG_H8I8O_*`, declared in
`Kconfig`: which functions, the product identity). `cmake/h8i8o_variant.cmake` splits it and
has both halves checked by Kconfig, so a misspelt option fails the configure step.

| Variant | Data Secure | Functions | ETS application |
|---|---|---|---|
| `standard` (default) | yes | see the file and [the design](design/channel-functions.md#9-result) | number 1, "H8i8o 8 inputs 8 outputs" |
| `extended` | no | all | number 2, "H8i8o 8 inputs 8 outputs, extended" |

Choose one with `-DH8I8O_VARIANT=<name>`, or `-DH8I8O_VARIANT=<path>` for a file of your own;
the build writes the product file of that variant, `h8i8o_<name>`. Each variant is its own application program
and catalogue product: moving a device from one to the other means flashing the other image
and replacing the device in the ETS project.

Build options (`cmake -D<option>=...`):

| Option | Default | Effect |
|---|---|---|
| `H8I8O_VARIANT` | `standard` | The build variant (above) |
| `H8I8O_TRACE` | `OFF` | The host link trace (below): every octet to and from the TP-UART2 over RTT. Costs about 3.2 KiB of RAM (the trace ring and a 2 KiB RTT buffer instead of 512 B) |
| `H8I8O_LTO` | `ON` | Link-time optimisation |

The RTT bus test generator (`b`, `x`) is the Kconfig option `CONFIG_H8I8O_BUS_TEST`, off in
both variants: add `CONFIG_H8I8O_BUS_TEST=y` to a variant file of your own for bench work.

### Host tests

The channel logic is tested on the host, without KNstaX or the board:

```
cmake -S test -B build/test -G Ninja && cmake --build build/test && ctest --test-dir build/test
```

With MinGW on Windows, add `-DCMAKE_EXE_LINKER_FLAGS=-static`.

## Execution model

No RTOS: one main loop and three interrupts.

- **USART1 receive.** Feeds every octet from the TP-UART2 to the KNstaX TP-UART backend,
  which assembles frames and answers each group telegram addressed to the device with
  `U_AckInformation` before the chip's deadline.
- **SAVE edge.** The TP-UART2 warns that the bus voltage is failing: every output is
  released at once (see "Bus power failure" below).
- **SysTick, 1 ms.** The time base. Higher priority than USART1, so time stamps in the
  receive path are current.
- **Main loop.** Runs the KNstaX stack, the flash log's background step, the input scan and
  debounce and the channel logic (every millisecond), the programming button, pending status
  telegrams, learned scenes, the RTT command channel and the watchdog, then sleeps in `WFI`
  until the next interrupt.

The channel logic (`app/logic`) gets the debounced input edges, the objects the bus writes
and a 1 ms tick. What an input sends goes through a sink that publishes it; what an output
should be is read back every millisecond and driven to the pin with the operating mode
applied. The object handlers only feed the logic, so they stay short and run from the
stack's `loop()`.

## Start-up

1. `Reset_Handler` drives DO1–DO8 low (inactive) before anything else: before the clock
   set-up, `.data` and C++ constructors. It also keeps the debug clocks on in sleep,
   freezes the watchdog while the core is halted, and paints the stack reserve with a
   pattern, so the deepest the stack has been can be read later (`s`).
2. The clock goes to 48 MHz (internal 8 MHz oscillator, /2 × 12, one flash wait state).
   USART1 runs at 19200 baud from it (divider 2500, exact).
3. The flash log is mounted, and any erase that a reset interrupted is finished before the
   stack's first save.
4. KNstaX starts: the TP-UART2 is reset and given the stored individual address, and the
   stored tables and parameters are loaded.
5. The outputs whose "State after a restart" is "As before" are set to their stored switch
   values, and their status objects are sent once the device sends (below). The counters
   take their stored counts.
6. Every object that can send cyclically gets its transmit policy slot (the slot allocates,
   so this must come before the lock); later parameter changes only change the settings.
7. The heap is locked (no allocation after start), and the watchdog starts with about 1 s
   timeout. It is fed only from the main loop.
8. Once the device is operational and the "delay before sending after a start" is over, it
   starts sending: the inputs set to send after a start do, and "in operation" starts.

The device is on the bus about 15 ms after reset.

## Memory

| Region | Address | Size | Content |
|---|---|---:|---|
| Flash | `0x0800_0000` | 120 KiB | The image |
| Flash | `0x0801_E000` | 8 KiB | Persistence: two banks of four 1 KiB pages |
| RAM | `0x2000_0000` | 20 KiB | 5 KiB stack at the bottom, then `.data` (with the flash erase routine), `.bss` and the heap |

Use in application version 5, built with Arm GNU Toolchain 13.2.Rel1:

| | `standard` | `extended` |
|---|---:|---:|
| Objects | 74 | 90 |
| Image (of 122,880 B) | 121,032 B (1,848 B free) | 101,048 B (21,832 B free) |
| Static RAM besides the 5 KiB stack | 2,096 B | 2,040 B |
| Heap after start | about 11.5 KB, **estimated** | about 10 KB, **estimated** |

Ubuntu's toolchain (the Docker image in `tools/docker`) builds a larger image than the Arm
GNU Toolchain these figures are from: version 4 was 1,244 B larger there. `standard` keeps
about 1.8 KB free for that reason; check the `FLASH` line of a Docker build.

The static RAM grew by about 800 B over version 4, almost all of it the channel logic (eight
inputs and eight outputs). The heap figure is estimated from version 4's measured 9,744 B and
what version 5 adds: 50 more objects (about 14 B each), their bindings (8 B each), the
transmit policy slots of the cyclic objects, and the 332-octet parameter image. That leaves
about 1.7 KB between the heap and the end of RAM in `standard`. Measure it with `s` on the
board before relying on it.

Version 4 (Data Secure, before the channel functions), measured on the board: image
118,396 B, static RAM 1,304 B (4,504 B with `H8I8O_TRACE`), 9,744 B of heap, and a 3,388 B
stack peak after a secure download and secured group traffic (application version 3, without
Data Secure: 7,660 B and 1,760 B). Secured telegrams account for the difference, hence the
5 KiB reserve. With `H8I8O_TRACE` the `standard` variant has about 3 KB less room for the
heap than above, which may not be enough: build the trace into `extended`, or measure.

The stack grows down towards the start of RAM. One that overflows its reserve runs into the
reserved addresses below `0x2000_0000` and faults; the watchdog resets the device, and the
next start logs `reset: watchdog`. Nothing else is overwritten. The linker script stops the
image from growing into the persistence pages, and `_sbrk` grows the heap up to the end of
RAM.

## Persistence

KNstaX's `FlashLog` keeps a log-structured store in the persistence pages: a CRC on every
record, and compaction into the other bank when one fills. It holds the individual address,
the group address and association tables, the parameters (learned scenes included) and the
Data Secure state, and four records of the firmware's own, under KNstaX's product keys, which
the stack never uses and keeps when a new persistence layout discards its own state:

| Key | Record |
|---|---|
| 0x7F00 | The outputs' switch values |
| 0x7F01 | The Factory Tool Key (16 octets), made at the first start (Data Secure variants) |
| 0x7F02 | A start counter (4 octets), part of the random source's seed (Data Secure variants) |
| 0x7F03 | The input counters (8 × 4 octets, in pulses) |

The output record is written when `SAVE` warns of a bus power failure and before a planned
restart (after an ETS download, `R`), and only when it changed. The counter record is written
at the same moments and also every 15 minutes, in each case only when a count changed; never
per pulse. A reset without warning (the watchdog, a fault, a power cut `SAVE` did not report)
finds the last records. A factory reset erases both.

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
| Application | version 5; number 1 (`standard`) or 2 (`extended`), from the variant |
| Hardware type | 1 (`standard`) or 2 (`extended`), from the variant |

## Diagnostics over RTT

The firmware logs over SEGGER RTT through the debug probe, and takes short commands. `tools/scripts/rtt_console.sh` records the log and sends commands; any RTT client
on OpenOCD's RTT server works as well.

```
tools/scripts/rtt_console.sh build/logs/session.log 30 "1:a 2:s"
```

| Command | Action |
|---|---|
| `p` | Toggle programming mode, as the button does |
| `s` | Statistics: TP-UART frames, acknowledges, confirmations, errors, erases in busy mode, USART counters, heap high-water mark, stack peak and reserve |
| `i` | Raw input pin levels (DI1 first) and the button |
| `1`–`8` | Toggle that output's switch value locally, as a switch telegram would (delays, staircase and so on apply), and send its status |
| `a` | Individual address, KNX serial number, lifecycle state (and trace drops with `H8I8O_TRACE`) |
| `k` | Data Secure variants: the device certificate for ETS (serial number and Factory Tool Key), whether Data Secure is on, and whether the tool key in force is the FDSK or one from ETS |
| `R` | Restart, storing the output states and counters first |
| `b<area>.<line>.<device>,<count>,<gap ms>;` | With `CONFIG_H8I8O_BUS_TEST`: send `count` A_DeviceDescriptor_Read to that device, `gap` ms apart, each after the TP-UART2 has confirmed the last; then print how many were confirmed, refused busy, failed or timed out |
| `x` | Stop a running bus test |

**Bus test** (`CONFIG_H8I8O_BUS_TEST`). `b1.1.3,200,100;` checks another device's receiver and transmitter: it has
to acknowledge 200 requests and answer each. With a gap of 0 the next request is fed while
the device is still answering, so the two compete for the bus. The answers are counted by
the statistics (`tpuart rx`); a repeated answer or a damaged one shows in the trace and in
the octet and checksum error counters. A long run at gap 0 produces more trace than RTT
carries, so use the counters there. The requests come from the device's own address and
are not part of its application.

Log lines start with the time since boot in seconds. Besides the commands' output the log
shows the reset cause, the stored address and the device certificate at start, lifecycle
changes, parameter downloads, input and output changes, and raw input level changes.

**Host link trace** (build option `H8I8O_TRACE`). Every octet between the MCU and the TP-UART2 is logged, one line per
burst: `rx <ms>: …` from the chip, `tx <ms>: …` to it, where `<ms>` is the burst's start
time modulo 16,384. The once-a-second `U_State` poll and its answer are left out. A burst ends after 2 ms
without an octet, so a frame can be split over two lines.

Expected at every start: one unexpected octet and one USART receive error, from the
framing error the TP-UART2 produces while it resets.

**Bus power.** The TP-UART2's `SAVE` output (PB15) goes low when the bus voltage fails. The
stack debounces it as it debounces the bus-health output of other transceivers: the link is
reported down once `SAVE` has stayed low for 20 ms, and up again after 200 ms high. The log
shows:

| Line | Meaning |
|---|---|
| `link Up`, `link Down`, `link Degraded` | The stack's link state: `SAVE` debounced, the chip's answers to `U_State`, its temperature warning |
| `power fail warning (SAVE edge), outputs released` | `SAVE` fell; from the edge interrupt, ahead of the debounce |
| `output states stored (power fail) <bits>` | The switch values went to flash (only when they changed since the last record) |
| `counters stored (power fail)` | The counts went to flash (only when one changed) |
| `pf <ms>` | Every millisecond while `SAVE` stays low: time since it fell |
| `SAVE released` | `SAVE` is high again: the bus came back before the supply gave out (the outputs follow the channel logic again), or `SAVE` was low at start without an edge |
| `DO <bits>` | The output pins changed (DO1 first, 1 = energised) |

The last `pf` line the host receives after the bus is disconnected is how long the device
runs on its capacitors, to within the host's RTT polling interval. The statistics (`s`)
count the warnings (`pfw`).

## Bus power failure

The board runs from the bus, so the relays drop anyway once the capacitors are empty. The
firmware gets ahead of that:

1. The `SAVE` edge interrupt releases every output. The loads stop drawing on the capacitors,
   which leaves more time for step 2 (122 ms was measured with every output off; see
   [hardware.md](hardware.md#observed-behaviour-of-the-parts)).
2. The main loop stores the outputs' switch values and the counters (Persistence, above) and
   asks KNstaX to save its own power-fail state (`requestPowerFailSave()`; with Data Secure,
   the sequence numbers). The pins stay released while `SAVE` is low; the channel logic keeps
   running, so a command that arrives meanwhile still counts.
3. If `SAVE` goes high again before the supply gives out, the pins follow the channel logic
   again.

## Programming button

| Press | Effect |
|---|---|
| Short | Toggles programming mode (on the press). The LED shows it |
| Held 10 s | The LED flashes fast: released now, the device does a factory reset |
| Held 15 s | Nothing: the factory reset is not armed any more, and the LED shows programming mode again |

The factory reset is KNstaX's `factoryReset()`: what ETS's Master Reset with erase code 02h
does (individual address 15.15.255, empty tables, default parameters, the authorisation
and Data Secure keys back to ex-factory), then a restart. It is not a flash erase: a Master
Reset keeps the Data Secure sending sequence number, which a secured device must never reuse.
The firmware also erases its output and counter records. The first press of the hold toggles programming
mode, as any press does; the restart clears it.

Programming mode ends by itself after 15 minutes, however it was turned on.

## Changing the firmware

- **Pin polarities** are in `bsp/board.h`. Nothing else in the firmware depends on them.
- **Objects and parameters** are declared in `app/product.hpp`, and when ETS shows an
  object, and as what, in its `kObjectViews`. When they change:
  - raise `applicationVersion`, so that ETS sees a new application program;
  - raise `PersistencePolicy::schemaVersion` when ports are added, removed or reordered,
    because the stored object layout is positional;
  - build every variant and commit `product.lock`; a build whose product changed without a
    new version fails ([ets-product.md](ets-product.md#when-the-product-changes)).
- **A function** lives in `app/logic` with its host tests in `test/`; `app/main.cpp` only
  decodes its parameters and binds its objects. A function that not every variant can carry
  gets a `CONFIG_H8I8O_*` option in `Kconfig`.
- **Another KNstaX checkout**: configure with `-DKNSTAX_DIR=<path>`. The default is the
  submodule.
