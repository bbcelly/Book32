#!/bin/bash
# Set the CPU undervolt (over_voltage) in the boot config. Run as root; the
# web interface calls it through sudo. Takes effect after a reboot.
#   apply_undervolt.sh <0 to -8>    (each step is 25 mV lower; 0 removes it)

set -e
LEVEL="$1"
if ! [[ "$LEVEL" =~ ^(0|-[1-8])$ ]]; then
    echo "Usage: $0 <0 to -8>" >&2
    exit 1
fi
if [ -f /boot/firmware/config.txt ]; then CONFIG=/boot/firmware/config.txt; else CONFIG=/boot/config.txt; fi

cp "$CONFIG" "$CONFIG.inkdeck-backup"
# Remove every existing setting: config.txt uses the last one, so duplicates mislead.
sed -i -e '/^over_voltage=/d' -e '/^# InkDeck undervolt$/d' "$CONFIG"
if [ "$LEVEL" != 0 ]; then
    printf '\n# InkDeck undervolt\n[all]\nover_voltage=%s\n' "$LEVEL" >> "$CONFIG"
fi
echo "over_voltage set to $LEVEL in $CONFIG (backup: $CONFIG.inkdeck-backup). Reboot to apply."
