#!/bin/bash
# InkDeck for Raspberry Pi installer.
#
# Run from the app folder (the one holding inkdeck/ and scripts/):
#   ./scripts/install.sh
# Works for any user name and install location. Installs system packages,
# Python packages and the Waveshare e-Paper library, enables SPI and I2C,
# installs the inkdeck service and, if an old PiBook install is found in
# ~/PiBook, copies its books, reading positions, to-dos and settings.
#
# Options: --no-pisugar   skip the PiSugar power manager
#          --no-service   do not install or start the systemd service

set -e

APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_USER="$(id -un)"
WITH_PISUGAR=1
WITH_SERVICE=1
for arg in "$@"; do
    case "$arg" in
        --no-pisugar) WITH_PISUGAR=0 ;;
        --no-service) WITH_SERVICE=0 ;;
        *) echo "Unknown option: $arg"; exit 1 ;;
    esac
done

if [ "$APP_USER" = "root" ]; then
    echo "Run this as your normal user (it uses sudo when needed), not as root."
    exit 1
fi
if [ -f /boot/firmware/config.txt ]; then BOOT=/boot/firmware; else BOOT=/boot; fi
cd "$APP_DIR"

echo "== InkDeck for Raspberry Pi =="
echo "App folder: $APP_DIR (user $APP_USER)"
[ -f /proc/device-tree/model ] && echo "Board: $(tr -d '\0' < /proc/device-tree/model)"

echo "== System packages"
sudo apt-get update
sudo apt-get install -y python3-pip python3-dev git fonts-dejavu-core fonts-freefont-ttf libopenjp2-7 \
    libjpeg-dev libfreetype-dev libcairo2 expect avahi-utils arp-scan nmap netcat-openbsd

echo "== Python packages"
pip3 install --break-system-packages -r requirements.txt

echo "== Waveshare e-Paper library"
if [ ! -d lib/waveshare_epd ]; then
    mkdir -p lib
    tmp="$(mktemp -d)"
    git clone --depth 1 --filter=blob:none --sparse https://github.com/waveshare/e-Paper "$tmp"
    git -C "$tmp" sparse-checkout set RaspberryPi_JetsonNano/python/lib/waveshare_epd
    cp -r "$tmp/RaspberryPi_JetsonNano/python/lib/waveshare_epd" lib/
    rm -rf "$tmp"
fi

echo "== SPI and I2C"
sudo raspi-config nonint do_spi 0
sudo raspi-config nonint do_i2c 0

if [ "$WITH_PISUGAR" = 1 ] && ! systemctl list-unit-files pisugar-server.service >/dev/null 2>&1; then
    echo "== PiSugar power manager"
    wget -O /tmp/pisugar-power-manager.sh http://cdn.pisugar.com/release/pisugar-power-manager.sh
    sudo bash /tmp/pisugar-power-manager.sh
    rm -f /tmp/pisugar-power-manager.sh
fi

mkdir -p books data logs

OLD="$HOME/PiBook"
if [ -d "$OLD" ] && [ "$OLD" != "$APP_DIR" ] && [ ! -f data/.migrated ]; then
    echo "== Copying books and data from $OLD"
    cp -n "$OLD"/books/*.epub books/ 2>/dev/null || true
    [ -f "$OLD/data/reading_progress.json" ] && cp -n "$OLD/data/reading_progress.json" data/
    [ -d "$OLD/data/covers" ] && cp -rn "$OLD/data/covers" data/
    [ -d "$OLD/data/documents" ] && cp -rn "$OLD/data/documents" data/
    [ -f "$OLD/todos.json" ] && cp -n "$OLD/todos.json" data/
    [ -f "$OLD/settings.json" ] && cp -n "$OLD/settings.json" data/
    touch data/.migrated
fi

if [ "$WITH_SERVICE" = 1 ]; then
    echo "== inkdeck service"
    sed -e "s|@USER@|$APP_USER|g" -e "s|@APP_DIR@|$APP_DIR|g" scripts/inkdeck.service |
        sudo tee /etc/systemd/system/inkdeck.service >/dev/null
    sudo systemctl daemon-reload
    sudo systemctl enable inkdeck.service
    if systemctl list-unit-files pibook.service >/dev/null 2>&1; then
        sudo systemctl disable pibook.service
        echo "The old pibook service is disabled; $OLD can be deleted once InkDeck works."
    fi
    # On a timer, so running this from the old web terminal does not cut the installer short.
    sudo systemd-run --quiet --on-active=3 sh -c "systemctl stop pibook.service 2>/dev/null; systemctl restart inkdeck.service"
    echo "InkDeck starts in a few seconds."
fi

echo
echo "Installed. Reboot to enable SPI if this is the first install: sudo reboot"
echo "Then open http://$(hostname).local:5000 to upload books."
echo "Optional battery savers: sudo ./scripts/setup_power_optimization.sh, ./scripts/setup_zram.sh"
