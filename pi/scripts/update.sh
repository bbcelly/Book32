#!/bin/bash
# Update InkDeck for Raspberry Pi from GitHub and restart it.
# Your books and data/ are not tracked by git, so updates never touch them.

set -e
APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$APP_DIR"

echo "Updating $(git rev-parse --show-toplevel)..."
before="$(git rev-parse HEAD)"
git pull --ff-only
if ! git diff --quiet "$before" HEAD -- requirements.txt; then
    pip3 install --break-system-packages -q -r requirements.txt
fi
# Fonts added after the first release (the Native reading font).
if ! dpkg -s fonts-freefont-ttf >/dev/null 2>&1; then
    sudo -n apt-get install -y -q fonts-freefont-ttf || echo "Could not install fonts-freefont-ttf"
fi
echo "Updated to $(git log -1 --format='%h %s')"

# The service file changes now and then (port 80 for Wi-Fi setup).
if [ -f /etc/systemd/system/inkdeck.service ]; then
    unit="$(sed -e "s|@USER@|$(id -un)|g" -e "s|@APP_DIR@|$APP_DIR|g" scripts/inkdeck.service)"
    if [ "$unit" != "$(cat /etc/systemd/system/inkdeck.service)" ]; then
        echo "$unit" | sudo -n tee /etc/systemd/system/inkdeck.service >/dev/null && sudo -n systemctl daemon-reload
    fi
fi

if systemctl is-enabled --quiet inkdeck.service 2>/dev/null; then
    echo "Restarting InkDeck in 3 seconds..."
    # A timer, so this command finishes before the service (and the web terminal) restarts.
    sudo -n systemd-run --quiet --on-active=3 systemctl restart inkdeck.service
fi
