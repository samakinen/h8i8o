# KNX Data Secure on the H8i8o

Status, 22 November 2026: the groundwork is done on both sides; Data Secure itself is not on in
this firmware yet. Figures marked "est." are estimates, the rest are measured.

KNstaX implements Data Secure and has run it against ETS6 on ESP32. What it needs to run on a
small part, on the stack's side, is in KNstaX's
[docs/design/data-secure-small-target.md](../external/KNstaX/docs/design/data-secure-small-target.md).
This document covers the board: whether it fits, what is done, and what only the board can
provide.

## Verdict

**It fits.** Flash was never the problem and RAM no longer is. What blocks it now is one piece of
KNstaX work (fixed-capacity Security Interface Object) and the board-only items below.

| | This firmware (application version 3) | With Data Secure (est.) |
|---|---:|---:|
| Flash, image region (120 KiB = 122,880 B) | 95,756 B | about 114.9 KB: +19.1 KB, as KNstaX's size gate measures it |
| Static RAM, besides the 3 KiB stack | 1,208 B (4,408 B with `H8I8O_TRACE`) | unchanged |
| Heap after start | 7,660 B | about +0.7 KiB |

The size gate's Data Secure device (tp1-small, software AES, APDU 55, Cortex-M3) measures 105,814 B
of KNstaX flash and 9,374 B of KNstaX RAM, against 86,651 B and 7,774 B without it. The gate
reports that device 2.4 KB over the flash it reserves for the H8i8o reference design; that
reservation is a model of a whole product, and this firmware's own image is what counts here:
95,756 B now, about 114.9 KB with Data Secure, inside the 120 KiB region with about 8 KB to spare
(est.).

The GD32F103CB has no AES peripheral. KNstaX now has a portable AES-128, encrypt-only: 780 B of
flash in the gate image, 64 B of stack, no RAM. A secured telegram costs a few milliseconds at
48 MHz (est.), outside the TP-UART2's acknowledge deadline.

## Done

On this board (application version 3):

- The host link trace is a build option (`H8I8O_TRACE`, off), and the RTT up buffer is 512 B
  without it: static RAM 4,392 → 1,208 B.
- `knx.defconfig`: 64 group addresses and 96 associations (from 128 and 160), and APDU length 55,
  the most a 64-octet TP-UART2 frame carries. A secured write of one key table entry needs it.
  The product file declares the same.
- Bus power failure: the outputs are released at the `SAVE` edge and the firmware calls
  `requestPowerFailSave()`, which saves the Data Secure replay state when Secure is on.
- Factory reset by button through KNstaX's `factoryReset()` (Master Reset 02h), which keeps the
  Data Secure sending sequence number. A flash erase does not.
- The stack peak is measured (`s`): 1,760 B of the 3 KiB reserve, which sits at the bottom of
  RAM, so an overflow faults and the watchdog resets the device.
- Tested on the board with ETS6: downloads with 55-octet extended frames, a bus power cut with
  an output set to restore, the factory reset and commissioning again afterwards.

In KNstaX (branch `memory-reductions`):

- The memory reductions: KNstaX RAM in the tp1-small reference device 13.3 → 7.8 KiB.
- The portable AES backend, chosen in Kconfig (`KNX_AES_BACKEND`: software, mbedTLS or the ESP32
  block). Data Secure builds without mbedTLS.
- A Data Secure configuration of the size gate, so its flash and RAM are budgeted in CI.
- `factoryReset()` and a persistence key range for product records.

## What still blocks it in KNstaX

The Security Interface Object stores its tables in `std::vector`s and `std::map`s that grow
during the ETS secure download, and the per-sender sequence map grows on the first telegram from a
new sender. This firmware locks the heap after start-up, so the first secure download would abort
the device. The size gate's Data Secure device passes because a plain download never reaches those
paths. Fixed-capacity tables are the fix (design D3–D5).

## What only the board can provide

- **Factory Tool Key (FDSK).** ETS needs the device's FDSK, entered as its device certificate. It
  must be random per device, not derived from the MCU's unique ID, which anyone can read. Proposal:
  generate it at the first start from the noise source below, store it in the persistence pages
  (a product key, outside what a Master Reset touches), and print the device certificate with the
  RTT `a` command. A mass erase then gives the device a new FDSK; the button's factory reset keeps
  it, as it should. The generation and storage depend on the MCU, not the board: they belong in
  KNstaX's GD32F1 port (`ports/gd32f1`), which this firmware would then use.
- **Randomness.** No TRNG. Seed a generator at start-up from the LSB noise of repeated ADC
  conversions (internal temperature sensor and V<sub>REFINT</sub>) and a counter kept in
  persistence. The S-A_Sync random must never repeat for a key; the stack's fallback (time since
  boot, serial number, call counter) repeats after a reset. Also an MCU matter, for the port.
- **Read protection.** The keys are stored in flash in plain form. Anyone who can attach a probe
  to the `D C G V` pads can read them. A secured installation needs read protection set after
  programming, which ends reading the flash (and with it the FDSK) over SWD. Whether RTT
  diagnostics still work with protection set has to be checked on this part.
- **Product file.** The product declares itself Data Secure capable, with key table capacities
  that match the firmware's, and each object gets a security requirement. That is application
  version 4: declaring it before the firmware runs Data Secure would make ETS try a secure
  commissioning the device cannot do.

## Risks and open points

- **ETS behaviour with a secure download on this device**: only an ETS6 run on the board shows it.
- **Stack reserve**: 3 KiB, 1,760 B used. Read the peak again with Data Secure: a secure
  download decrypts on the receive path.

## Order of work

1. Here, done and tested on the board: application version 3.
2. In KNstaX: the fixed-capacity Security Interface Object (D3–D5); the FDSK store and the random
   source in `ports/gd32f1`.
3. Here: move onto the GD32F1 port, then Data Secure on: the FDSK, the product file (version 4).
4. An ETS6 secure commissioning run on the board. Then read protection and its effect on
   diagnostics.
