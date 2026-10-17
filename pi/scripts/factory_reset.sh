#!/bin/bash
# Reset InkDeck for Raspberry Pi for a new owner: removes books, reading
# positions, to-dos, documents, settings and logs, every saved Wi-Fi network,
# Bluetooth pairings, shell history and SSH keys, and the Wi-Fi password and
# login that Raspberry Pi Imager left on the boot partition. Then SSH is
# turned off and the Pi restarts.
#
# Afterwards InkDeck starts its InkDeck-Setup Wi-Fi network, so the new owner
# can choose their own network from a phone (see the README).
#
#   sudo ./scripts/factory_reset.sh            Asks first
#   sudo ./scripts/factory_reset.sh --yes      For scripts
#   --keep-ssh                                 Leave SSH on (keys are still removed)
#
# Run it on the Pi itself, or over SSH: the work continues after the
# connection drops, and the Pi restarts when it's done.

set -e

APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_USER="$(stat -c %U "$APP_DIR")"
APP_HOME="$(getent passwd "$APP_USER" | cut -d: -f6)"
YES=0
KEEP_SSH=0
for arg in "$@"; do
    case "$arg" in
        --yes) YES=1 ;;
        --keep-ssh) KEEP_SSH=1 ;;
        --run) ;;  # Internal: the detached second stage
        *) echo "Unknown option: $arg"; exit 1 ;;
    esac
done

if [ "$(id -u)" != 0 ]; then
    echo "Run with sudo: sudo $0 $*"
    exit 1
fi

if [[ " $* " != *" --run "* ]]; then
    if [ "$YES" != 1 ]; then
        echo "This erases all books, reading positions, to-dos, documents and settings,"
        echo "forgets every Wi-Fi network, removes SSH keys and history, and restarts the Pi."
        read -r -p "Type RESET to continue: " answer
        [ "$answer" = "RESET" ] || { echo "Cancelled."; exit 1; }
    fi
    # Detached, so it finishes when removing Wi-Fi drops this SSH session.
    systemd-run --unit=inkdeck-factory-reset --quiet --collect "$APP_DIR/scripts/factory_reset.sh" --run "$@"
    echo "Resetting. The Pi restarts in about a minute and then shows Wi-Fi setup."
    exit 0
fi

exec > /var/tmp/inkdeck-factory-reset.log 2>&1
set +e -x  # Best effort from here: always reach the restart
BOOT=/boot/firmware; [ -d "$BOOT" ] || BOOT=/boot

systemctl stop inkdeck.service || true

# InkDeck's data.
rm -rf "$APP_DIR"/books/* "$APP_DIR"/data/* "$APP_DIR"/data/.migrated "$APP_DIR"/logs/* "$APP_DIR"/config/local.yaml
rm -rf "$APP_HOME/PiBook"   # The old version, with its own copies

# History, keys and caches of the app's user and root (pip packages in ~/.local stay).
for home in "$APP_HOME" /root; do
    rm -rf "$home"/.ssh "$home"/.bash_history "$home"/.python_history "$home"/.lesshst "$home"/.wget-hsts \
        "$home"/.cache "$home"/.gitconfig "$home"/.sudo_as_admin_successful
done

# Bluetooth pairings.
systemctl stop bluetooth 2>/dev/null || true
find /var/lib/bluetooth -mindepth 2 -maxdepth 2 -type d -name '??:??:??:??:??:??' -exec rm -rf {} + 2>/dev/null || true

# Raspberry Pi Imager's settings on the boot partition (readable on any
# computer): the Wi-Fi password and the login. cloud-init has done its work,
# so it is turned off and its copies removed.
if [ -f "$BOOT/network-config" ]; then
    cat > "$BOOT/network-config" <<'NETWORK'
network:
  version: 2
  ethernets:
    eth0:
      dhcp4: true
      optional: true
NETWORK
fi
[ -f "$BOOT/user-data" ] && printf '#cloud-config\n' > "$BOOT/user-data"
rm -f "$BOOT/firstrun.sh" "$BOOT/wpa_supplicant.conf" "$BOOT/userconf.txt" "$BOOT/userconf"
if command -v cloud-init >/dev/null; then
    cloud-init clean --logs || true
    touch /etc/cloud/cloud-init.disabled
fi

# Host name.
hostnamectl set-hostname inkdeck
sed -i -E 's/^(127\.0\.1\.1\s+).*/\1inkdeck/' /etc/hosts

# SSH off and the password locked, unless asked. InkDeck's sudo needs no
# password, so the web terminal can set a new one: sudo passwd <user>.
if [ "$KEEP_SSH" != 1 ]; then
    systemctl disable ssh.service ssh.socket 2>/dev/null || true
    passwd -l "$APP_USER"
fi

# Logs.
journalctl --rotate || true
journalctl --vacuum-time=1s || true
find /var/log -type f \( -name '*.gz' -o -name '*.[0-9]' -o -name '*.old' \) -delete
find /var/log -type f -exec truncate -s 0 {} +
apt-get clean

# Wi-Fi last: every saved network goes (NetworkManager also removes its netplan files).
nmcli -g NAME,TYPE connection show | while IFS=: read -r name type; do
    if [ "$type" = 802-11-wireless ]; then nmcli connection delete "$name"; fi
done
rm -f /etc/netplan/50-cloud-init.yaml
rm -f /var/tmp/inkdeck-factory-reset.log
sync
reboot
