#!/usr/bin/env bash
# Install Neural DSP Cortex Control on Linux under Wine.
# Usage: ./install.sh ["/path/to/Cortex Control vX.Y.Z.exe"]
# Without an argument, the tested installer is downloaded from Neural DSP.
set -euo pipefail

REPO="0xteo/cortex-control-linux"
PREFIX="${CORTEX_PREFIX:-$HOME/.wine-cortex}"
HERE="$(cd "$(dirname "$0")" && pwd)"
DATA_DIR="${XDG_DATA_HOME:-$HOME/.local/share}"
BIN_DIR="$HOME/.local/bin"
TESTED_WINE_MAJOR=10
# The Cortex Control version this was tested with, from Neural DSP's public
# download page (https://neuraldsp.com/downloads).
CC_VERSION="4.1.0"
CC_URL="https://downloads.neuraldsp.com/file/cortex-control-installers/v_$CC_VERSION/windows/Cortex%20Control%20v$CC_VERSION.exe"
CC_SHA256="c03942a0631afd195d3d70febf91384bf39cb7101014146f26f5ba65fd73228c"
CACHE_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/cortex-control-linux"

warn() { printf '\033[1;33m!! %s\033[0m\n' "$*"; }
die()  { printf '\033[1;31mError: %s\033[0m\n' "$*" >&2; exit 1; }
step() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
confirm() {
  [ "${CORTEX_YES:-}" = 1 ] && return 0
  read -r -p "$1 [y/N] " ans
  [[ "$ans" =~ ^[Yy]$ ]]
}

wine_() { WINEPREFIX="$PREFIX" WINEDEBUG=-all wine "$@"; }
# Wine only reads system colours and theme at startup, so restart it after
# changing them. Ubuntu keeps wineserver out of PATH.
WINESERVER="$(command -v wineserver || true)"
for ws in /usr/lib/x86_64-linux-gnu/wine/wineserver /usr/lib/*/wine/wineserver /usr/lib/wine/wineserver; do
  if [ -z "$WINESERVER" ] && [ -x "$ws" ]; then WINESERVER="$ws"; fi
done
restart_wine() {
  [ -n "$WINESERVER" ] || return 0
  WINEPREFIX="$PREFIX" "$WINESERVER" -k 2>/dev/null || true
  WINEPREFIX="$PREFIX" "$WINESERVER" -w 2>/dev/null || true
}

INSTALLER=""
if [ $# -ge 1 ]; then
  [ -f "$1" ] || die "installer not found: $1"
  INSTALLER="$(realpath "$1")"
fi

cat <<'EOF'

  Cortex Control for Linux (unofficial, not affiliated with Neural DSP)

  USE AT YOUR OWN RISK. This software is provided "as is", without warranty
  of any kind. The authors take NO responsibility for damaged or unusable
  ("bricked") devices, lost presets or data, voided warranties, or any other
  harm resulting from its use. Neural DSP does not support running Cortex
  Control on Linux and may not help you with problems caused by it.

  !! Tested ONLY on Ubuntu 26.04 + Wine 10.0 with a Quad Cortex mini.
  !! DO NOT UPDATE FIRMWARE through Cortex Control under Wine. A failed
  !! update can leave your unit unusable. Update over Wi-Fi on the device.
  !! Back up your presets before using it.

EOF
if [ "${CORTEX_ACCEPT_RISK:-}" != 1 ]; then
  read -r -p 'Type "I AGREE" to accept these risks and continue: ' ans
  [ "$ans" = "I AGREE" ] || die "not accepted; nothing was installed"
fi

step "Checking requirements"
missing=()
for cmd in wine winetricks curl; do
  command -v "$cmd" >/dev/null || missing+=("$cmd")
done
if [ ${#missing[@]} -gt 0 ]; then
  if command -v apt-get >/dev/null; then
    echo "Missing: ${missing[*]}"
    confirm "Install them with 'sudo apt install ${missing[*]}'?" || die "install ${missing[*]} and run this again"
    sudo apt-get install -y "${missing[@]}"
  else
    die "missing: ${missing[*]}. Install them with your package manager and run this again"
  fi
fi
wine_version="$(wine --version)"
wine_major="$(sed -E 's/^wine-([0-9]+).*/\1/' <<<"$wine_version")"
echo "Found $wine_version"
[ "$wine_major" = "$TESTED_WINE_MAJOR" ] || warn "only tested with Wine $TESTED_WINE_MAJOR.x; continuing anyway"
# DXVK needs a Vulkan driver; most distros ship one with Mesa by default.
if ! compgen -G '/usr/share/vulkan/icd.d/*.json' >/dev/null && ! compgen -G '/etc/vulkan/icd.d/*.json' >/dev/null; then
  warn "no Vulkan driver found; the app window may stay black (Ubuntu: sudo apt install mesa-vulkan-drivers)"
