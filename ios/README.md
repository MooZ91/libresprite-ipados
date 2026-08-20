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

None of the Homebrew libraries can be reused: they are macOS binaries. Build
static iOS ones into a prefix of their own. All of them are plain CMake builds
configured with `CMAKE_SYSTEM_NAME=iOS`, except:

- **libjpeg-turbo** needs `-DCMAKE_SYSTEM_PROCESSOR=arm64`; cross-compiling
  leaves that variable empty and its CMakeLists fails on it.
- **giflib** ships no CMake build; compile its library sources directly.
- **pixman** ships no CMake build either, and needs `-DHAVE_PTHREADS`. Only
  `pixman_region32_*` is used (see `src/gfx/region.cpp`), so building
  `pixman-region32.c` alone is enough, plus stubs for `_pixman_log_error` and
  the `pixman_image_get_*` accessors that `pixman_region32_init_from_image()`
  refers to but that LibreSprite never calls.
- **SDL2_image** must be built with `-DSDL2IMAGE_QOI=OFF`. It bundles its own
  copy of QOI, which collides with LibreSprite's when everything is linked
  statically under `-all_load`.
- **SDL2** must be patched first, see below.

zlib comes from the iOS SDK. libcurl, libarchive and V8 are unavailable on iOS
and are not needed: the sources that use them disable themselves.

## 3. Patch SDL2

    cd <sdl2-source> && patch -p1 < ios/patches/sdl2-ios-pencil.patch

Stock SDL cannot tell an Apple Pencil from a finger or a resting palm, and
reports raw `UITouch.force` where its own API documents a 0..1 pressure. The
patch gives the pencil a touch device of its own and normalises pressure.

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

## Known gaps

- No right mouse button, so the secondary colour is unreachable. Apple Pencil
  Pro's squeeze (`UIPencilInteraction`) is the natural place to put it.
- No keyboard modifiers, so the Alt/Shift/Ctrl tool variants are unreachable
  without a hardware keyboard.
- No palm rejection yet. The pencil can now be identified, but touches from a
  palm are still delivered and drawn.
- No hover. The hardware supports it on M2 and later iPads, but SDL's UIKit
  backend does not expose `UIHoverGestureRecognizer`.
- No app icon.
