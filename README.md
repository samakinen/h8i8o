# H8i8o KNX firmware

Open-source replacement firmware for the **H8i8o V0.8** KNX I/O module (GD32F103CB MCU,
Siemens TP-UART2 transceiver), built on the [KNstaX](https://github.com/samakinen/KNstaX)
KNX stack. With it the module is an ETS-configurable KNX TP1 device with eight binary inputs
and eight switching outputs.

## Features

- Commissioned with ETS6 like any KNX device: individual address by programming button,
  application download, group communication. Configuration is kept in flash across
  restarts and bus power cuts.
- **8 inputs** (DI1–DI8), each with a function chosen in ETS: switch (an action per edge),
  alarm, window/door, presence or open/close contact, short and long press, dimming, shutter,
  value or percentage, scene, and (in the `extended` variant) pulse counter or energy meter.
  Every input can be locked, sent cyclically and sent after a start; normally open or
  normally closed, debounce time 10–200 ms.
- **8 outputs** (DO1–DO8), each with a function: switch with on and off delays, staircase
  light with a warning, pulse, or heating valve by pulse width. Every output has a status
  object, a lock and the central switch, and in the `extended` variant forced operation and
  four scenes with learning; normally open or normally closed operation, released or restored
  after a restart or bus power failure.
- Device: "in operation" heartbeat, a delay before sending after a start, a telegram limit.
- ETS shows only the objects and parameters a channel's function uses, each object with the
  datapoint type that function sends.
- Two build variants: `standard` with KNX Data Secure, `extended` with every function and
  without Data Secure ([docs/firmware.md](docs/firmware.md#build-variants)).
- Bus power failure: the outputs are released as soon as the TP-UART2 warns of it, and their
  states (and the counters) are stored.
- Programming button and LED. Programming mode ends by itself after 15 minutes; holding the
  button for 10 seconds does a factory reset.
- KNX Data Secure (`standard` variant): secured group communication and a secure ETS
  download, with a Factory Tool Key the device makes itself and a device certificate for ETS
  ([docs/data-secure.md](docs/data-secure.md)).
- Safe start: the outputs are driven inactive first thing after reset.
- Watchdog, and diagnostics over SEGGER RTT through the debug probe.

[docs/application.md](docs/application.md) describes the objects, parameters and behaviour
in full.

## Status and limitations

The firmware runs on a V0.8 board on a live bus: ETS6 imports the product, programs the
address, downloads the application and parameters, and the inputs and outputs work through
group telegrams. Before you use it, note that:

- **Not a certified KNX product.** It uses KNstaX's example manufacturer ID 0x00FA, which
  belongs to the KNX Association (ETS shows that name). It is not registered or certified.
  Use it in your own installations at your own risk.
- **Only DI1 and DO1 were measured** electrically. DI2–DI8 and DO2–DO8 follow the community
  pin map and are assumed to behave the same. The ratings of the input and output stages, and
  what the I/O board connects to them, are not characterised: check yours before connecting
  loads, especially mains loads.
- **The channel functions of application version 5 have not run on a board yet.** They are
  tested on the host, and both variants build and produce schema-valid product files; the
  ETS6 import, the downloads and the heap after start still need checking on the board
  ([docs/application.md](docs/application.md#checking-a-download-on-the-board)).
- KNX Data Secure (application version 4) has run on the board with ETS6 (secure
  commissioning, a secured download, secured group communication). It protects the bus, not a
  device in someone's hands; [docs/data-secure.md](docs/data-secure.md) explains that and lists
  what is open.
- Programming the firmware destroys the factory firmware for good (see below).

## Repository layout

| Path | Content |
|---|---|
| `app/` | The product definition (`product.hpp`), the channel functions (`logic/`) and the glue to the board and the bus (`main.cpp`) |
| `knx.defconfig` | KNstaX capacities and APDU length, for the firmware and the product file |
| `Kconfig`, `variants/` | Application options and the build variants (`standard`, `extended`) |
| `test/` | Host tests of the channel functions |
| `bsp/`, `startup/` | Board support, start-up code and linker script |
| `external/KNstaX/` | The KNX stack (git submodule) |
| `docs/` | [Application](docs/application.md), [hardware](docs/hardware.md), [firmware internals](docs/firmware.md), [ETS product file](docs/ets-product.md) |
| `tools/docker/` | Toolchain image for the firmware and its ETS product file |
| `product.lock` | Application version and content of each variant's ETS product |
| `tools/scripts/` | Flashing and RTT console scripts |
| `tools/swd-probe/` | Settings for using an ESP32-S2 board as the SWD probe |

Clone with the submodule:

```
git clone --recursive <repository-url>
```

or run `git submodule update --init` in an existing clone.

The submodule tracks KNstaX's `main` branch. A clone checks out the commit recorded here; to move
to the latest KNstaX, run `git submodule update --remote external/KNstaX`, build, and commit the
new submodule commit.

## What you need

- An H8i8o V0.8 board and a KNX TP1 line with a power supply.
- An SWD probe that OpenOCD supports, and OpenOCD 0.12 or later. Tested with an ESP32-S2
  running esp-usb-bridge as a CMSIS-DAP probe ([tools/swd-probe](tools/swd-probe/README.md)).
- Docker, for the toolchains.
- ETS6, and OpenKNXproducer (or Kaenx-Creator) to sign the product file.
- Python 3, for the helper scripts.

## Build the firmware

Build the toolchain image once:

```
docker build -t h8i8o-arm -f tools/docker/Dockerfile.arm tools/docker
```

Then, from the repository root:

```
docker run --rm -v "$PWD":/work -w /work h8i8o-arm sh -c \
  "cmake -S . -B build/fw -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel \
   -DCMAKE_TOOLCHAIN_FILE=external/KNstaX/cmake/toolchains/arm-none-eabi.cmake && cmake --build build/fw"
```

On Git Bash for Windows, prefix `docker run` with `MSYS_NO_PATHCONV=1`. The result is
`build/fw/h8i8o.elf` (and `h8i8o.bin`, to be written at `0x08000000`), and the ETS product
file of the same variant, `build/fw/h8i8o_standard.knxprod.xml`, with everything collected in
`build/fw/dist/h8i8o_standard/` ([docs/ets-product.md](docs/ets-product.md)).

That builds the `standard` variant, with KNX Data Secure. For the `extended` one (every
function, no Data Secure) add `-DH8I8O_VARIANT=extended`, preferably into another build
directory ([docs/firmware.md](docs/firmware.md#build-variants)).

Add `-DH8I8O_TRACE=ON` to the first `cmake` for the host link trace: every octet between the
MCU and the TP-UART2 in the RTT log, for diagnosing an ETS download. It costs about 3 KiB of
RAM, so it is off by default ([docs/firmware.md](docs/firmware.md#diagnostics-over-rtt)).

## Flash the board

Wire the probe to the `D C G V` pads, leaving `V` unconnected, and power the board from the
bus. Read [docs/hardware.md](docs/hardware.md#debug-header-and-power) first: the board's
ground is part of the bus circuit.

The scripts find OpenOCD on `PATH`, or in `$OPENOCD` (with `$OPENOCD_SCRIPTS` for its
scripts directory). They assume a CMSIS-DAP probe; set `OPENOCD_INTERFACE` and
`OPENOCD_TRANSPORT` for another (`tools/scripts/openocd.sh`). The OpenOCD options used
throughout are:

```
openocd -f interface/cmsis-dap.cfg -c "transport select swd" -c "set CPUTAPID 0" \
        -f target/stm32f1x.cfg -c "adapter speed 400" -c "reset_config none" ...
```

**First time only: remove the read protection.** This mass-erases the chip. The factory
firmware is lost and cannot be restored.

```
openocd <options above> -c "init; reset halt; stm32f1x unlock 0; shutdown"
```

Then disconnect the board from the bus for a few seconds, so the change takes effect.

**Program the firmware:**

```
tools/scripts/flash.sh build/fw/h8i8o.elf
```

The script retries the attach, programs, verifies and starts the firmware. It then reads the
device certificate that ETS needs for KNX Data Secure, prints it and saves it as
`h8i8o-<serial>.cert.txt` (in `CERT_DIR`, by default the current directory): put it on a label
on the device. Programming leaves the persistence pages alone, so the device keeps its ETS configuration across
firmware updates as long as the product's objects stay the same.

**Back to factory state** (address 15.15.255, no configuration): hold the programming button
for 10 seconds and release it while the LED flashes fast. Or erase the whole chip and program
the firmware again; for a device that will use KNX Data Secure prefer the button, which keeps
the secure sequence number ([docs/application.md](docs/application.md#factory-reset)).

```
openocd <options above> -c "init; reset halt; stm32f1x mass_erase 0; shutdown"
```

**Check that it runs.** Record its log for ten seconds and ask for the address and the
statistics:

```
tools/scripts/rtt_console.sh build/logs/check.log 10 "2:a 3:s"
```

The log starts with `h8i8o start` and `started, address …`.
[docs/firmware.md](docs/firmware.md#diagnostics-over-rtt) lists the commands.

## Commission with ETS6

1. Sign the product file the build wrote: [docs/ets-product.md](docs/ets-product.md#pack-and-sign).
2. Import it into ETS6 and add "H8i8o 8 inputs 8 outputs" to the project.
3. Program the individual address. Press the programming button **once** when ETS asks; the
   LED lights while programming mode is on.
4. Download the application, then link group addresses to the objects.

For KNX Data Secure, ETS also needs the device certificate, which `flash.sh` saves after
programming and the RTT log shows at every start:
[docs/data-secure.md](docs/data-secure.md#commissioning-with-ets6).

Details: [docs/application.md](docs/application.md#commissioning-with-ets6).

## Possible future improvements

The module's hardware allows considerably more than this firmware does. Candidates:

- **Input functions** beyond Switch: toggle on press, short and long press, double click,
  dimming (DPT 3.007), shutter (DPT 1.008/1.007), sending fixed values and scenes; a lock
  object per input; sending the state after bus voltage recovery, with a delay; cyclic
  sending.
- **Output functions**: switch-on and switch-off delays, staircase timer, lock and forced
  control, "on" or "off" after bus voltage recovery and after an ETS download, a central
  switch object, scenes, an inverted or cyclic status.
- **Device**: LED patterns for "not loaded" and "transceiver not ready", an "in operation"
  heartbeat object.
- The DIAUX and AUX signals, once their electrical function is known.
- Moving the board support onto KNstaX's GD32F1 reference port (`ports/gd32f1`), which
  covers the same MCU.
- A registered manufacturer ID, for a product that can be distributed.

## Licence

GPL-3.0-or-later, the licence of KNstaX: see [LICENSE](LICENSE).

## Acknowledgements

The board's pin map was first published by [thelsing/knx](https://github.com/thelsing/knx)
and [pavkriz/hacking-knx-h8i8o](https://github.com/pavkriz/hacking-knx-h8i8o).
