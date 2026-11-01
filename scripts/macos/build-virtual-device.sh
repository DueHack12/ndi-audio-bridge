#!/bin/bash
# Builds the "NDI Audio Bridge" virtual audio device for macOS.
#
# It is a renamed build of BlackHole (https://github.com/ExistentialAudio/BlackHole,
# GPL-3.0), a well-known open-source loopback driver. Once installed, "NDI Audio
# Bridge" appears in System Settings > Sound > Output; whatever the Mac plays
# there is picked up by the NDI Audio Bridge app.
#
# Needs Xcode (full Xcode, not just the Command Line Tools).
#
# Usage: scripts/macos/build-virtual-device.sh [output-dir]
#        (default output: ./build-virtual-device)

set -euo pipefail

BLACKHOLE_TAG="v0.7.2"
DEVICE_NAME="NDI Audio Bridge"
DRIVER_ID="NDIAudioBridge"                       # used for UIDs + bundle file name
BUNDLE_ID="com.ndiaudiobridge.virtualdevice"

OUT_DIR="${1:-$PWD/build-virtual-device}"
mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"

if ! xcodebuild -version >/dev/null 2>&1; then
    echo "xcodebuild is not available. Install Xcode from the App Store, then run:"
    echo "  sudo xcode-select -s /Applications/Xcode.app"
    echo "Alternatively install BlackHole with 'brew install blackhole-2ch' and pick 'BlackHole 2ch' in the app."
    exit 1
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "==> Fetching BlackHole $BLACKHOLE_TAG"
git clone --quiet --depth 1 --branch "$BLACKHOLE_TAG" https://github.com/ExistentialAudio/BlackHole.git "$WORK/BlackHole"
cd "$WORK/BlackHole"

echo "==> Renaming the device to \"$DEVICE_NAME\""
# BlackHole guards these with #ifndef, so defining them first renames the device.
{
    echo "#define kDriver_Name \"$DRIVER_ID\""
    echo "#define kPlugIn_BundleID \"$BUNDLE_ID\""
    echo "#define kHas_Driver_Name_Format false"
    echo "#define kDevice_Name \"$DEVICE_NAME\""
    echo "#define kManufacturer_Name \"NDI Audio Bridge\""
    echo "#define kNumber_Of_Channels 2"
    cat BlackHole/BlackHole.c
} > BlackHole/BlackHole.c.new
mv BlackHole/BlackHole.c.new BlackHole/BlackHole.c

echo "==> Building"
xcodebuild \
    -project BlackHole.xcodeproj \
    -configuration Release \
    -target BlackHole \
    CONFIGURATION_BUILD_DIR="$WORK/build" \
    PRODUCT_BUNDLE_IDENTIFIER="$BUNDLE_ID" \
    ARCHS="arm64 x86_64" ONLY_ACTIVE_ARCH=NO \
    CODE_SIGN_IDENTITY="-" CODE_SIGN_STYLE=Manual DEVELOPMENT_TEAM="" \
    -quiet

DRIVER="$WORK/build/BlackHole.driver"

# Give the plug-in its own factory UUID so it never clashes with a real BlackHole install.
UUID="$(uuidgen)"
sed -i '' "s/e395c745-4eea-4d94-bb92-46224221047c/$UUID/g" "$DRIVER/Contents/Info.plist"

# Ad-hoc sign (required on Apple Silicon).
codesign --force --deep --sign - "$DRIVER"

rm -rf "$OUT_DIR/$DRIVER_ID.driver"
mv "$DRIVER" "$OUT_DIR/$DRIVER_ID.driver"

echo "==> Built $OUT_DIR/$DRIVER_ID.driver"
echo "    Install it with: scripts/macos/install-virtual-device.sh \"$OUT_DIR/$DRIVER_ID.driver\""
