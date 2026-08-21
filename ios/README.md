# Building LibreSprite for iPadOS

The build targets the iPad in landscape, at native pixel resolution. It is a
cross-compile, so it takes a few more steps than the desktop build.

## Requirements

- Xcode with the iOS SDK. If `xcode-select -p` points at the Command Line
  Tools, either switch it (`sudo xcode-select -s /Applications/Xcode.app`) or
  export `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer`.
- CMake and Ninja.

## 1. Build `gen` for the host

`gen` turns the XML under `data/` into C++ at build time, so it has to run on
the build machine rather than the target. Build one for the host:

    cmake -S . -B build/ios-host -G Ninja -DGEN_ONLY=on -DCMAKE_BUILD_TYPE=Release
    cmake --build build/ios-host

## 2. Build the dependencies for iOS

None of the Homebrew libraries can be reused: they are macOS binaries. The
script builds static iOS ones -- including a patched SDL2, see below -- into a
prefix of its own:

    ./ios/build-deps.sh simulator     # build/ios-deps/prefix-simulator
    ./ios/build-deps.sh device        # build/ios-deps/prefix-device

zlib comes from the iOS SDK. libcurl, libarchive and V8 are unavailable on iOS
and are not needed: the sources that use them disable themselves.

Most of these are ordinary CMake builds. The ones that are not, and the reason
in each case, are commented in the script: libjpeg-turbo needs
`CMAKE_SYSTEM_PROCESSOR` set by hand, giflib and pixman ship no CMake build,
only pixman's region32 API is used, and SDL2_image has to be built without its
bundled QOI so it does not collide with LibreSprite's.

## 3. The SDL2 patch

`ios/patches/sdl2-ios-pencil.patch` is applied by the script. Stock SDL cannot
tell an Apple Pencil from a finger or a resting palm, and reports raw
`UITouch.force` where its own API documents a 0..1 pressure. The patch gives
the pencil a touch device of its own and normalises pressure.

## 4. Build the app

    cmake -S . -B build/ios -G Ninja \
      -DCMAKE_SYSTEM_NAME=iOS \
      -DCMAKE_OSX_SYSROOT=iphonesimulator \
      -DCMAKE_OSX_ARCHITECTURES=arm64 \
      -DCMAKE_SYSTEM_PROCESSOR=arm64 \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=18.0 \
      -DCMAKE_PREFIX_PATH=<deps-prefix> \
      -DCMAKE_FIND_ROOT_PATH=<deps-prefix> \
      -DGEN_EXECUTABLE=$PWD/build/ios-host/bin/gen \
      -DCMAKE_BUILD_TYPE=Release
    cmake --build build/ios

For a device rather than the simulator, use `CMAKE_OSX_SYSROOT=iphoneos` and
build the dependencies against that sysroot too.

## Running

    xcrun simctl boot "iPad Pro 11-inch (M5)"
    xcrun simctl install booted build/ios/bin/libresprite.app
    xcrun simctl launch --console-pty booted org.libresprite.LibreSprite

The simulator emulates neither the Pencil, nor pressure, nor hover, so
anything touching those has to be checked on real hardware.

## Touch targets

`SkinTheme::touchMinSize()` is the one place that knows how small a control
may get before a finger stops hitting it: 44 UI units on iOS, where a UI unit
is a point, and zero everywhere else, so the desktop is untouched. It floors
the height of buttons, checkboxes, radios, combo boxes, menu items, list items
and sliders, plus the size of the window close button.

Only the height of the widgets is floored. Forcing the width as well pushes
the tool bar off the side of the screen.

The scrollbars are deliberately left alone. Widening them was tried and had
to be reverted: the skin draws them from a nine-slice cut for a bar a few
pixels wide, and stretching that to a fingertip turns it into bands and
stripes. Making them touchable needs either new art or a hit area kept
separate from what is drawn -- see the known gap below.

## Known gaps

- No right mouse button, so the secondary colour is unreachable. Apple Pencil
  Pro's squeeze (`UIPencilInteraction`) is the natural place to put it.
- No keyboard modifiers, so the Alt/Shift/Ctrl tool variants are unreachable
  without a hardware keyboard.
- No palm rejection yet. The pencil can now be identified, but touches from a
  palm are still delivered and drawn.
- No hover. The hardware supports it on M2 and later iPads, but SDL's UIKit
  backend does not expose `UIHoverGestureRecognizer`.
- The scrollbars are too thin to drag with a finger, and cannot simply be
  widened: the skin's nine-slice does not stretch that far.
