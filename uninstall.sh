#!/usr/bin/env bash
# Remove everything install.sh created.
set -euo pipefail

PREFIX="${CORTEX_PREFIX:-$HOME/.wine-cortex}"
DATA_DIR="${XDG_DATA_HOME:-$HOME/.local/share}"
RULE=/etc/udev/rules.d/70-neural-dsp-quad-cortex.rules

echo "This removes the Wine prefix $PREFIX (Cortex Control and its local data),"
echo "the launcher, the app entry and the icons."
read -r -p "Continue? [y/N] " ans
[[ "$ans" =~ ^[Yy]$ ]] || exit 1

pkill -f 'Cortex Control.exe' 2>/dev/null || true
rm -rf "$PREFIX"
rm -f "$HOME/.local/bin/cortex-control" "$DATA_DIR/applications/cortex-control.desktop"
rm -rf "$DATA_DIR/applications/wine/Programs/Cortex Control" "$DATA_DIR/desktop-directories/wine-Programs-Cortex Control.directory"
rm -f "$DATA_DIR"/icons/hicolor/*/apps/cortex-control.png "$DATA_DIR"/icons/hicolor/*/apps/*_Cortex\ Control.0.png
update-desktop-database "$DATA_DIR/applications" 2>/dev/null || true

if [ -f "$RULE" ]; then
  echo "Removing the udev rule (needs sudo):"
  sudo rm -f "$RULE" && sudo udevadm control --reload
fi
echo "Cortex Control removed."
