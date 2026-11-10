# OpenRGB 1.0 – Stable fork with profile, startup and Aura USB fixes, and a full CLI

A patched build of [OpenRGB](https://gitlab.com/CalcProgrammer1/OpenRGB) **1.0** for people who stayed on 1.0 (or went back to it). It fixes profiles and zone sizes that silently stop applying, makes OpenRGB work as a Linux service that starts before you log in, fixes several ASUS Aura USB bugs, and adds command-line options for everything the GUI can do.

> Not affiliated with the official OpenRGB project. All changes are on top of the `release_1.0` tag. The original upstream README is kept as [README-upstream.md](README-upstream.md).

> Made by [vdurzo](https://github.com/vdurzo) with the help of Claude (Anthropic).

> **Platform:** built and tested on **Linux only**. The debug log uses Linux/POSIX clock functions, so the Windows build will most likely not compile without changes.

## Contents
- [General fixes (all brands)](#general-fixes-all-brands)
- [Linux service started before login (all brands, Linux)](#linux-service-started-before-login-all-brands-linux)
- [ASUS Aura USB fixes](#asus-aura-usb-fixes)
- [Everything from the command line](#everything-from-the-command-line)
- [Always-on debug log](#always-on-debug-log)
- [Install](#install-arch--cachyos--arch-based)
- [Tested on](#tested-on)

## General fixes (all brands)

These apply to any device, from any vendor.

### Profiles and saved zone sizes
OpenRGB finds a device's saved data (profile colors, manual zone sizes) by comparing its name, description, version, serial number and location. If any of these changed, the saved data was silently ignored.

- **Firmware/version changes:** saved data still applies when only the version string differs, as long as exactly one saved entry matches.
- **Serial number changes:** saved data still applies when only the serial number differs, as long as exactly one saved entry matches. Some devices report a serial with random trailing bytes that change on every read (for example the ASUS ROG Azoth reports `R5MPGDD23637a`, `R5MPGDD23637w`, …).
- **Modes restored by name:** profile modes were matched by their position in the list, so a profile stopped applying its mode after an update added or reordered modes. They are now matched by name.
- **No stale duplicates:** saving the configuration replaces old entries for the same device instead of piling them up.

### Clear feedback when a profile does not apply
- Loading a profile now logs each device it was **not** applied to.
- `--profile` reports a failure when the profile matched no device (before, it always said "Profile loaded successfully").

### Other fixes
- **Settings crash:** changing a setting option that does not exist aborted the program (`update()` with null). Fixed in `SettingsManager`.
- **`--size` false error:** resizing a zone printed "neither mode nor color given". Fixed.
- **Action-only commands** (profiles, settings, rescan, `--save-profile`) no longer re-send the current state to every device and no longer clear the active profile.

## Linux service started before login (all brands, Linux)

On Linux, USB (HID) devices are only accessible to the user with an active local session (udev `uaccess`). When OpenRGB runs as a system service under a normal user (`User=` in the unit), it starts **before** that user logs in. Every USB device is denied, silently skipped, and the startup profile only reaches I2C devices such as RAM and the GPU. Colors only work after pressing **Rescan**.

This fork remembers every device that returned *Permission denied* and checks them every 5 seconds. As soon as access is granted (you log in), it waits 3 seconds for the remaining devices, rescans, and re-applies the active profile automatically. No udev changes are needed.

## ASUS Aura USB fixes

These only affect ASUS Aura USB controllers: motherboards and addressable (ARGB) controllers that use the Aura USB protocol.

- **Use-after-free:** when reading the config table failed, the controller deleted itself (`delete this`) inside its constructor while the detector kept using it.
- **Detection hangs:** requests used a blocking read with no timeout. They now use a 100 ms timeout with retries.
- **Crossed replies:** stale reports are drained before each request, and only the reply that matches the request is accepted. This matters when another program (or a second OpenRGB) talks to the controller.
- **Garbage firmware version:** the version buffer was never initialized, so a failed read produced a different random string on every boot, and saved zone sizes and profiles never matched again.
- **Clean skip:** if the config table cannot be read, the device is skipped instead of being registered with an empty layout.

## Everything from the command line

Functions that were only available in the GUI can now be done with options. When the OpenRGB service is running, these go through it, the same way the GUI does.

| Option | What it does |
|---|---|
| `-lx`, `--list-extended` | Devices with each mode's capabilities, zone LED ranges, sizes and segments |
| `--direction left\|right\|up\|down\|horizontal\|vertical\|up-left\|up-right\|down-left\|down-right` | Sets the direction of the selected mode |
| `--led 0,3,5-9` | Colors only those LEDs with `--color` (numbers relative to `--zone`); the rest keep their color |
| `--save-to-device` | Saves the applied mode to the device's memory (GUI "Save to Device") |
| `--rename-device "name"` | Custom device name (`""` restores the original) |
| `--rename-zone "name"` | Custom zone name (`""` restores the original) |
| `--segments "Fan 1:21,Fan 2:21"` | Replaces the segments of a zone (`""` clears them) |
| `--rescan` | Rescans devices (in the running service, if connected) |
| `--delete-profile name` | Deletes a profile |
| `--rename-profile old new` | Renames a profile; automatic profiles follow the new name |
| `--list-auto-profiles` | Shows the profile loaded on each event |
| `--auto-profile event name\|off` | Profile loaded on `service-startup`, `service-shutdown`, `open`, `exit`, `suspend` or `resume` |
| `--list-detectors [filter]` | Device detectors and whether they are enabled (GUI "Supported Devices") |
| `--enable-detector name` / `--disable-detector name` | Exact name or a unique part of it |
| `--list-settings` | Every settings section and option, with descriptions |
| `--get-setting section[.option]` | Current value of a section or option |
| `--set-setting section.option value` | Changes a setting; value is JSON (`true`, `5`, `"text"`) or plain text |

Run `openrgb --help` for the full list.

Examples:

```bash
# See device and zone numbers
openrgb -lx

# Red on LEDs 0-9 of zone 1 of device 3, keep the rest
openrgb -d 3 -z 1 --led 0-9 -c FF0000

# Name an ARGB header and split it into three fans
openrgb -d 3 -z 1 --rename-zone "Front fans"
openrgb -d 3 -z 1 --segments "Fan 1:21,Fan 2:21,Fan 3:21"

# Store static red in the motherboard itself (Direct mode cannot be saved)
openrgb -d 3 -m static -c FF0000 --save-to-device

# Load "Red" automatically when the service starts
openrgb --auto-profile service-startup Red

# Disable a detector and rescan
openrgb --disable-detector "ASUS ROG Azoth USB" && openrgb --rescan
```

## Always-on debug log

Writes `~/.config/OpenRGB/OpenRGB-debug.log` regardless of log level, so a problem at boot can be diagnosed after the fact:

- process start: user, groups, login session state
- permissions of every `/dev/hidraw*` node, at startup and at every detection
- every HID device seen, each detector run, and the exact reason an open failed
- device registration with zone sizes before and after the saved configuration
- profile loading: which saved entry matched each device, or why none did
- requests from the GUI or CLI to the service, including rescans
- the permission watcher (`[PERMWATCH]`)
- for ASUS Aura USB: every packet sent and received

The file rotates to `.old` at 10 MB.

## Install (Arch / CachyOS / Arch-based)

This builds a package named `openrgb` (1.0-3) that replaces the distro package. The udev rules, systemd service and module files are copied from your current `openrgb` package, so it must already be installed.

```bash
mkdir -pv ~/build/openrgb-fix && cd ~/build/openrgb-fix && \
cp -v /usr/lib/udev/rules.d/60-openrgb.rules . && \
cp -v /usr/lib/modules-load.d/openrgb.conf openrgb-modules.conf && \
cp -v /usr/lib/systemd/system/openrgb.service . && \
cp -v /usr/lib/tmpfiles.d/openrgb.conf openrgb-tmpfiles.conf && \
cat > PKGBUILD <<'PKG'
pkgname=openrgb
pkgver=1.0
pkgrel=3
pkgdesc="OpenRGB 1.0 with profile, startup and Aura USB fixes, and a full CLI"
arch=(x86_64)
url="https://github.com/vdurzo/OpenRGB-Stable-CLI"
license=(GPL-2.0-only)
depends=(glibc libgcc libstdc++ qt6-base libusb hidapi mbedtls3 hicolor-icon-theme)
makedepends=(git qt6-tools)
source=("openrgb-src::git+https://github.com/vdurzo/OpenRGB-Stable-CLI.git#branch=fix/aura-profiles-1.0"
        60-openrgb.rules openrgb-modules.conf openrgb.service openrgb-tmpfiles.conf)
sha256sums=(SKIP SKIP SKIP SKIP SKIP)

build() {
  mkdir -p build && cd build
  qmake6 ../openrgb-src/OpenRGB.pro PREFIX=/usr
  make -j"$(nproc)"
}

package() {
  cd build
  make INSTALL_ROOT="$pkgdir" install
  install -Dm644 "$srcdir/60-openrgb.rules"      "$pkgdir/usr/lib/udev/rules.d/60-openrgb.rules"
  install -Dm644 "$srcdir/openrgb-modules.conf"  "$pkgdir/usr/lib/modules-load.d/openrgb.conf"
  install -Dm644 "$srcdir/openrgb.service"       "$pkgdir/usr/lib/systemd/system/openrgb.service"
  install -Dm644 "$srcdir/openrgb-tmpfiles.conf" "$pkgdir/usr/lib/tmpfiles.d/openrgb.conf"
}
PKG
makepkg -si
```

To update later, run `makepkg -sif` again in the same folder.

To stop pacman from replacing it on the next system update, add `openrgb` to `IgnorePkg` in `/etc/pacman.conf`.

### Build from source (any Linux)

```bash
git clone -b fix/aura-profiles-1.0 https://github.com/vdurzo/OpenRGB-Stable-CLI.git
cd OpenRGB-Stable-CLI && mkdir build && cd build
qmake6 ../OpenRGB.pro PREFIX=/usr && make -j"$(nproc)"
```

Requires Qt 6, hidapi, libusb and mbedtls 3.x.

## Tested on
- CachyOS, KDE Plasma 6
- ASUS TUF GAMING X670E-PLUS WIFI (Aura USB `0b05:19af`, 3 ARGB headers)
- ASUS ROG Azoth keyboard (wired `0b05:1a83` and 2.4 GHz `0b05:1a85`), Logitech G502, ENE DRAM, ASUS ROG Strix RTX 2080 Ti
- OpenRGB as a systemd service with `User=` set to a regular user and `--server --profile <name>`
- The CLI options were also tested with OpenRGB's debug devices, both standalone and through a running service

Other Aura USB devices use the same code paths, but I have not tested them.

## Credits
These fixes and the CLI work were done by [vdurzo](https://github.com/vdurzo) with the help of Claude (Anthropic), working from the debug logs and tests on real hardware.

## License
GPL-2.0-or-later, same as upstream OpenRGB.
