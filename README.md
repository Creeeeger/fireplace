# Fireplace

Fireplace is an experimental Exynos 9830 emulator for the Samsung Galaxy
S20+ 5G (SM-G986B). It models enough of the SoC and Samsung boot flow to run
from BootROM through BL1/BL2, EL3, LK, and into the Android kernel handoff.

The emulator uses raw UFS LUN images from a device. Its bootchain support
profile is fixed to the bundled `bootchain/G986B` directory.

## Requirements

- CMake and pkg-config
- Unicorn
- Capstone
- OpenSSL/libcrypto
- libjpeg-turbo
- SDL2 and OpenGL

## Build

On Ubuntu:

```sh
sudo apt update
sudo apt install build-essential cmake pkg-config \
    libunicorn-dev libcapstone-dev libssl-dev libturbojpeg0-dev \
    libsdl2-dev libgl-dev

cmake -S . -B build
cmake --build build -j "$(nproc)"
```

On Apple Silicon with Homebrew dependencies:

```sh
PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig:$PKG_CONFIG_PATH" \
cmake -S . -B build \
    -DCMAKE_PREFIX_PATH=/opt/homebrew \
    -DCMAKE_LIBRARY_PATH=/opt/homebrew/lib

LIBRARY_PATH=/opt/homebrew/lib cmake --build build -j
```

For dependencies installed in the system search path, a regular
`cmake -S . -B build && cmake --build build -j` is sufficient.

## Run

Place `lun0.img` through `lun4.img` in one directory, then start either the
GUI or the headless emulator:

```sh
# GUI
./build/emulator/core/fireplace --lun-dir .

# Headless
./build/emulator/core/fireplace --headless --lun-dir .

# Headless with a numbered kernel instruction trace
./build/emulator/core/fireplace --headless --trace-kernel --lun-dir .
```

Secure OS / EL3 diagnostics are hidden by default. Add `--secure-os-logs`
to show them in the console.

Android is the default boot mode. Recovery and download modes can be selected
with `--boot-mode recovery` or `--boot-mode download`. Run with `--help` for
the complete command-line summary. Headless execution runs without an
artificial timeout; use Ctrl-C to stop it.

## Dumping the UFS LUNs

Identify the device-to-LUN mapping on the phone first, because block-device
letters are not guaranteed to match this example:

```sh
for block in /sys/block/sd*; do
    device=${block##*/}
    scsi=$(basename "$(readlink -f "$block/device")")
    lun=${scsi##*:}
    sectors=$(cat "$block/size")
    echo "$device -> LUN$lun sectors=$sectors"
done
```

With the usual SM-G986B mapping, run these commands on the host with root
ADB access (for example, in recovery). `adb pull` shows transfer progress;
`-Z` disables compression:

```sh
adb pull -Z /dev/block/sda lun0.img
adb pull -Z /dev/block/sdb lun1.img
adb pull -Z /dev/block/sdc lun2.img
adb pull -Z /dev/block/sdd lun3.img
adb pull -Z /dev/block/sde lun4.img
```

Expected image sizes:

```text
lun0.img  127934660608
lun1.img       4194304
lun2.img       4194304
lun3.img       8388608
lun4.img      16777216
```

Only use dumps obtained from hardware you are authorized to access. Fireplace
is distributed under the [GNU General Public License, version 2](LICENSE).
