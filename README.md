# Cortex Control for Linux

Run Neural DSP's **Cortex Control** editor on Linux under Wine, connected
over USB to your Quad Cortex, with no Windows VM.

> [!WARNING]
> **Tested only on Ubuntu 26.04 with Wine 10.0 and a Quad Cortex mini.**
> Other distros, Wine versions and the full-size Quad Cortex may work, but
> nobody has checked yet. Reports are welcome.

> [!CAUTION]
> **Do not update firmware through Cortex Control under Wine.** If an update
> fails partway, the unit can be left unusable. Update firmware over **Wi-Fi
> on the device itself** instead.

## ⚠️ Disclaimer: use at your own risk

**By installing or using this project you accept full responsibility for
the outcome.** It is provided "as is", without warranty of any kind (see
[LICENSE](LICENSE)). The authors and contributors take **no responsibility**
for damaged or unusable ("bricked") devices, failed or interrupted firmware
updates, lost presets or other data, voided warranties, or any other damage
or loss resulting from its use.

- This is an unofficial community project. It is **not affiliated with,
  endorsed by or supported by Neural DSP**. Neural DSP does not support
  running Cortex Control on Linux and may decline to help with problems
  caused by it.
- Neural DSP's hardware warranty excludes damage from not following the
  Owner's Manual. Using unsupported software, and especially updating
  firmware with it, may put your warranty at risk.
- **Back up your presets** before using it.
- This project does not include or redistribute Cortex Control. The
  installer downloads the official Windows installer from Neural DSP's own
  servers, and you accept Neural DSP's license agreement in its setup
  wizard as usual.

The installer asks you to type `I AGREE` to these terms before it does
anything.

## What works

| | Status |
|---|---|
| App starts and renders | ✅ |
| Detects and connects to the Quad Cortex mini over USB | ✅ |
| Loading and editing presets | ✅ |
| Window resizing | ⚠️ works but jumpy |
| Firmware updates | ⛔ don't, see above |
| Full-size Quad Cortex | ❓ untested |

## Requirements

- **Ubuntu 26.04** (the only tested distro; others may work)
- **Wine 10** and **winetricks**: the installer offers to `apt install`
  them if they're missing
- A GPU with a **Vulkan** driver (standard Mesa drivers on Intel/AMD are
  fine; Ubuntu ships them by default)
- A **Quad Cortex mini**, connected over USB
- Optional: `mingw-w64` to build the shim DLL from source. Without it, the
  installer downloads the prebuilt DLL from this repo's latest
  [release](../../releases) and checks its SHA-256.

## Install

Plug in the Quad Cortex, then:

```sh
git clone https://github.com/0xteo/cortex-control-linux
cd cortex-control-linux
./install.sh
```

No git? Download `cortex-control-linux-<version>.tar.gz` from the
[latest release](../../releases/latest) (it includes the prebuilt shim),
extract it, and run `./install.sh` inside.

The script:

1. Installs missing prerequisites (`wine`, `winetricks`, `curl`) with
   `apt` after asking.
2. Downloads the official Cortex Control v4.1.0 Windows installer from
   Neural DSP's [download page](https://neuraldsp.com/downloads) and checks
   its SHA-256. This project doesn't host it.
3. Opens the official setup wizard in its own Wine prefix. Read and accept
   Neural DSP's license there, then click through with the defaults.
4. Adds DXVK, the cfgmgr32 shim, and a udev rule if your system needs one
   (this step asks for `sudo`).
5. Adds a **Cortex Control** entry to your app menu and a `cortex-control`
   command.

To use an installer you already downloaded, or a newer version (untested),
pass its path: `./install.sh ~/Downloads/"Cortex Control vX.Y.Z.exe"`.

Everything goes into its own Wine prefix (`~/.wine-cortex`, override with
`CORTEX_PREFIX=...`), so your other Wine apps are not affected.

## How it works

Out of the box, Cortex Control under Wine 10 has two problems:

