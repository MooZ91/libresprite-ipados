# Building LibreSprite for iPadOS

The build targets the iPad in landscape, at native pixel resolution. It is a
cross-compile, so it takes a few more steps than the desktop build.

## Requirements

- Xcode with the iOS SDK. If `xcode-select -p` points at the Command Line
  Tools, either switch it (`sudo xcode-select -s /Applications/Xcode.app`) or
  export `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer`.
- CMake and Ninja.
- To install on a real iPad rather than the simulator: an Apple ID signed
  into Xcode (Settings ▸ Accounts) with a development certificate — a free
  personal team is enough — and the iPad paired to the Mac at least once
  (Xcode ▸ Window ▸ Devices and Simulators, or just plug it in and tap
  "Trust" on the iPad).

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
build the dependencies against that sysroot too -- but see the next section
first: a device build needs the Xcode generator, not Ninja.

## Running in the simulator

    xcrun simctl boot "iPad Pro 11-inch (M5)"
    xcrun simctl install booted build/ios/bin/libresprite.app
    xcrun simctl launch --console-pty booted org.libresprite.LibreSprite

The simulator emulates neither the Pencil, nor pressure, nor hover, so
anything touching those has to be checked on real hardware. It also needs no
code signing, which is why the Ninja build above is enough for it. To check
those on real hardware, see the next section.

## Building and installing on a real iPad

A device build has two extra requirements the simulator build doesn't: the
app has to be code-signed, and it needs an app icon, which only Xcode's own
`actool` build phase produces (a Ninja build succeeds but ships with no
icon). Both mean configuring with `-G Xcode` instead of `-G Ninja`, so CMake
emits an `.xcodeproj` for `xcodebuild` to sign and build.

Signing with a personal (free) Apple ID team also means the bundle
identifier can't be the upstream `org.libresprite.LibreSprite` -- that
belongs to someone else's team. Pick your own reverse-DNS identifier, e.g.
`com.<you>.LibreSprite`, and pass it as `LIBRESPRITE_BUNDLE_IDENTIFIER`.

### 1. Build the dependencies for the device

    ./ios/build-deps.sh device      # build/ios-deps/prefix-device

### 2. Configure and build with Xcode's generator

Find your team ID with `security find-identity -v -p codesigning` (it's the
value after the name, in parentheses is the *certificate's* ID -- the team
ID is the `OU=` field if you check the cert itself, or just open Xcode ▸
Settings ▸ Accounts ▸ your Apple ID ▸ your team and read it off there).

    cmake -S . -B build/ios-xcode -G Xcode \
      -DCMAKE_SYSTEM_NAME=iOS \
      -DCMAKE_OSX_SYSROOT=iphoneos \
      -DCMAKE_OSX_ARCHITECTURES=arm64 \
      -DCMAKE_SYSTEM_PROCESSOR=arm64 \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=18.0 \
      -DCMAKE_PREFIX_PATH=$PWD/build/ios-deps/prefix-device \
      -DCMAKE_FIND_ROOT_PATH=$PWD/build/ios-deps/prefix-device \
      -DGEN_EXECUTABLE=$PWD/build/ios-host/bin/gen \
      -DCMAKE_BUILD_TYPE=Release \
      -DLIBRESPRITE_BUNDLE_IDENTIFIER=com.<you>.LibreSprite \
      -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGN_STYLE=Automatic \
      -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM=<your-team-id>

    xcodebuild -project build/ios-xcode/libresprite.xcodeproj \
      -scheme libresprite -configuration Release \
      -destination 'generic/platform=iOS' \
      -allowProvisioningUpdates build

This produces a signed `build/ios-xcode/bin/libresprite.app`. Check
`codesign -dv build/ios-xcode/bin/libresprite.app` if anything looks off --
it should report your `TeamIdentifier` and `Identifier=` your chosen bundle
id, not the upstream one.

### 3. Install and launch over `devicectl`

List paired devices to get the target's identifier:

    xcrun devicectl list devices

Then install and launch by that identifier:

    xcrun devicectl device install app --device <device-id> \
      build/ios-xcode/bin/libresprite.app
    xcrun devicectl device process launch --device <device-id> \
      com.<you>.LibreSprite

Xcode's own Product ▸ Run works too, and is usually easier while iterating:
open `build/ios-xcode/libresprite.xcodeproj`, pick the `libresprite` scheme
and the iPad as the run destination, and hit Run. `xcodebuild`/`devicectl`
is the scriptable path once signing is set up.

A build here is landscape-only and targets iOS 18.0+ (the floor the Pencil
Pro APIs need); nothing in these steps assumes a particular iPad model
beyond that.

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
- Apple Pencil hover arrives, but by accident rather than by design: UIKit
  routes it through the same pointer interaction as a trackpad, so SDL
  reports it as motion of its *real* mouse (`SDL_MouseMotionEvent::which ==
  0`) rather than of the touch-emulated one (`SDL_TOUCH_MOUSEID`). It moves
  the brush preview, which is useful, but the two share one cursor position
  and UIKit fires a hover motion as the pencil lifts that still carries the
  point the stroke began at. `she.cpp` therefore ignores hover for as long as
  the screen is being touched, and takes the button events' position from the
  touch instead of from the shared cursor. Proper hover -- tip proximity,
  `UIHoverGestureRecognizer`, tilt -- is still unimplemented.
- The scrollbars are too thin to drag with a finger, and cannot simply be
  widened: the skin's nine-slice does not stretch that far.
