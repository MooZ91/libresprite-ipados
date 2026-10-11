#!/bin/bash
#
# Builds the static libraries LibreSprite needs on iOS into a prefix of its
# own. Homebrew's copies cannot be reused: they are macOS binaries.
#
#   ./ios/build-deps.sh [simulator|device] [prefix-dir]
#
# zlib comes from the iOS SDK. libcurl, libarchive and V8 are unavailable on
# iOS and are not needed -- the sources using them disable themselves.

set -euo pipefail

TARGET="${1:-simulator}"
case "$TARGET" in
  simulator) SYSROOT=iphonesimulator ;;
  device)    SYSROOT=iphoneos ;;
  *) echo "usage: $0 [simulator|device] [prefix-dir]" >&2; exit 1 ;;
esac

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PREFIX="${2:-$REPO/build/ios-deps/prefix-$TARGET}"
WORK="$REPO/build/ios-deps"
SRC="$WORK/src"
IOS_MIN=18.0

export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"

SDL2_VER=2.32.10
SDL2_IMAGE_VER=2.8.8
FREETYPE_VER=2.13.3
PNG_VER=1.6.44
JPEG_VER=3.1.3
GIF_VER=5.2.2
TINYXML2_VER=11.0.0
PIXMAN_VER=0.46.4

mkdir -p "$SRC" "$PREFIX/lib" "$PREFIX/include"
cd "$SRC"

fetch() { # url [filename]
  local url="$1" out="${2:-$(basename "$1")}"
  [ -f "$out" ] || curl -fsSL -o "$out" "$url"
}

unpack() { # tarball dir
  [ -d "$2" ] || tar xzf "$1"
}

ios_cmake() {
  cmake -G Ninja \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT="$SYSROOT" \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_SYSTEM_PROCESSOR=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$IOS_MIN" \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_PREFIX_PATH="$PREFIX" \
    -DCMAKE_FIND_ROOT_PATH="$PREFIX" \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_BUILD_TYPE=Release \
    "$@"
}

build() { # name source-dir [extra cmake args...]
  local name="$1" dir="$2"; shift 2
  echo "==> $name"
  ios_cmake -S "$dir" -B "$WORK/b-$name-$TARGET" "$@" >/dev/null
  cmake --build "$WORK/b-$name-$TARGET" --target install >/dev/null
}

echo "==> fetching sources"
fetch "https://github.com/libsdl-org/SDL/releases/download/release-$SDL2_VER/SDL2-$SDL2_VER.tar.gz"
fetch "https://github.com/libsdl-org/SDL_image/releases/download/release-$SDL2_IMAGE_VER/SDL2_image-$SDL2_IMAGE_VER.tar.gz"
fetch "https://downloads.sourceforge.net/project/freetype/freetype2/$FREETYPE_VER/freetype-$FREETYPE_VER.tar.gz"
fetch "https://downloads.sourceforge.net/project/libpng/libpng16/$PNG_VER/libpng-$PNG_VER.tar.gz"
fetch "https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/$JPEG_VER/libjpeg-turbo-$JPEG_VER.tar.gz"
fetch "https://sourceforge.net/projects/giflib/files/giflib-$GIF_VER.tar.gz/download" "giflib-$GIF_VER.tar.gz"
fetch "https://github.com/leethomason/tinyxml2/archive/refs/tags/$TINYXML2_VER.tar.gz" "tinyxml2-$TINYXML2_VER.tar.gz"
# cairographics.org is frequently unreachable; use the project's own GitLab.
fetch "https://gitlab.freedesktop.org/pixman/pixman/-/archive/pixman-$PIXMAN_VER/pixman-pixman-$PIXMAN_VER.tar.gz" "pixman-$PIXMAN_VER.tar.gz"

for t in *.tar.gz; do tar xzf "$t"; done

# --- plain CMake builds -------------------------------------------------
build tinyxml2 "$SRC/tinyxml2-$TINYXML2_VER" -Dtinyxml2_BUILD_TESTING=OFF
build png      "$SRC/libpng-$PNG_VER" \
  -DPNG_SHARED=OFF -DPNG_STATIC=ON -DPNG_TESTS=OFF -DPNG_EXECUTABLES=OFF -DPNG_FRAMEWORK=OFF
# Cross-compiling leaves CMAKE_SYSTEM_PROCESSOR empty and libjpeg-turbo's
# CMakeLists calls string(TOLOWER) on it, so ios_cmake always sets it.
build jpeg     "$SRC/libjpeg-turbo-$JPEG_VER" \
  -DENABLE_SHARED=OFF -DENABLE_STATIC=ON -DWITH_TURBOJPEG=OFF
build freetype "$SRC/freetype-$FREETYPE_VER" \
  -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON -DFT_DISABLE_BZIP2=ON \
  -DFT_DISABLE_PNG=ON -DFT_DISABLE_ZLIB=ON

