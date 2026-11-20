# SPDX-License-Identifier: GPL-3.0-or-later
"""Read an OpenOCD RTT server into a log file and send scripted commands.

Used by rtt_console.sh. Python 3, standard library only.
"""

import argparse
import socket
import sys
import time


def parse_script(text):
    items = []
    for item in text.split():
        at, _, chars = item.partition(":")
        items.append((float(at), chars.encode("ascii")))
    return sorted(items)


def connect(port, timeout):
    deadline = time.monotonic() + timeout
    while True:
        try:
            return socket.create_connection(("127.0.0.1", port), timeout=1.0)
        except OSError:
            if time.monotonic() > deadline:
                raise
            time.sleep(0.2)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=19021)
    parser.add_argument("--seconds", type=float, required=True)
    parser.add_argument("--log", required=True)
    parser.add_argument("--script", default="")
    args = parser.parse_args()

    try:
        sock = connect(args.port, timeout=10.0)
    except OSError as error:
        print(f"error: no RTT server on port {args.port}: {error}", file=sys.stderr)
        return 1
    sock.settimeout(0.05)
    pending = parse_script(args.script)
    start = time.monotonic()
    with open(args.log, "wb") as log:
        while (elapsed := time.monotonic() - start) < args.seconds:
            while pending and pending[0][0] <= elapsed:
                sock.sendall(pending.pop(0)[1])
            try:
                data = sock.recv(4096)
            except socket.timeout:
                continue
            if not data:
                break
            log.write(data)
            log.flush()
            sys.stdout.write(data.decode("ascii", errors="replace"))
            sys.stdout.flush()
    sock.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
