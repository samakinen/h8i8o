#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Program an ELF into the H8i8o over SWD, verify it and start it, then read
# the device certificate ETS asks for when the device is added to a project.
#
#   tools/scripts/flash.sh <firmware.elf> [attempts]
#
# Retries the attach (default 20 attempts): the first attach after power-up
# can fail. OpenOCD settings: tools/scripts/openocd.sh.
#
# The certificate is read over RTT once the firmware runs, printed and saved
# as h8i8o-<serial>.cert.txt in CERT_DIR (default: the current directory), to
# go on a label on the device. It contains the device's Factory Tool Key:
# keep it with the device, and out of version control. A firmware without
# KNX Data Secure has no certificate; the script then says so and still
# succeeds.
set -u
here=$(dirname "$0")
. "$here/openocd.sh"

ELF=${1:?usage: flash.sh <firmware.elf> [attempts]}
ATTEMPTS=${2:-20}
CERT_DIR=${CERT_DIR:-.}
[ -f "$ELF" ] || { echo "error: $ELF not found" >&2; exit 2; }
ELF_NATIVE=$(native_path "$ELF")

# Asks the running firmware for its serial number (a) and certificate (k).
read_certificate() {
    local log status serial cert ocpid
    log=$(mktemp)
    "$OPENOCD" "${OPENOCD_ARGS[@]}" "${RTT_ARGS[@]}" > "$log.openocd" 2>&1 &
    ocpid=$!
    "$PYTHON" "$here/rtt_client.py" --port "$RTT_PORT" --seconds 4 --log "$log" --script "1:a 2:k" > /dev/null 2>&1
    status=$?
    kill "$ocpid" 2>/dev/null
    wait "$ocpid" 2>/dev/null
    serial=$(sed -n 's/.*device serial \([0-9A-F]\{12\}\).*/\1/p' "$log" | tail -n 1)
    cert=$(sed -n 's/.*device certificate \([A-Z2-7]\{6\}\(-[A-Z2-7]\{6\}\)\{5\}\).*/\1/p' "$log" | tail -n 1)
    rm -f "$log" "$log.openocd"
    if [ "$status" -ne 0 ] || [ -z "$serial" ]; then
        echo "warning: could not read the device certificate over RTT; send k on the RTT console" >&2
        return
    fi
    if [ -z "$cert" ]; then
        echo "no device certificate (a firmware without KNX Data Secure)"
        return
    fi
    mkdir -p "$CERT_DIR"
    local file="$CERT_DIR/h8i8o-$serial.cert.txt"
    printf 'serial number      %s\ndevice certificate %s\n' "$serial" "$cert" > "$file"
    echo "device certificate $cert (serial $serial), saved to $file"
}

out=""
for attempt in $(seq 1 "$ATTEMPTS"); do
    out=$("$OPENOCD" "${OPENOCD_ARGS[@]}" -c "init; halt; program {$ELF_NATIVE} verify; reset run; shutdown" 2>&1)
    if echo "$out" | grep -q "Verified OK"; then
        echo "flashed and verified (attempt $attempt)"
        read_certificate
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
