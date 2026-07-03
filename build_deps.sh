#!/usr/bin/env bash
#/|/ Copyright (c) preFlight 2025+ oozeBot, LLC
#/|/
#/|/ Released under AGPLv3 or higher
#/|/
# Builds preFlight dependencies.
# Usage: ./build_deps.sh [options]
#   -clean    Remove existing deps build and rebuild from scratch
#   -preset   CMake preset override (auto-detected if not specified)
#
# Platform auto-detection:
#   Windows ARM  -> win_arm64
#   Windows x64  -> win_amd64
#   macOS arm64  -> mac_universal_arm
#   macOS x86_64 -> mac_universal_x86
#   Linux        -> default
#
# Dependencies are installed to deps/build/destdir/usr/local (Windows)
# or deps/build-<preset>/destdir/usr/local (Linux/macOS)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CLEAN=0
PRESET=""
CMAKE_CMD="cmake"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        -clean)  CLEAN=1 ;;
        -preset) PRESET="$2"; shift ;;
        -h|-help|--help)
            sed -n '5,18p' "$0"
            exit 0
            ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
    shift
done

# Detect platform and configure cmake
PLATFORM_OS="$(uname -s)"
IS_WINDOWS=0

case "$PLATFORM_OS" in
    MINGW*|MSYS*|CYGWIN*)
        IS_WINDOWS=1

        # Find Visual Studio via vswhere
        VSWHERE="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
        [[ ! -f "$VSWHERE" ]] && VSWHERE="/c/Program Files/Microsoft Visual Studio/Installer/vswhere.exe"
        if [[ ! -f "$VSWHERE" ]]; then
            echo "ERROR: Visual Studio not found (vswhere.exe missing)"
            exit 1
        fi
        VS_PATH=$("$VSWHERE" -latest -property installationPath | tr -d '\r')

        # Use VS-bundled cmake
        VS_CMAKE="$VS_PATH/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
        if [[ -f "$VS_CMAKE" ]]; then
            CMAKE_CMD="$VS_CMAKE"
        else
            echo "ERROR: CMake not found in Visual Studio installation at:"
            echo "  $VS_CMAKE"
            exit 1
        fi

        # Auto-detect architecture from processor identifier (PROCESSOR_ARCHITECTURE
        # lies under x64 emulation on ARM, but PROCESSOR_IDENTIFIER is truthful)
        if [[ -z "$PRESET" ]]; then
            if echo "$PROCESSOR_IDENTIFIER" | grep -qi "ARM"; then
                PRESET="win_arm64"
            else
                PRESET="win_amd64"
            fi
        fi

        PLATFORM_LABEL="Windows ($PRESET)"
        ;;
    Darwin)
        if [[ -z "$PRESET" ]]; then
            if [[ "$(uname -m)" == "arm64" ]]; then
                PRESET="mac_universal_arm"
            else
                PRESET="mac_universal_x86"
            fi
        fi
        PLATFORM_LABEL="macOS ($(uname -m))"
        ;;
    *)
        if [[ -z "$PRESET" ]]; then
            PRESET="default"
        fi
        PLATFORM_LABEL="$PLATFORM_OS"
        ;;
esac

DEPS_DIR="$SCRIPT_DIR/deps"
if [[ $IS_WINDOWS -eq 1 ]]; then
    BUILD_DIR="$DEPS_DIR/build"
else
    BUILD_DIR="$DEPS_DIR/build-${PRESET}"
fi
DESTDIR="$BUILD_DIR/destdir/usr/local"
DEPS_PATH_FILE="$DEPS_DIR/build/.DEPS_PATH.txt"
START_TIME=$SECONDS

echo "**********************************************************************"
echo "** preFlight Dependency Build ($PLATFORM_LABEL)"
echo "** Preset: $PRESET"
echo "** CMake:  $CMAKE_CMD"
echo "** Output: $DESTDIR"
echo "**********************************************************************"

# Clean if requested
if [[ $CLEAN -eq 1 && -d "$BUILD_DIR" ]]; then
    echo "** Cleaning $BUILD_DIR ..."
    rm -rf "$BUILD_DIR"
fi

# Configure
echo ""
echo "** Configuring deps ..."
cd "$DEPS_DIR"
"$CMAKE_CMD" --preset "$PRESET"

# Record deps path so the app build script can find it
# On Windows, convert MSYS2 paths (/c/...) to native Windows paths (C:/...)
# since the path is read by native Windows tools (cmake.exe, batch scripts)
mkdir -p "$DEPS_DIR/build"
if [[ $IS_WINDOWS -eq 1 ]]; then
    cygpath -m "$DESTDIR" > "$DEPS_PATH_FILE"
else
    echo "$DESTDIR" > "$DEPS_PATH_FILE"
fi

# Build release deps (required)
echo ""
echo "** Building release deps (this will take a while) ..."
if [[ $IS_WINDOWS -eq 1 ]]; then
    "$CMAKE_CMD" --build "$BUILD_DIR" --target deps -j 1
else
    "$CMAKE_CMD" --build "$BUILD_DIR" --target deps -j 1 -- -k
fi

# Build debug deps (Windows/MSVC only, optional)
# MSVC can't link release libs into Debug builds, so debug variants are needed.
# If this fails, release builds and RelWithDebInfo still work fine.
if [[ $IS_WINDOWS -eq 1 ]]; then
    echo ""
    echo "** Building debug deps (optional) ..."
    if "$CMAKE_CMD" --build "$BUILD_DIR" --target deps_debug -j 1; then
        echo "** Debug deps built successfully."
    else
        echo ""
        echo "** WARNING: Debug deps build failed. Release and RelWithDebInfo"
        echo "** builds will work fine. Only pure Debug builds are affected."
        echo ""
        # Clean up partial debug config dirs that wxWidgets leaves behind,
        # otherwise FindwxWidgets will try to use debug libs that don't exist
        find "$BUILD_DIR/destdir" -type d -name "mswud" -exec rm -rf {} + 2>/dev/null
    fi
fi

ELAPSED=$(( SECONDS - START_TIME ))
MINS=$(( ELAPSED / 60 ))
SECS=$(( ELAPSED % 60 ))

echo ""
echo "**********************************************************************"
echo "** Deps build complete!"
echo "** Elapsed: ${MINS}m ${SECS}s"
echo "** Installed to: $DESTDIR"
echo "**********************************************************************"
