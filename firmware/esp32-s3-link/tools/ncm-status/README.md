# NCM bridge status probe

`rx3-ncm-status` exchanges a private Ethernet diagnostic frame with the S3 and
prints its bridge state, last Wi-Fi transmit result, and packet counters. The
probe does not require either side of the NCM link to have an IP address.

Build it inside the existing RX3 ARM toolchain container:

```sh
./build.sh
```

Copy `build/rx3-ncm-status` to the RX3 and run it as root:

```sh
./rx3-ncm-status usb0
```

Future firmware updates can enter the ESP32-S3 ROM USB downloader without the
physical buttons:

```sh
./rx3-ncm-status usb0 bootloader
```

The command waits for the S3 to acknowledge the request before it resets. The
USB device then re-enumerates as Espressif's `303a:1001` download interface.
