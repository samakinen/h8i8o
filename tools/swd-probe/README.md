# An ESP32-S2 board as the SWD probe

Any CMSIS-DAP probe that OpenOCD supports can program the H8i8o. This is how an ESP32-S2
board (tested: a YD-ESP32-S2-mini) becomes one, with Espressif's
[esp-usb-bridge](https://github.com/espressif/esp-usb-bridge) firmware in CMSIS-DAP SWD mode.

## Build the bridge firmware

With ESP-IDF (for example the `espressif/idf` Docker image), in a clone of esp-usb-bridge:

```
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32s2;<path-to-h8i8o>/tools/swd-probe/sdkconfig.defaults.h8i8o" set-target esp32s2
idf.py build
```

`sdkconfig.defaults.h8i8o` selects SWD, with SWCLK on GPIO10 and SWDIO on GPIO8.

## Flash it

1. Hold BOOT (GPIO0), tap RESET, release BOOT: the board appears as a serial port in ROM
   download mode.
2. From the bridge's `build` directory:

   ```
   esptool --chip esp32s2 -p <port> -b 460800 --after no_reset write_flash --flash_mode dio --flash_freq 80m --flash_size 4MB 0x1000 bootloader/bootloader.bin 0x8000 partition_table/partition-table.bin 0x10000 bridge.bin
   ```

3. Tap RESET. The board enumerates as a CMSIS-DAP probe.

The bridge takes over the USB port, so repeat step 1 to flash the ESP32-S2 again.

## Wiring

| H8i8o pad | Signal | ESP32-S2 |
|---|---|---|
| D | SWDIO | GPIO8 |
| C | SWCLK | GPIO10 |
| G | GND | GND |
| V | 3.3 V | not connected |

Read the power and grounding notes in [docs/hardware.md](../../docs/hardware.md#debug-header-and-power)
first.

Espressif's OpenOCD build (`openocd-esp32`, shipped with ESP-IDF) includes the CMSIS-DAP
driver and the `stm32f1x` target. If it fails with `CMSIS-DAP command CMD_INFO failed`,
unplug the ESP32-S2 and plug it in again.
