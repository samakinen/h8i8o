# H8i8o KNX firmware

Open-source replacement firmware for the **H8i8o V0.8** KNX I/O module (GD32F103CB MCU,
Siemens TP-UART2 transceiver), built on the [KNstaX](https://github.com/samakinen/KNstaX)
KNX stack. With it the module is an ETS-configurable KNX TP1 device with eight binary inputs
and eight switching outputs.

## Features

- Commissioned with ETS6 like any KNX device: individual address by programming button,
  application download, group communication. Configuration is kept in flash across
  restarts and bus power cuts.
- **8 inputs** (DI1–DI8). Each sends its state (DPT 1.001) on change and answers reads.
  Per input: enabled or disabled, normally open or normally closed, debounce time 10–200 ms.
- **8 outputs** (DO1–DO8), each with a switch object and a status object (DPT 1.001). Per
  output: enabled or disabled, normally open or normally closed operation, status sent on
  change or not at all.
- Programming button and LED.
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
- Outputs are not restored after a restart or bus power cut; they start released.
- No factory reset by button, and no programming-mode timeout.
- Programming the firmware destroys the factory firmware for good (see below).

## Repository layout

| Path | Content |
|---|---|
| `app/` | The product definition (`product.hpp`) and the application (`main.cpp`) |
| `bsp/`, `startup/` | Board support, start-up code and linker script |
| `external/KNstaX/` | The KNX stack (git submodule) |
| `docs/` | [Application](docs/application.md), [hardware](docs/hardware.md), [firmware internals](docs/firmware.md) |
| `tools/docker/` | Toolchain images for the firmware and the ETS product file |
| `tools/knxprod/` | Build of the ETS product file |
| `tools/scripts/` | Flashing and RTT console scripts |
| `tools/swd-probe/` | Settings for using an ESP32-S2 board as the SWD probe |

Clone with the submodule:

```
git clone --recursive <repository-url>
```

or run `git submodule update --init` in an existing clone.

## What you need

- An H8i8o V0.8 board and a KNX TP1 line with a power supply.
- An SWD probe that OpenOCD supports, and OpenOCD 0.12 or later. Tested with an ESP32-S2
  running esp-usb-bridge as a CMSIS-DAP probe ([tools/swd-probe](tools/swd-probe/README.md)).
- Docker, for the toolchains.
- ETS6, and OpenKNXproducer or Kaenx-Creator to sign the product file.
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
`build/fw/h8i8o.elf` (and `h8i8o.bin`, to be written at `0x08000000`).

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

The script retries the attach, programs, verifies and starts the firmware. Programming
leaves the persistence pages alone, so the device keeps its ETS configuration across
firmware updates as long as the product's objects stay the same.

**Back to factory state** (address 15.15.255, no configuration): erase the whole chip, then
program the firmware again.

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

1. Build and sign the product file: [tools/knxprod/README.md](tools/knxprod/README.md).
2. Import it into ETS6 and add "H8i8o 8 inputs 8 outputs" to the project.
3. Program the individual address. Press the programming button **once** when ETS asks; the
   LED lights while programming mode is on.
4. Download the application, then link group addresses to the objects.

Details: [docs/application.md](docs/application.md#commissioning-with-ets6).

## Possible future improvements

The module's hardware allows considerably more than this firmware does. Candidates:

- **Input functions** beyond Switch: toggle on press, short and long press, double click,
  dimming (DPT 3.007), shutter (DPT 1.008/1.007), sending fixed values and scenes; a lock
  object per input; sending the state after bus voltage recovery, with a delay; cyclic
  sending.
- **Output functions**: switch-on and switch-off delays, staircase timer, lock and forced
  control, behaviour after bus power failure, recovery and ETS download, restoring the
  state after a power cut, a central switch object, scenes, an inverted or cyclic status.
- **Bus power failure**: switch every active output off as soon as the TP-UART2's `SAVE`
  warns of it (the firmware already takes that warning from an interrupt, ahead of the
  20 ms debounce). Loads such as LEDs on the outputs then stop draining the capacitors,
  which lengthens the time left to store state before the supply gives out (122 ms measured
  with every output off; see [docs/hardware.md](docs/hardware.md#observed-behaviour-of-the-parts)).
- **Device**: factory reset by a long press of the button, a programming-mode timeout, LED
  patterns for "not loaded" and "transceiver not ready", an "in operation" heartbeat
  object.
- The DIAUX and AUX signals, once their electrical function is known.
- KNX Data Secure. It fits the 128 KiB flash and 20 KiB RAM, after work on the stack and on
  this firmware: [docs/data-secure.md](docs/data-secure.md).
- A build without the RTT host-link trace, for a smaller image.
- A registered manufacturer ID, for a product that can be distributed.

## Licence

GPL-3.0-or-later, the licence of KNstaX: see [LICENSE](LICENSE).

## Acknowledgements

The board's pin map was first published by [thelsing/knx](https://github.com/thelsing/knx)
and [pavkriz/hacking-knx-h8i8o](https://github.com/pavkriz/hacking-knx-h8i8o).
