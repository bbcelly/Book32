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
echo "Updated to $(git log -1 --format='%h %s')"

if systemctl is-enabled --quiet inkdeck.service 2>/dev/null; then
    echo "Restarting InkDeck in 3 seconds..."
    # A timer, so this command finishes before the service (and the web terminal) restarts.
    sudo -n systemd-run --quiet --on-active=3 systemctl restart inkdeck.service
fi
