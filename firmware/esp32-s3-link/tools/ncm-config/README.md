# NCM configuration command

`rx3-s3-config` exercises the versioned S3 configuration protocol directly
from the RX3. It reads live status, atomically changes Wi-Fi credentials, and
requests reconnection without requiring Wi-Fi association or an IP address.

Build it in the RX3 ARM container:

```sh
./build.sh
```

On the RX3, run:

```sh
./rx3-s3-config usb0 status
./rx3-s3-config usb0 set 'network name'
./rx3-s3-config usb0 reconnect
```

`set` reads the password from a prompt with terminal echo disabled. For
automation it reads standard input. The password is never accepted as a
command-line argument, printed, logged, or returned by the S3. The local
password buffer is cleared after the acknowledged exchange.

The separate `ncm-status` tool retains the bridge counters and ROM bootloader
recovery commands.
