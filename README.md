# Fireplace

Fireplace is an experimental Exynos 9830 emulator for the Samsung Galaxy
S20+ 5G (SM-G986B). It models enough of the SoC and Samsung boot flow to run
from BootROM through BL1/BL2, EL3, LK, and into the Android kernel handoff.

The emulator uses raw UFS LUN images from a device. Its bootchain support
profile is fixed to the bundled `bootchain/G986B` directory.

## Requirements

- CMake and pkg-config
- Unicorn
- OpenSSL/libcrypto
- libjpeg-turbo
- SDL2 and OpenGL

## Build

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
```

Android is the default boot mode. Recovery and download modes can be selected
with `--boot-mode recovery` or `--boot-mode download`. Run with `--help` for
the complete command-line summary. Headless execution runs without an
artificial timeout; use Ctrl-C to stop it.

## Dumping the UFS LUNs

Identify the device-to-LUN mapping first, because Linux block-device letters
are not guaranteed to match this example:

```sh
for block in /sys/block/sd*; do
    device=${block##*/}
    scsi=$(basename "$(readlink -f "$block/device")")
    lun=${scsi##*:}
    sectors=$(cat "$block/size")
    echo "$device -> LUN$lun sectors=$sectors bytes=$((sectors * 512))"
done
```

With the usual SM-G986B mapping, the images can be copied over ADB:

```sh
adb exec-out "dd if=/dev/block/sda bs=4M 2>/dev/null" > lun0.img
adb exec-out "dd if=/dev/block/sdb bs=4M 2>/dev/null" > lun1.img
adb exec-out "dd if=/dev/block/sdc bs=4M 2>/dev/null" > lun2.img
adb exec-out "dd if=/dev/block/sdd bs=4M 2>/dev/null" > lun3.img
adb exec-out "dd if=/dev/block/sde bs=4M 2>/dev/null" > lun4.img
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
