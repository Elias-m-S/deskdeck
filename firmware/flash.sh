#!/usr/bin/env bash
# Builds the firmware, reboots the DeskDeck into the bootloader via software, flashes and restarts it.
#
#   ./flash.sh            build + flash
#   ./flash.sh monitor    open the serial monitor afterwards
#
# No BOOT/RST needed as long as a firmware that understands @BOOTLOADER is running.
# Alternative on the device: long-press the encoder -> Einstellungen -> "Firmware flashen".
#
# ESP-IDF: either have idf.py in PATH (export.sh sourced) or set IDF_PATH / IDF_EXPORT_SH.
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"

FW_GLOB='/dev/serial/by-id/usb-*_DeskDeck_*-if00'
FW_GLOB_LEGACY='/dev/serial/by-id/usb-*_Stream_Deck_*-if00'   # boards still running the pre-rename firmware
BL_GLOB='/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_*-if00'
SERVICE=deskdeck

find_port() { compgen -G "$1" | head -n1 || true; }
wait_port() { # wait_port <glob> <seconds>
    local p i
    for ((i = 0; i < $2 * 5; i++)); do
        p=$(find_port "$1"); [[ -n $p ]] && { echo "$p"; return 0; }
        sleep 0.2
    done
    return 1
}

if ! command -v idf.py >/dev/null; then
    export_sh="${IDF_EXPORT_SH:-${IDF_PATH:-$HOME/esp/esp-idf}/export.sh}"
    if [[ ! -f $export_sh ]]; then
        echo "!! idf.py not found and no ESP-IDF at $export_sh (set IDF_PATH or IDF_EXPORT_SH)." >&2
        exit 1
    fi
    # shellcheck disable=SC1090
    source "$export_sh" >/dev/null
fi

echo ">> Building"
idf.py build >/dev/null || idf.py build   # on failure, repeat with full output

# The host daemon keeps the port open -> pause it
restart_service=0
if systemctl --user is-active --quiet "$SERVICE" 2>/dev/null; then
    echo ">> Pausing host daemon"
    systemctl --user stop "$SERVICE"
    restart_service=1
fi
trap '[[ $restart_service == 1 ]] && systemctl --user start "$SERVICE"' EXIT

bl=$(find_port "$BL_GLOB")
if [[ -z $bl ]]; then
    fw=$(find_port "$FW_GLOB")
    [[ -z $fw ]] && fw=$(find_port "$FW_GLOB_LEGACY")
    if [[ -z $fw ]]; then
        echo "!! No DeskDeck found (neither firmware nor bootloader). Check the USB cable." >&2
        exit 1
    fi
    echo ">> Requesting bootloader via $fw"
    stty -F "$fw" 115200 raw -echo
    printf '\n@BOOTLOADER\n' > "$fw"
    if ! bl=$(wait_port "$BL_GLOB" 8); then
        echo "!! Bootloader port did not appear." >&2
        echo "   Manually: hold BOOT, tap RST, release BOOT - then run ./flash.sh again." >&2
        exit 1
    fi
fi

echo ">> Flashing via $bl"
(
    cd build
    python -m esptool --chip esp32s3 -p "$bl" -b 460800 \
        --before default-reset --after watchdog-reset write-flash @flash_args
)

if fw=$(wait_port "$FW_GLOB" 10); then
    echo ">> Running: $fw"
else
    echo "!! Firmware does not come up - press RST once." >&2
    exit 1
fi

if [[ ${1:-} == monitor ]]; then
    trap - EXIT   # daemon stays paused while the monitor is running
    exec idf.py -p "$fw" monitor --no-reset
fi
