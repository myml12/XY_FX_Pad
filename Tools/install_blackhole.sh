#!/usr/bin/env bash
# Build BlackHole from ./BlackHole and install it as a Core Audio HAL plugin.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/BlackHole"
DRIVER_SRC="$SRC/build/Release/BlackHole.driver"
DRIVER_DST="/Library/Audio/Plug-Ins/HAL/BlackHole2ch.driver"

if [[ ! -d "$SRC/BlackHole.xcodeproj" ]]; then
  echo "BlackHole source not found at $SRC"
  echo "Clone it first: git clone https://github.com/ExistentialAudio/BlackHole.git"
  exit 1
fi

echo "Building BlackHole..."
xcodebuild \
  -project "$SRC/BlackHole.xcodeproj" \
  -configuration Release \
  CODE_SIGN_IDENTITY="-" \
  CODE_SIGNING_REQUIRED=NO \
  CODE_SIGNING_ALLOWED=NO

if [[ ! -d "$DRIVER_SRC" ]]; then
  echo "Build succeeded but driver bundle was not found: $DRIVER_SRC"
  exit 1
fi

echo "Installing to $DRIVER_DST (sudo required)..."
sudo rm -rf "$DRIVER_DST"
sudo cp -R "$DRIVER_SRC" "$DRIVER_DST"
sudo killall -9 coreaudiod || true

echo
echo "Installed. Device name should appear as \"BlackHole 2ch\"."
echo "Next:"
echo "  1. Launch DJ XY Pad and switch to PC Audio mode"
echo "  2. The app routes system output to BlackHole and plays through your speakers"