# --- giflib: no build system of its own ----------------------------------
echo "==> giflib"
G="$SRC/giflib-$GIF_VER"
mkdir -p "$WORK/b-giflib-$TARGET" && cd "$WORK/b-giflib-$TARGET"
SDKP="$(xcrun --sdk "$SYSROOT" --show-sdk-path)"
if [ "$TARGET" = simulator ]; then
  MINFLAG="-mios-simulator-version-min=$IOS_MIN"
else
  MINFLAG="-miphoneos-version-min=$IOS_MIN"
fi
for f in dgif_lib egif_lib gifalloc gif_err gif_font gif_hash openbsd-reallocarray quantize; do
  xcrun --sdk "$SYSROOT" clang -arch arm64 -isysroot "$SDKP" $MINFLAG -O2 -I"$G" -c "$G/$f.c" -o "$f.o"
done
xcrun --sdk "$SYSROOT" ar rcs "$PREFIX/lib/libgif.a" ./*.o
cp "$G/gif_lib.h" "$PREFIX/include/"

# --- pixman: only the region32 API is used -------------------------------
# src/gfx/region.cpp uses nothing but pixman_region32_*, so pixman-region32.c
# alone is enough. It still refers to _pixman_log_error, which lives in
# pixman-utils.c and would drag in the whole implementation core, and to the
# pixman_image_get_* accessors used only by pixman_region32_init_from_image(),
# which LibreSprite never calls. Both are supplied as a small shim.
echo "==> pixman"
P="$SRC/pixman-pixman-$PIXMAN_VER/pixman"
mkdir -p "$WORK/b-pixman-$TARGET" && cd "$WORK/b-pixman-$TARGET"
sed -e 's/@PIXMAN_VERSION_MAJOR@/0/' -e 's/@PIXMAN_VERSION_MINOR@/46/' \
    -e 's/@PIXMAN_VERSION_MICRO@/4/' "$P/pixman-version.h.in" > pixman-version.h
cat > pixman-region-shim.c <<'SHIM'
#include <stdio.h>
void _pixman_log_error (const char *function, const char *message)
{
    fprintf (stderr, "*** BUG ***\nIn %s: %s\n", function, message);
}
/* Unreachable: pixman_region32_init_from_image() is unused by LibreSprite. */
void *pixman_image_get_data   (void *image) { (void)image; return 0; }
int   pixman_image_get_height (void *image) { (void)image; return 0; }
int   pixman_image_get_stride (void *image) { (void)image; return 0; }
int   pixman_image_get_width  (void *image) { (void)image; return 0; }
SHIM
for f in "$P/pixman-region32.c" pixman-region-shim.c; do
  xcrun --sdk "$SYSROOT" clang -arch arm64 -isysroot "$SDKP" $MINFLAG -O2 \
    -DPACKAGE=pixman -DHAVE_PTHREADS -I"$P" -I. -c "$f" -o "$(basename "${f%.c}").o"
done
xcrun --sdk "$SYSROOT" ar rcs "$PREFIX/lib/libpixman-1.a" ./*.o
mkdir -p "$PREFIX/include/pixman-1"
cp "$P/pixman.h" pixman-version.h "$PREFIX/include/pixman-1/"

# --- SDL2, patched for Apple Pencil --------------------------------------
cd "$SRC/SDL2-$SDL2_VER"
if ! grep -q pencilTouchId src/video/uikit/SDL_uikitview.m; then
  echo "==> patching SDL2 for Apple Pencil"
  patch -p1 < "$REPO/ios/patches/sdl2-ios-pencil.patch"
fi
if ! grep -q SDLUIKitSceneDelegate src/video/uikit/SDL_uikitappdelegate.m; then
  echo "==> patching SDL2 for the UIScene lifecycle"
  patch -p1 < "$REPO/ios/patches/sdl2-ios-scene.patch"
fi
build sdl2 "$SRC/SDL2-$SDL2_VER" -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF

# SDL2_image bundles its own QOI, which collides with LibreSprite's once
# everything is linked statically under -all_load.
build sdl2image "$SRC/SDL2_image-$SDL2_IMAGE_VER" \
  -DSDL2IMAGE_SAMPLES=OFF -DSDL2IMAGE_TESTS=OFF -DSDL2IMAGE_VENDORED=OFF \
  -DSDL2IMAGE_DEPS_SHARED=OFF -DSDL2IMAGE_PNG=ON -DSDL2IMAGE_JPG=ON \
  -DSDL2IMAGE_QOI=OFF -DSDL2IMAGE_AVIF=OFF -DSDL2IMAGE_WEBP=OFF \
  -DSDL2IMAGE_TIF=OFF -DSDL2IMAGE_JXL=OFF -DSDL2IMAGE_BACKEND_IMAGEIO=OFF

echo
echo "Done. $TARGET libraries are in $PREFIX"
