#!/bin/sh
# Build the drag-to-Applications macOS install from the CI's macOS tarball:
#   dist/ET Legacy.app (game, renderer libraries, legacy mod and the W:ET data inside)
#   dist/ET-Legacy-<VER>.dmg (the app and an /Applications link)
#
#   sh misc/macos/package-mac-app.sh <etlegacy-*-macOS.tar.gz> [VER]
#
# Environment: ET_BUNDLE_PAKS=1 puts the W:ET data inside the app (private builds only),
# ET_PAKS_DIR (a folder with pak0.pk3, pak1.pk3 and pak2.pk3; downloaded from the
# ET: Legacy mirror into dist/paks when unset).
# The engine finds its data in Contents/Resources (see the self-contained bundle check in
# sys_main.c) and writes config, downloads and logs to ~/Library/Application Support/etlegacy,
# never into the bundle, so the signature stays valid.
# The app is signed ad hoc like Vietcong's package-mac-app.sh. A copy that never got a
# quarantine flag (ClaudeDrop, curl, USB stick) opens with no Gatekeeper prompt.
set -e
cd "$(dirname "$0")/../.."
TGZ=$1
[ -f "$TGZ" ] || { echo "usage: sh misc/macos/package-mac-app.sh <etlegacy-*-macOS.tar.gz> [VER]"; exit 1; }
VER=${2:-$(basename "$TGZ" | sed -e 's/^etlegacy-//' -e 's/-macOS\.tar\.gz$//')}
MIRROR=https://mirror.etlegacy.com/etmain

mkdir -p dist
STAGE=dist/mac-stage
rm -rf "$STAGE" && mkdir -p "$STAGE"
tar -xzf "$TGZ" -C "$STAGE"

SRC_APP=$(find "$STAGE" -maxdepth 3 -name '*.app' -type d | head -1)
[ -n "$SRC_APP" ] || { echo "no .app in $TGZ"; exit 1; }
SRC_DIR=$(dirname "$SRC_APP")

APP="dist/ET Legacy.app"
rm -rf "$APP"
cp -R "$SRC_APP" "$APP"
mkdir -p "$APP/Contents/Resources"

# everything the upstream package keeps next to the .app moves inside it:
# libraries beside the executable, data folders into Resources
for f in "$SRC_DIR"/*; do
	name=$(basename "$f")
	[ "$f" = "$SRC_APP" ] && continue
	case "$name" in
		*.dylib) cp "$f" "$APP/Contents/MacOS/" ;;
		*.command) ;; # macOS-packs.command: the paks are bundled instead
		*)
			if [ -d "$f" ]; then
				cp -R "$f" "$APP/Contents/Resources/"
			else
				cp "$f" "$APP/Contents/Resources/"
			fi
			;;
	esac
done
mkdir -p "$APP/Contents/Resources/etmain"

# The original W:ET data is free to download but not ours to redistribute, so it only goes into
# private builds (ET_BUNDLE_PAKS=1, e.g. built on Mikko's own Mac). Public builds leave etmain empty.
if [ "$ET_BUNDLE_PAKS" = 1 ]; then
	PAKS=${ET_PAKS_DIR:-dist/paks}
	mkdir -p "$PAKS"
	for p in pak0.pk3 pak1.pk3 pak2.pk3; do
		if [ ! -s "$PAKS/$p" ]; then
			echo "downloading $p"
			curl --fail -L --retry 3 -o "$PAKS/$p" "$MIRROR/$p"
		fi
		cp "$PAKS/$p" "$APP/Contents/Resources/etmain/$p"
	done
fi

PLIST="$APP/Contents/Info.plist"
plutil -replace CFBundleIdentifier -string "ee.mikko.etlegacy" "$PLIST"
plutil -replace CFBundleShortVersionString -string "$VER" "$PLIST"
plutil -replace NSLocalNetworkUsageDescription -string "ET Legacy finds and joins games on your local network." "$PLIST"

xattr -cr "$APP"
codesign --force --deep -s - "$APP"
codesign --verify --deep --strict "$APP" && echo "signed (ad hoc)"

# DMG: the app and an /Applications link
DMG="dist/ET-Legacy-$VER.dmg"
DMG_STAGE=dist/dmg-stage
rm -rf "$DMG_STAGE" "$DMG" && mkdir -p "$DMG_STAGE"
cp -Rc "$APP" "$DMG_STAGE/ET Legacy.app"
ln -s /Applications "$DMG_STAGE/Applications"
hdiutil create -volname "ET Legacy" -srcfolder "$DMG_STAGE" -ov -format UDZO -imagekey zlib-level=6 "$DMG"
rm -rf "$DMG_STAGE" "$STAGE"
ls -la "$DMG"; du -sh "$APP"
