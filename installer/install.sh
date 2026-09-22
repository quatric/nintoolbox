#!/usr/bin/env bash
# Installs the CLI tools bundled next to this script onto your PATH. This
# script ships inside the GUI's own app/dist folder (built with PyInstaller's
# --contents-directory . so everything lands flat, right next to the main
# executable) -- there is no separate "CLI-only" copy of these binaries.
#
# Usage: ./install.sh [DEST_DIR]
#   DEST_DIR defaults to /usr/local/bin (needs sudo) if writable or run as
#   root; otherwise falls back to ~/.local/bin (created if missing).
#
# Copies every executable file next to this script (skipping this script,
# the GUI app/its support libraries, and the share/ directory) into DEST_DIR,
# and copies share/ (titles.txt etc.) to DEST_DIR/../share/nintoolbox if
# present.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

DEST="${1:-}"
if [[ -z "$DEST" ]]; then
	if [[ -w /usr/local/bin || $(id -u) = 0 ]]; then
		DEST=/usr/local/bin
	else
		DEST="$HOME/.local/bin"
	fi
fi

mkdir -p "$DEST"

echo "Installing to $DEST"
installed=0
for f in "$HERE"/*; do
	name="$(basename "$f")"
	case "$name" in
		install.sh|nintoolbox|*.app|share) continue ;;
		# PyInstaller's own support libraries land flat alongside the tools
		# under --contents-directory . -- never CLI tools, always skip.
		*.so|*.so.*|*.dylib|*.dll) continue ;;
	esac
	[[ -f "$f" && -x "$f" ]] || continue
	cp -p "$f" "$DEST/$name"
	echo "  $name"
	installed=$((installed + 1))
done

if (( installed == 0 )); then
	echo "No executables found next to this script -- nothing installed." >&2
	exit 1
fi

# Bundled data files resolved next to the running tool by lib-passthru.c:
# seeddb.bin for ctrtool's 3DS seed-crypto RomFS, prod.keys/title.keys for the
# Switch tools, keys.txt/wiiu_keys for Wii U disc extraction (see resolve_bundled_tool(),
# locate_switch_key(), locate_wiiu_resource()). They are non-executable so the
# loop above skips them -- copy them explicitly so the installed CLI tools keep
# working out of the box.
for data in seeddb.bin prod.keys title.keys keys.txt ffdec.jar; do
	if [[ -f "$HERE/$data" ]]; then
		cp -p "$HERE/$data" "$DEST/$data"
		echo "  $data"
	fi
done

if [[ -d "$HERE/lib" ]]; then
	mkdir -p "$DEST/lib"
	cp -Rp "$HERE/lib/." "$DEST/lib/"
	echo "  lib/ (bundled Java libraries)"
fi

if [[ -d "$HERE/wiiu_keys" ]]; then
	mkdir -p "$DEST/wiiu_keys"
	cp -Rp "$HERE/wiiu_keys/." "$DEST/wiiu_keys/"
	echo "  wiiu_keys/ (Wii U retail disc keys)"
fi

if [[ -d "$HERE/share" ]]; then
	SHARE_DEST="$DEST/../share/nintoolbox"
	mkdir -p "$SHARE_DEST"
	cp -Rp "$HERE/share/." "$SHARE_DEST/"
	echo "Installed shared data (titles.txt etc.) to $SHARE_DEST"
fi

case ":$PATH:" in
	*":$DEST:"*) ;;
	*)
		echo
		echo "Note: $DEST is not on your PATH."
		echo "Add this to your shell profile (~/.bashrc, ~/.zshrc, etc.):"
		echo "  export PATH=\"$DEST:\$PATH\""
		;;
esac

echo
echo "Done. Try: wszst --version"