fi

step "Getting the Cortex Control installer"
if [ -n "$INSTALLER" ]; then
  echo "Using $INSTALLER"
  [ "$(sha256sum "$INSTALLER" | cut -d' ' -f1)" = "$CC_SHA256" ] \
    || warn "this isn't the tested v$CC_VERSION installer; it may or may not work"
else
  INSTALLER="$CACHE_DIR/Cortex Control v$CC_VERSION.exe"
  if [ ! -f "$INSTALLER" ]; then
    echo "Downloading Cortex Control v$CC_VERSION from neuraldsp.com"
    mkdir -p "$CACHE_DIR"
    curl -fL --progress-bar -o "$INSTALLER.part" "$CC_URL" \
      || die "download failed; get the Windows installer from https://neuraldsp.com/downloads and pass its path"
    mv "$INSTALLER.part" "$INSTALLER"
  fi
  [ "$(sha256sum "$INSTALLER" | cut -d' ' -f1)" = "$CC_SHA256" ] \
    || { rm -f "$INSTALLER"; die "checksum mismatch on the downloaded installer"; }
  echo "Verified $INSTALLER"
fi

step "Getting the cfgmgr32 shim"
SHIM="$HERE/shim/cfgmgr32.dll"
if [ ! -f "$SHIM" ]; then
  if command -v x86_64-w64-mingw32-gcc >/dev/null; then
    make -C "$HERE/shim" cfgmgr32.dll
  else
    echo "mingw not found; downloading the prebuilt shim from the latest GitHub release"
    url="https://github.com/$REPO/releases/latest/download"
    curl -fsSL -o "$SHIM" "$url/cfgmgr32.dll"
    curl -fsSL -o "$SHIM.sha256" "$url/cfgmgr32.dll.sha256"
    (cd "$HERE/shim" && sha256sum -c cfgmgr32.dll.sha256) || die "checksum mismatch on downloaded shim"
  fi
fi

step "Checking USB access to the Quad Cortex"
hidraw=""
for h in /sys/class/hidraw/hidraw*; do
  grep -q "HID_NAME=Neural DSP" "$h/device/uevent" 2>/dev/null && hidraw="/dev/$(basename "$h")"
done
RULE=/etc/udev/rules.d/70-neural-dsp-quad-cortex.rules
if [ -f "$RULE" ]; then
  echo "udev rule already installed"
elif [ -n "$hidraw" ] && [ -r "$hidraw" ] && [ -w "$hidraw" ]; then
  echo "$hidraw is already accessible; no udev rule needed"
else
  echo "Installing a udev rule so Cortex Control can talk to the device (needs sudo):"
  sudo install -m 644 "$HERE/udev/70-neural-dsp-quad-cortex.rules" "$RULE"
  sudo udevadm control --reload
  sudo udevadm trigger --subsystem-match=hidraw
fi
[ -n "$hidraw" ] || warn "no Quad Cortex detected right now; plug it in before starting Cortex Control"

step "Creating Wine prefix at $PREFIX"
wine_ wineboot -i >/dev/null 2>&1
wine_ winecfg -v win10 >/dev/null 2>&1
# Use the classic (unthemed) look: with Wine's default "light" theme the setup
# wizard ignores the colours set below and its text is unreadable.
wine_ reg add 'HKCU\Software\Microsoft\Windows\CurrentVersion\ThemeManager' \
  /v ThemeActive /d 0 /f >/dev/null

# The setup wizard shows Neural DSP's license agreement. CORTEX_ACCEPT_EULA=1
# installs silently for automated setups, i.e. accepts it without seeing it.
if [ "${CORTEX_ACCEPT_EULA:-}" = 1 ]; then
  step "Installing Cortex Control silently (CORTEX_ACCEPT_EULA=1)"
  wine_ "$INSTALLER" /exenoui /qn >/dev/null 2>&1 || true
