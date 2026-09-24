#!/bin/sh
# SPDX-License-Identifier: MPL-2.0

export HOME=/root
export USER=root
export LOGNAME=root
export PATH=/sbin:/bin:/usr/sbin:/usr/bin
cd /root 2>/dev/null || cd /
printf '\nRX3 volatile USB debug shell (firmware 1.19)\n'
exec /bin/sh -i
