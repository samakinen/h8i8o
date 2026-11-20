#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Program an ELF into the H8i8o over SWD, verify it and start it.
#
#   tools/scripts/flash.sh <firmware.elf> [attempts]
#
# Retries the attach (default 20 attempts): the first attach after power-up
# can fail. OpenOCD settings: tools/scripts/openocd.sh.
set -u
here=$(dirname "$0")
. "$here/openocd.sh"

ELF=${1:?usage: flash.sh <firmware.elf> [attempts]}
ATTEMPTS=${2:-20}
[ -f "$ELF" ] || { echo "error: $ELF not found" >&2; exit 2; }
ELF_NATIVE=$(native_path "$ELF")

out=""
for attempt in $(seq 1 "$ATTEMPTS"); do
    out=$("$OPENOCD" "${OPENOCD_ARGS[@]}" -c "init; halt; program {$ELF_NATIVE} verify; reset run; shutdown" 2>&1)
    if echo "$out" | grep -q "Verified OK"; then
        echo "flashed and verified (attempt $attempt)"
        exit 0
    fi
    if echo "$out" | grep -qi "unable to find\|no device found\|couldn't open\|CMD_INFO failed"; then
        echo "error: the debug probe is missing or not answering (unplug it and plug it in again)" >&2
        echo "$out" >&2
        exit 2
    fi
done
echo "error: could not flash after $ATTEMPTS attempts; last output:" >&2
echo "$out" >&2
exit 1