else
  step "Running the Cortex Control setup wizard"
  echo "Read and accept Neural DSP's license, then click through with the defaults."
  # The wizard is dark-themed with transparent labels, but Wine fills label
  # backgrounds with the system button colour (light grey), hiding the white
  # text. Use dark button colours while it runs, then restore the defaults.
  wizard_colors=("ButtonFace=0 0 0" "ButtonText=235 235 235" "ButtonHilight=90 90 90"
                 "ButtonLight=60 60 60" "ButtonShadow=40 40 40" "ButtonDkShadow=20 20 20"
                 "GrayText=130 130 130")
  for kv in "${wizard_colors[@]}"; do
    wine_ reg add 'HKCU\Control Panel\Colors' /v "${kv%%=*}" /d "${kv#*=}" /f >/dev/null
  done
  restart_wine
  # With dark button colours the Cancel confirmation (black text on a white
  # image) would be unreadable; this transform makes it match the wizard.
  # See transform/make-mst.c.
  mst="$(WINEPREFIX="$PREFIX" WINEDEBUG=-all winepath -w "$HERE/transform/cancel-dark.mst")"
  wine_ "$INSTALLER" TRANSFORMS="$mst" >/dev/null 2>&1 || true
  for kv in "${wizard_colors[@]}"; do
    wine_ reg delete 'HKCU\Control Panel\Colors' /v "${kv%%=*}" /f >/dev/null 2>&1 || true
  done
fi
exe=""
for _ in $(seq 1 60); do
  exe="$(find "$PREFIX/drive_c" -path '*Neural DSP*' -name 'Cortex Control.exe' 2>/dev/null | head -n1)"
  [ -n "$exe" ] && break
  sleep 2
done
[ -n "$exe" ] || die "Cortex Control.exe not found after install; did the installer finish?"
app_dir="$(dirname "$exe")"
echo "Installed to $app_dir"

step "Installing DXVK into the prefix (fixes the black app window)"
WINEPREFIX="$PREFIX" WINEDEBUG=-all winetricks -q dxvk >/dev/null 2>&1 || die "winetricks dxvk failed"

step "Installing the cfgmgr32 shim (fixes device detection)"
install -m 755 "$SHIM" "$app_dir/cfgmgr32.dll"
wine_ reg add 'HKCU\Software\Wine\AppDefaults\Cortex Control.exe\DllOverrides' \
  /v cfgmgr32 /d native,builtin /f >/dev/null
# The default X11 driver gives the window the desktop's own title bar; Wine's
# Wayland driver draws a Windows-style frame and breaks resizing here.
wine_ reg delete 'HKCU\Software\Wine\Drivers' /v Graphics /f >/dev/null 2>&1 || true

step "Creating launcher, app entry and icon"
mkdir -p "$BIN_DIR" "$DATA_DIR/applications"
cat > "$BIN_DIR/cortex-control" <<EOF
#!/bin/sh
# Generated by cortex-control-linux (https://github.com/$REPO)
cd "$app_dir" || exit 1
WINEPREFIX="$PREFIX" WINEDEBUG=-all exec wine "Cortex Control.exe" "\$@"
EOF
chmod +x "$BIN_DIR/cortex-control"

# Reuse the icon Wine extracted from the app itself, renamed to a stable name.
for f in "$DATA_DIR"/icons/hicolor/*/apps/*_Cortex\ Control.0.png; do
  [ -f "$f" ] && cp "$f" "$(dirname "$f")/cortex-control.png"
done

cat > "$DATA_DIR/applications/cortex-control.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Cortex Control
GenericName=Quad Cortex Editor
Comment=Edit presets and settings on the Neural DSP Quad Cortex (via Wine)
Exec=$BIN_DIR/cortex-control
Icon=cortex-control
Terminal=false
StartupNotify=true
StartupWMClass=cortex control.exe
Categories=AudioVideo;Audio;Music;
Keywords=Neural;DSP;Quad;Cortex;guitar;preset;editor;
EOF

# Drop the duplicate entries Wine's installer integration created.
rm -rf "$DATA_DIR/applications/wine/Programs/Cortex Control"
rm -f "$DATA_DIR/desktop-directories/wine-Programs-Cortex Control.directory"
desktop_dir="$(xdg-user-dir DESKTOP 2>/dev/null || echo "$HOME/Desktop")"
rm -f "$desktop_dir/Cortex Control.desktop" "$desktop_dir/Cortex Control.lnk"
update-desktop-database "$DATA_DIR/applications" 2>/dev/null || true
gtk-update-icon-cache -f -t "$DATA_DIR/icons/hicolor" >/dev/null 2>&1 || true

step "Done"
cat <<EOF
Start "Cortex Control" from your app menu, or run: cortex-control

!! Reminder: DO NOT update firmware from here. Use Wi-Fi updates on the device.
EOF
