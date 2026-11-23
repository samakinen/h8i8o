# KNX Data Secure on the H8i8o

Status, 23 November 2026: **on in application version 4** and run on the board with ETS6:
secure commissioning with the device certificate, a full secured download, and secured group
communication in both directions. The figures below are measured.

KNstaX implements Data Secure and has run it against ETS6 on ESP32. What it took to run it on a
small part is in KNstaX's
[docs/design/data-secure-small-target.md](../external/KNstaX/docs/design/data-secure-small-target.md).
This document covers the board: what is in the firmware, how to commission it, and what is
still open.

## What the firmware does

- **Security Interface Object** with fixed tables (`knx.defconfig`): 64 group keys (one per
  group address the address table can hold), 16 senders in the Security Individual Address
  Table and 16 more slots for senders' sequence numbers, 2 point-to-point keys. The product
  file declares the same sizes; ETS cannot download more than the device holds. Nothing in a
  secure download allocates, so it works with the heap locked.
- **Factory Tool Key (FDSK)**, made from the random source at the first start and kept in the
  flash log among the firmware's own records (0x7F01). Neither a factory reset (Master Reset
  02h) nor a firmware update with a new persistence layout erases it; only a full erase of the
  flash does, which makes a new key and a new device certificate.
- **Device certificate**: the RTT command `k` prints it, with the Data Secure state. It is
  the serial number and the FDSK in the form ETS asks for (36 characters, six groups of six).
- **Random source** (the S-A_Sync random and the FDSK): AES-128 in counter mode, keyed at
  every start from 1024 conversions of the temperature sensor and V<sub>REFINT</sub> (their low
  bits are noise), the MCU's unique ID, the time and a start counter kept in flash (0x7F02),
  which makes every seeding different whatever the noise (KNstaX's
  `knx::security::SeededRandom`).
- **Sequence numbers**: the sending number is reserved ahead in flash, and the partners' last
  valid numbers are saved at least every 15 minutes and at the `SAVE` warning of a bus power
  failure (`requestPowerFailSave()`).
- **Security Mode** belongs to ETS: the device starts with it off and plain commissioning
  still works; ETS switches it on at the end of a secure download.
- **Services ETS uses with a secure device**, which KNstaX switches on with Data Secure: the
  extended memory services (the product file declares them, so ETS writes the parameters and
  tables with `A_MemoryExtended_Write`) and the group object diagnostics (see below).

## Commissioning with ETS6

1. Build and import the product file of version 4
   ([tools/knxprod/README.md](../tools/knxprod/README.md)).
2. Read the device certificate: connect the debug probe, open the RTT console and send `k`.
   The line `device certificate XXXXXX-XXXXXX-XXXXXX-XXXXXX-XXXXXX-XXXXXX` is what ETS needs;
   the next line should say `tool key FDSK`.
3. In ETS6, add the device to a project that has a project password, and enter the device
   certificate for it (ETS asks for it when the device is added, or later in the device's
   properties).
4. Set the security of the group addresses (or objects) that should be secured.
5. Program the individual address and download the application as usual. ETS talks to the
   device with the FDSK, installs a tool key of its own and the keys of the secured group
   addresses, and switches Security Mode on. After that `k` shows `data secure on, tool key
   from ETS`.

To switch a secured group address from the Group Monitor through an interface that cannot
secure telegrams itself (a plain KNXnet/IP interface), ETS6 asks the device to send the
telegram on its behalf with the group object diagnostics. The monitor then shows the device's
individual address as the sender of the write.

A factory reset (the button, 10 s) returns the device to the FDSK with Security Mode off, so
ETS can commission it again with the same certificate.

Keep the device certificate with the device: it is the key to it until ETS has installed its
own. It is printed only when asked for, and only over the debug probe.

## Memory

| | Application version 3 | Version 4, Data Secure |
|---|---:|---:|
| Flash (120 KiB image region = 122,880 B) | 95,756 B | 118,648 B (4,232 B free) |
| Static RAM | 1,208 B + 3 KiB stack | 1,304 B + 5 KiB stack |
| Heap after start | 7,660 B | 9,744 B |
| Stack peak | 1,760 B | 3,388 B (secure download and secured group traffic) |

The stack reserve went from 3 to 5 KiB: a secured telegram puts about 1.6 KiB of buffers on
the stack in the CCM and around it (KNstaX D7 would remove most of them), more than the old
reserve held. With the trace build (`H8I8O_TRACE`) static RAM is 4.5 KB plus the stack, which
leaves about 10.8 KB for the heap: enough, but with only about 1 KB of margin.

## Open points

- **Flash.** 4,232 B of the image region are left; a feature that needs more has to make
  room first.
- **Read protection.** The keys are in flash in plain form, and anyone with a probe on the
  `D C G V` pads can read them. A secured installation needs the option bytes' read
  protection set after programming. It also ends reading the flash over SWD, and the firmware
  does not set it: whether RTT diagnostics still work with it has to be checked on this part
  first.
- **The noise source** is the ADC's conversion noise at the shortest sampling time. Its
  entropy has not been measured on the board; the start counter keeps the S-A_Sync random
  from repeating either way, but the FDSK's unpredictability rests on the noise.
- **A sender outside the Security Individual Address Table** takes one of 16 slots; when they
  are full its secured telegrams are refused (design D5 in KNstaX). ETS is expected to list
  every partner in the table; watch for `InvalidSender` in the log.
