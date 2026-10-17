#!/bin/bash
# Set how many CPU cores start at boot (maxcpus in cmdline.txt). Run as root;
# the web interface calls it through sudo. Takes effect after a reboot.
# Reference: https://www.jeffgeerling.com/blog/2021/disabling-cores-reduce-pi-zero-2-ws-power-consumption-half
#   apply_cpu_cores.sh <1 to 4>    (4 removes the limit)

set -e
CORES="$1"
if ! [[ "$CORES" =~ ^[1-4]$ ]]; then
    echo "Usage: $0 <1 to 4>" >&2
    exit 1
fi
if [ -f /boot/firmware/cmdline.txt ]; then CMDLINE=/boot/firmware/cmdline.txt; else CMDLINE=/boot/cmdline.txt; fi

cp "$CMDLINE" "$CMDLINE.inkdeck-backup"
# cmdline.txt must stay a single line.
line="$(head -n 1 "$CMDLINE" | sed -E 's/ ?maxcpus=[0-9]+//g')"
[ "$CORES" != 4 ] && line="$line maxcpus=$CORES"
printf '%s\n' "$line" > "$CMDLINE"
echo "Boot cores set to $CORES in $CMDLINE (backup: $CMDLINE.inkdeck-backup). Reboot to apply."
