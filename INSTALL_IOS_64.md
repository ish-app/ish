# Putting the 64-bit build on an iPhone or iPad

Status (2026-11-13): the app builds for iOS arm64 with the 64-bit guest and, built for the
simulator, boots to a shell in the iPhone simulator (`uname -m` prints `x86_64`). It has not
run on a real device yet. That last step needs an Apple ID in Xcode, which only the owner of
the device can provide.

## What is needed
- A Mac with Xcode (tested with Xcode 26.5) and, from Homebrew: `meson ninja llvm lld`.
- This branch, with submodules: `git submodule update --init deps/libarchive deps/libapps`.
- An Apple ID signed in under Xcode > Settings > Accounts. A free account works; apps signed
  with it stop launching after 7 days and are rebuilt and reinstalled the same way.

## Steps (Xcode)
1. Open `iSH.xcodeproj`.
2. In `app/iSH.xcconfig` set
   - `ISH_GUEST = x86_64`
   - `ROOT_BUNDLE_IDENTIFIER` to an identifier of your own (for example `com.yourname.ish64`);
     `app.ish.iSH` belongs to the iSH developers and cannot be signed by anyone else.
3. Select the `iSH` target > Signing & Capabilities, tick "Automatically manage signing" and
   choose your team. Do the same for the `iSHFileProvider` target.
   With a free account Xcode may refuse the File Provider extension or the app group; if so,
   remove the `iSHFileProvider` target from the `iSH` scheme's build (the terminal does not
   need it; only the Files-app integration goes away).
4. Connect the device, trust the computer, enable Developer Mode on the device
   (Settings > Privacy & Security > Developer Mode), choose the device as the run destination
   and press Run.
5. On first launch iOS asks to trust the developer: Settings > General > VPN & Device
   Management.

The first launch unpacks an Alpine Linux x86_64 root (downloaded at build time from
dl-cdn.alpinelinux.org, see `ROOTFS_URL_x86_64`). Packages come from Alpine's normal
repositories over the network: `apk update && apk add python3`.

## Command line (no device, checks that it compiles)
    xcodebuild -project iSH.xcodeproj -scheme iSH -configuration Release -sdk iphoneos \
        ARCHS=arm64 ISH_GUEST=x86_64 CODE_SIGNING_ALLOWED=NO build

## Simulator
    xcodebuild -project iSH.xcodeproj -scheme iSH -configuration Release -sdk iphonesimulator \
        -derivedDataPath build-sim ISH_GUEST=x86_64 CODE_SIGN_IDENTITY=- CODE_SIGNING_REQUIRED=NO build
    xcrun simctl boot "iPhone 17 Pro"
    xcrun simctl install booted build-sim/Build/Products/Release-iphonesimulator/iSH.app
    xcrun simctl launch booted app.ish.iSH
(The simulator build must be signed "to run locally" as above; without it the app has no
app-group entitlement and exits at launch.)

## What to check first on a real device
- The shell starts and `uname -m` prints `x86_64`.
- `tests/guest64` programs: copy `torture.c` in, `apk add gcc musl-dev`, build and compare
  with the expected output in `tests/guest64` (the same binaries were compared with native
  runs on x86_64 Linux, on macOS, and on arm64 Linux under emulation).
- Speed: `time sh -c 'i=0; while [ $i -lt 200000 ]; do i=$((i+1)); done'`
  (22 s on a Ryzen 9 3900X; an A-series phone chip has never been measured).

## Claude Code inside the 64-bit build
    apk add nodejs npm libstdc++ libgcc
    npm install -g @anthropic-ai/claude-code
    claude
(npm warns that it wants Node 22; ignore it, the package brings its own runtime.) Sign in with
`/login`: it prints a link to open in a browser and takes the code pasted back. Checked on
Linux, macOS and emulated arm64 hosts up to the "Not logged in" answer; first start takes
10-20 s on a desktop CPU and has not been timed on a phone.
Claude Code's shell tool needs bash: `apk add bash` and `export SHELL=/bin/bash` before `claude`.
ssh works as on any Alpine system: `apk add openssh-client` (and `openssh-server` to log in to the
phone).
