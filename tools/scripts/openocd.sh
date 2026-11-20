# SPDX-License-Identifier: GPL-3.0-or-later
# Shared OpenOCD settings for the H8i8o scripts; sourced, not run.
#
# Environment (all optional):
#   OPENOCD            OpenOCD executable (default: openocd on PATH)
#   OPENOCD_SCRIPTS    its scripts directory, if OpenOCD does not find it itself
#   OPENOCD_INTERFACE  probe configuration (default: interface/cmsis-dap.cfg)
#   OPENOCD_TRANSPORT  transport (default: swd)
#   OPENOCD_SPEED      SWD clock in kHz (default: 400)
#   PYTHON             Python 3 interpreter (default: python3, else python)
#
# The GD32F103 is driven as an STM32F1: "set CPUTAPID 0" accepts its ID, and
# "reset_config none" because the board has no reset line on its debug header.

OPENOCD=${OPENOCD:-openocd}
OPENOCD_ARGS=()
[ -n "${OPENOCD_SCRIPTS:-}" ] && OPENOCD_ARGS+=(-s "$OPENOCD_SCRIPTS")
OPENOCD_ARGS+=(-f "${OPENOCD_INTERFACE:-interface/cmsis-dap.cfg}"
               -c "transport select ${OPENOCD_TRANSPORT:-swd}" -c "set CPUTAPID 0"
               -f target/stm32f1x.cfg
               -c "adapter speed ${OPENOCD_SPEED:-400}" -c "reset_config none")

# RTT port served by OpenOCD; the control block is searched for in all 20 KiB of RAM.
RTT_PORT=${RTT_PORT:-19021}
RTT_ARGS=(-c "init" -c "rtt setup 0x20000000 20480 {SEGGER RTT}" -c "rtt start"
          -c "rtt server start $RTT_PORT 0")

if [ -z "${PYTHON:-}" ]; then
    if python3 -c "" 2>/dev/null; then PYTHON=python3; else PYTHON=python; fi
fi

# A path as OpenOCD expects it (Windows OpenOCD under Git Bash needs C:/...).
native_path() {
    if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s\n' "$1"; fi
}
