#!/usr/bin/env bash
# Installs the host daemon as a systemd user service that starts at login.
#
#   ./install.sh            link daemon to ~/.local/bin, install + enable the service
#   ./install.sh uninstall  stop, disable and remove it again
set -euo pipefail
HERE=$(cd "$(dirname "$(readlink -f "$0")")" && pwd)
BIN=~/.local/bin/deskdeck-daemon
UNIT_DIR=~/.config/systemd/user

if [[ ${1:-} == uninstall ]]; then
    systemctl --user disable --now deskdeck 2>/dev/null || true
    rm -f "$UNIT_DIR/deskdeck.service" "$BIN"
    systemctl --user daemon-reload
    echo "DeskDeck daemon removed."
    exit 0
fi

python3 -c "import serial" 2>/dev/null || {
    echo "pyserial is missing: sudo apt install python3-serial pulseaudio-utils" >&2
    exit 1
}

mkdir -p "$(dirname "$BIN")" "$UNIT_DIR"
ln -sf "$HERE/deskdeck_daemon.py" "$BIN"
chmod +x "$HERE/deskdeck_daemon.py"
cp "$HERE/deskdeck.service" "$UNIT_DIR/deskdeck.service"
systemctl --user daemon-reload
systemctl --user enable --now deskdeck
echo "DeskDeck daemon installed and running. Logs: journalctl --user -u deskdeck -f"
