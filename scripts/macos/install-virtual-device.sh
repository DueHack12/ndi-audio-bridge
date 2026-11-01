#!/bin/bash
# Installs (or removes) the "NDI Audio Bridge" virtual audio device on macOS.
#
# Usage:
#   install-virtual-device.sh [path/to/NDIAudioBridge.driver]   install
#   install-virtual-device.sh --uninstall                        remove
#
# With no path it looks for NDIAudioBridge.driver next to this script.

set -euo pipefail

HAL_DIR="/Library/Audio/Plug-Ins/HAL"
TARGET="$HAL_DIR/NDIAudioBridge.driver"

restart_coreaudio() {
    echo "==> Restarting Core Audio (sound will cut out for a moment)"
    sudo launchctl kickstart -k system/com.apple.audio.coreaudiod 2>/dev/null || sudo killall -9 coreaudiod
}

if [[ "${1:-}" == "--uninstall" ]]; then
    echo "==> Removing $TARGET"
    sudo rm -rf "$TARGET"
    restart_coreaudio
    echo "Done."
    exit 0
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SOURCE="${1:-$SCRIPT_DIR/NDIAudioBridge.driver}"

if [[ ! -d "$SOURCE" ]]; then
    echo "Can't find the driver at: $SOURCE"
    echo "Build it first with scripts/macos/build-virtual-device.sh, or pass its path."
    exit 1
fi

echo "==> Installing to $TARGET (you may be asked for your password)"
sudo rm -rf "$TARGET"
sudo mkdir -p "$HAL_DIR"
sudo cp -R "$SOURCE" "$TARGET"
sudo chown -R root:wheel "$TARGET"
sudo xattr -dr com.apple.quarantine "$TARGET" 2>/dev/null || true

restart_coreaudio

echo
echo "Installed. Next:"
echo "  1. System Settings > Sound > Output: choose \"NDI Audio Bridge\""
echo "  2. In the NDI Audio Bridge app, COMPUTER AUDIO: choose \"NDI Audio Bridge\""
