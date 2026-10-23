#!/bin/sh
# Native Apple toolchain only. This script does not fetch game files, SDKs or dependencies.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ "$(uname -s)" != Darwin ] || [ "$(uname -m)" != arm64 ]; then
    echo 'Run this from a native arm64 terminal on macOS (not WSL or Rosetta).' >&2
    exit 2
fi
for tool in cmake ninja pkg-config python3 xcrun; do
    command -v "$tool" >/dev/null || { echo "Missing dependency: $tool" >&2; exit 2; }
done
SDK=$(xcrun --sdk macosx --show-sdk-path)
CC=$(xcrun --sdk macosx --find clang)
if command -v brew >/dev/null; then
    PREFIX=$(brew --prefix)
    export PKG_CONFIG_PATH="$PREFIX/opt/ffmpeg/lib/pkgconfig:$PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
    export CMAKE_PREFIX_PATH="$PREFIX${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
fi
pkg-config --exists sdl2 libavformat libavcodec libswscale libswresample libavutil
GEN=${SH2_GENERATED_DIR:-"$ROOT/src/recomp/gen"}
if [ ! -f "$GEN/recomp_dispatch.c" ]; then
    echo "Missing generated C in $GEN; run the installer to generate it from your supported EE EXE." >&2
    exit 2
fi
BUILD=${1:-"$ROOT/build/macos-arm64"}
cmake -S "$ROOT" -B "$BUILD" -G Ninja \
    -DCMAKE_C_COMPILER="$CC" -DCMAKE_OSX_SYSROOT="$SDK" \
    -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSH2_EXE_PROFILE=v10-ee \
    -DSH2_GENERATED_DIR="$GEN" -DSH2_MAIN_THREAD_GAME=ON \
    -DPython3_EXECUTABLE="$(command -v python3)"
cmake --build "$BUILD" --parallel "${SH2_JOBS:-4}"
{
    uname -a
    "$CC" --version
    xcrun --sdk macosx --show-sdk-version
    pkg-config --modversion sdl2 libavformat libavcodec libswscale libswresample libavutil
    file "$BUILD/sh2"
    shasum -a 256 "$BUILD/sh2"
    otool -L "$BUILD/sh2"
} > "$BUILD/macos-build-environment.txt"
echo "Built $BUILD/sh2; this is not a runtime verification. See MACOS_BUILD.md in the project workspace."
