#!/usr/bin/env python3
"""Keep an RX3 Telnet session open while its source-state tracer runs."""

from __future__ import annotations

import argparse
import socket
import time


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="169.254.100.2")
    parser.add_argument("--port", type=int, default=23)
    parser.add_argument("--samples", type=int, default=12000)
    arguments = parser.parse_args()

    with socket.create_connection((arguments.host, arguments.port), timeout=5) as connection:
        connection.settimeout(1)
        time.sleep(0.5)
        try:
            connection.recv(65535)
        except TimeoutError:
            pass

        command = (
            "exec /tmp/trace_source_state.sh "
            f"/tmp/rx3-link-source-state.log {arguments.samples}\r\n"
        )
        connection.sendall(command.encode())
        connection.settimeout(None)
        while connection.recv(4096):
            pass


if __name__ == "__main__":
    main()
