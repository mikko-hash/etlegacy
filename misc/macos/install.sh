#!/bin/sh
# One-line install of ET Legacy for Mac:
#   curl -fsSL https://github.com/mikko-hash/etlegacy/releases/download/latest/install.sh | sh
# curl sets no quarantine flag, so the app opens without a Gatekeeper or
# Privacy & Security prompt. The game downloads the W:ET data itself on first launch.
set -e
URL=${ETL_DMG_URL:-https://github.com/mikko-hash/etlegacy/releases/download/latest/ET-Legacy-macOS.dmg}
TMP=$(mktemp -d)
trap 'hdiutil detach "$TMP/mnt" -quiet 2>/dev/null || true; rm -rf "$TMP"' EXIT

echo "Downloading ET Legacy..."
curl -fL --retry 3 -o "$TMP/etl.dmg" "$URL"
hdiutil attach "$TMP/etl.dmg" -nobrowse -noautoopen -quiet -mountpoint "$TMP/mnt"

echo "Installing to /Applications..."
rm -rf "/Applications/ET Legacy.app"
cp -R "$TMP/mnt/ET Legacy.app" /Applications/
xattr -cr "/Applications/ET Legacy.app"

echo "Done. Starting ET Legacy."
open "/Applications/ET Legacy.app"
