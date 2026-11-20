#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Record the running firmware's RTT log and optionally send it commands.
#
#   tools/scripts/rtt_console.sh <log-file> <seconds> [script]
#
# Records RTT channel 0 for <seconds> into <log-file> and prints it as it
# arrives. The optional script is a list of "<second>:<characters>" items
# separated by spaces; each sends its characters to the firmware at that
# second, for example "1:s 3:1 5:1 6:s" (statistics, DO1 toggled twice,
# statistics again). Commands: docs/firmware.md, "Diagnostics".
#
# Does not reset or flash the board. OpenOCD settings: tools/scripts/openocd.sh.
set -u
here=$(dirname "$0")
. "$here/openocd.sh"

LOG=${1:?usage: rtt_console.sh <log-file> <seconds> [script]}
SECONDS_TO_LOG=${2:?usage: rtt_console.sh <log-file> <seconds> [script]}
SCRIPT=${3:-}

mkdir -p "$(dirname "$LOG")"
"$OPENOCD" "${OPENOCD_ARGS[@]}" "${RTT_ARGS[@]}" > "$LOG.openocd" 2>&1 &
OCPID=$!
trap 'kill $OCPID 2>/dev/null; wait $OCPID 2>/dev/null' EXIT

"$PYTHON" "$here/rtt_client.py" --port "$RTT_PORT" --seconds "$SECONDS_TO_LOG" \
    --log "$LOG" --script "$SCRIPT" || { echo "see $LOG.openocd" >&2; exit 1; }