1. **The window is black.** The UI is JUCE drawing through Direct2D/Direct3D
   11, which Wine's built-in D3D doesn't handle well. The installer adds
   [DXVK](https://github.com/doitsujin/dxvk) (D3D11 on Vulkan) to the prefix,
   which fixes it.

2. **The Quad Cortex is never detected.** The app finds devices with
   [hidapi](https://github.com/libusb/hidapi), which calls
   `CM_Get_Device_Interface_List_SizeW` from `cfgmgr32.dll` first. In Wine 10
   that function is a stub that always fails (as is
   `SetupDiOpenDeviceInterfaceW`, which is needed to get the device's
   instance ID), so hidapi sees zero devices.
   [`shim/cfgmgr32.c`](shim/cfgmgr32.c) is a small replacement
   `cfgmgr32.dll` that implements those calls on top of the `SetupDi*` APIs
   that Wine *does* implement, and forwards everything else to `setupapi`
   like Wine's own DLL does. It sits next to `Cortex Control.exe` and is
   enabled by a per-app DLL override, so nothing else in the prefix uses it.

The official setup wizard is also patched up for Wine, without modifying
Neural DSP's installer: Wine draws its "transparent" text labels on the
system button colour instead of the background image, which hides the
wizard's white text. The installer temporarily switches Wine to dark system
colours while the wizard runs, and applies a small Windows Installer
transform ([`transform/cancel-dark.mst`](transform/make-mst.c), passed as
`TRANSFORMS=`) so the Cancel confirmation matches the dark wizard too.

Wine passes the Quad Cortex's vendor HID interface through via `/dev/hidraw*`,
which needs read/write access for your user. Many distros don't grant that,
so the installer adds
[`udev/70-neural-dsp-quad-cortex.rules`](udev/70-neural-dsp-quad-cortex.rules),
which gives the logged-in user access to Neural DSP devices only.

## Troubleshooting

**Window opens partly off-screen.** Double-click the title bar to maximize it
(or resize it), then close the app. It remembers the size.

**Device not detected.** Give it a few seconds first: Wine brings USB
devices up shortly after it starts, and Cortex Control rescans every second.
If it still doesn't connect, check that your user can access the HID node:

```sh
for h in /sys/class/hidraw/*; do grep -q "Neural DSP" $h/device/uevent && ls -l /dev/$(basename $h); done
```

Then run the shim's self-test, which looks for the Quad Cortex the same way
Cortex Control does (needs `mingw-w64`):

```sh
make -C shim test-enum.exe
cp shim/test-enum.exe ~/.wine-cortex/drive_c/Program\ Files/Neural\ DSP/Cortex\ Control/
cd ~/.wine-cortex/drive_c/Program\ Files/Neural\ DSP/Cortex\ Control
WINEPREFIX=~/.wine-cortex WINEDLLOVERRIDES=cfgmgr32=n wine test-enum.exe
```

Or launch the app with the shim's logging on:

```sh
cd ~/.wine-cortex/drive_c/Program\ Files/Neural\ DSP/Cortex\ Control
WINEPREFIX=~/.wine-cortex CFGSHIM_LOG=1 wine "Cortex Control.exe" 2>&1 | grep cfgshim
```

You should see a `\\?\hid#vid_152a&pid_...` line for the Quad Cortex. The
repeated `hidraw_device_set_output_report ... Broken pipe` errors in Wine's
log are harmless: the device rejects one optional message and everything
else works.

**Don't switch Wine to its Wayland driver.** Under Wayland the window loses
your desktop's title bar, gets a Windows 95-style frame and may get stuck
fullscreen. The default X11 driver (XWayland) gives a normal window.

## Audio

The Quad Cortex mini is a standard USB audio interface (8 in / 8 out,
48 kHz), so audio doesn't need any of this: PipeWire picks it up directly.
Tips:

- Switch it to the **Pro Audio** profile (e.g. in `pavucontrol`) so the
  channels appear as plain `AUX0–7` instead of a fake 7.1 surround layout.
- For a DAW, install `pipewire-jack`. On Ubuntu you also need
  `sudo cp /usr/share/doc/pipewire/examples/ld.so.conf.d/pipewire-jack-*.conf /etc/ld.so.conf.d/ && sudo ldconfig`.
- Headphones with a built-in mic (TRRS plugs) sound hollow and phasey in the
  headphone jack. Use plain stereo headphones or a TRRS-to-TRS adapter.

## Uninstall

```sh
./uninstall.sh
```

## License

The scripts and shim are MIT licensed and come with no warranty (see [LICENSE](LICENSE)). Cortex
Control, Quad Cortex and Neural DSP are trademarks of Neural DSP
Technologies Oy, which is not involved with this project.
