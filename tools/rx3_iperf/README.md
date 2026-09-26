# RX3 iperf3

This directory builds a static ARM EABI iperf 3.21 client for the RX3. The
build uses pinned iperf and musl releases inside the user Podman RX3 toolchain.

```sh
tools/rx3_iperf/build.sh
```

The output is `tools/rx3_iperf/build/iperf3`. It is a stripped, static ARMv7
soft-float executable and does not depend on the RX3's old C library.

The firmware 1.19 kernel can run a single TCP stream, but iperf 3.21's local
byte accounting is incorrect and its parallel workers can fail while reading
`/dev/urandom` with `EFAULT`. UDP pacing is incorrect as well. Treat the modern
peer's single-stream TCP counters as authoritative. The first antenna-on test
measured 4.13 Mbit/s from RX3 to LAN and 6.94 Mbit/s from LAN to RX3.

Copy the binary into `/dev/shm` and run it from RAM:

```sh
chmod 700 /dev/shm/iperf3
/dev/shm/iperf3 -c 10.0.0.1 -t 10 -O 2
/dev/shm/iperf3 -c 10.0.0.1 -t 10 -O 2 -R
```

Redirect the command to a file and detach it from Telnet. Sustained bridge
traffic can otherwise terminate the diagnostic console's foreground child.
