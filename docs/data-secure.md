# KNX Data Secure on the H8i8o: analysis

Status: analysis, 21 November 2026. Nothing here is built. Figures marked "est." are estimates,
not measurements.

KNstaX already implements Data Secure and has run it against ETS6 on ESP32. What it needs to run
on this board, on the stack's side, is in KNstaX's
[docs/design/data-secure-small-target.md](../external/KNstaX/docs/design/data-secure-small-target.md).
This document covers the board: whether it fits, what the firmware here must give up or add, and
what only the board can provide.

## Verdict

**It fits, but only after work on both sides.** Flash is not the problem. RAM is, and so is the way
the stack's Security Interface Object allocates memory after start-up, which this firmware forbids
by locking the heap.

| | Today | Data Secure needs (est.) |
|---|---:|---:|
| Flash, image region (120 KiB) | 96,512 B used, **26,368 B free** | 13–19 KiB |
| RAM (20 KiB) | static 5,584 B + heap about 10.5 KiB + stack 2,560 B: **about 1.5 KiB free** | as KNstaX is today: about 4.8 KiB; after the KNstaX work items: about 2 KiB |

The GD32F103CB has no AES peripheral, so the crypto is software: AES-128, encrypt-only (CCM needs no
decryption), about 1–1.5 KiB of flash. A secured telegram costs a few milliseconds at 48 MHz
(est.), outside the TP-UART2's acknowledge deadline.

## What the board can save

| Saving | RAM | Notes |
|---|---:|---|
| Build without the host link trace (`g_trace`, 768 × 2 B, `bsp/usart1.c`) | 1,536 B | Already listed under "Possible future improvements". A build option, so a debug build keeps it |
| RTT up buffer 2,048 → 512 B (`bsp/rtt.c`) | 1,536 B | It is 2 KiB only to hold the trace during an ETS download. Without the trace, 512 B carries the log |
| Smaller capacities through a `knx.defconfig` override: 64 group addresses (128 today), 96 associations (160 today) | about 1,200 B (est.) | The address and association tables, their load segments and the persistence scratch all scale with them. 24 objects need far fewer than the `tp1-small` defaults. Changes the memory layout in the product file: rebuild it and raise `applicationVersion` |
| **Total** | **about 4.2 KiB** | |

With these, the board has about 5.7 KiB of RAM for Data Secure's roughly 2 KiB (after the KNstaX
work items) and for the stack the secure path needs (est. 450 B at peak). As KNstaX is today
(about 4.8 KiB) it would also fit in RAM, but it would still abort at the first secure download
(next section). The KNstaX work is needed either way.

Flash, if more is needed: the trace build option also removes its code and format strings.

## What blocks it in KNstaX

In short (details in the KNstaX design):

1. No AES backend for bare metal; the standalone build demands mbedTLS.
2. The Security Interface Object stores its tables in `std::vector`s and `std::map`s that grow
   during the ETS download, and the per-sender sequence map grows on the first telegram from a new
   sender. This firmware locks the heap after start-up (`knx::heap::lock()` in `app/main.cpp`),
   so the first secure download would abort the device. Fixed-capacity tables are the fix.
3. With a 15-octet APDU, a secured write of one key-table entry does not fit in a frame. The
   firmware must move to extended frames: `CONFIG_KNX_MAX_APDU_LENGTH=55` and
   `maxApduLength = 55` in `app/product.hpp`, 55 being the most a 64-octet TP-UART2 frame carries.
   About +1 KiB of RAM, already counted in the 2 KiB above.

## What only the board can provide

- **Factory Tool Key (FDSK).** ETS needs the device's FDSK, entered as its device certificate. It
  must be random per device, not derived from the MCU's unique ID, which anyone can read. Proposal:
  generate it at the first start from the noise source below, store it in the persistence pages,
  and print the device certificate with the RTT `a` command. A mass erase then gives the device a
  new FDSK, as the factory reset it is.
- **Randomness.** No TRNG. Seed a generator at start-up from the LSB noise of repeated ADC
  conversions (internal temperature sensor and V<sub>REFINT</sub>) and a counter kept in
  persistence. The S-A_Sync random must never repeat for a key; the stack's fallback (time since
  boot, serial number, call counter) repeats after a reset.
- **Sequence numbers at power loss.** The TP-UART2's `SAVE` warning gives about 122 ms
  (hardware.md). That is enough to write the sequence state into pre-erased flash, which removes
  most of the periodic writes and the flash wear they cause. A periodic checkpoint every minute of
  secured traffic would compact the 8 KiB log every hour or two of continuous traffic (est.).
- **Read protection.** The keys are stored in flash in plain form. Anyone who can attach a probe
  to the `D C G V` pads can read them, as the README procedure does today. A secured installation
  needs read protection set after programming, which ends reading the flash (and with it the
  FDSK) over SWD. Whether RTT diagnostics still work with protection set has to be checked on this
  part.
- **Product file.** The product declares itself Data Secure capable, with key table capacities
  that match the firmware's. Each object gets a security requirement. Signing with the example
  manufacturer ID is the same as today.

## Risks and open points

- **Extended frames on this board are untested.** The KNstaX TP-UART backend handles frames up to
  64 octets, but has only carried standard frames on the H8i8o.
- **ETS behaviour with a 55-octet APDU** and a secure download on this device: only an ETS6 run on
  the board shows it.
- **Stack reserve.** 2.5 KiB, with no measured peak. Measure the peak by painting the stack before
  enabling Secure, and again with it.
- **All figures are estimates** until KNstaX's size gate has a Data Secure reference device and this
  firmware is built with it.

## Order of work

1. Here: the trace build option, the smaller RTT buffer, the capacity override. These are useful
   without Data Secure too.
2. In KNstaX: the portable AES and the build split, then the fixed-capacity Security Interface
   Object (its design document, §8).
3. Here: APDU 55 on standard traffic first (extended frames, ETS download), then Data Secure on: the
   FDSK, the random source, the `SAVE` save path, the product file.
4. An ETS6 secure commissioning run on the board. Then read protection and its effect on
   diagnostics.
