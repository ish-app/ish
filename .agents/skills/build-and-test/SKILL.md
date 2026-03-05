# Build and Test iSH

## Overview
iSH is a Linux shell for iOS using usermode x86 emulation and syscall translation. It uses the Meson build system with Ninja.

## Prerequisites
The following packages must be installed:
- clang, lld
- libsqlite3-dev
- libarchive-dev
- pkg-config
- ninja-build
- python3 + meson (via pip3)

Install on Ubuntu:
```bash
sudo apt-get install -y clang lld libsqlite3-dev libarchive-dev pkg-config ninja-build python3-pip
pip3 install meson
```

## Git Submodules
This project has git submodules. After cloning, run:
```bash
git submodule update --init --recursive
```

## Building
```bash
# Configure (run from repo root)
meson setup build

# Build
ninja -C build
```

If you need to reconfigure (e.g. after changing options), remove the build dir first:
```bash
rm -rf build && meson setup build
```

## Running Tests
```bash
ninja -C build test
```

There are two test suites:
- `float80` - unit test for the floating point library (fast)
- `e2e` - end-to-end tests (takes ~2 minutes, uses `tests/e2e/e2e.bash`)

## Logging
To enable debug logging channels:
```bash
meson configure build -Dlog="strace verbose"
```

Available channels: `strace`, `instr`, `verbose`

## Running the emulator
After building, run programs with:
```bash
./build/ish -f <rootfs_dir> /bin/sh
```

To create a rootfs, download an Alpine minirootfs tarball for i386 and use:
```bash
./build/tools/fakefsify <tarball> <output_dir>
```
