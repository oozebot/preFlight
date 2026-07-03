#!/usr/bin/env bash
#/|/ Copyright (c) preFlight 2025+ oozeBot, LLC
#/|/
#/|/ Released under AGPLv3 or higher
#/|/
# Unified build script for preFlight on all platforms.
# On Windows, run build.bat instead (sets up MSVC, then calls this script).
#
# Usage: ./build.sh [options]
#   -deps     Build dependencies only
#   -debug    Build RelWithDebInfo instead of Release
#   -clean    Remove build directory and reconfigure from scratch
#   -config   Run cmake configure only, don't build
#   -flush    Force resource recompilation (Windows: icons, splash screen)
#   -jobs N   Number of parallel build jobs (default: auto-detect)
#   -arch A   Target architecture override (macOS: arm64/x86_64)
#
# Examples:
#   ./build.sh                Build release
#   ./build.sh -deps          Build dependencies
#   ./build.sh -deps -clean   Clean and rebuild dependencies
#   ./build.sh -debug         Build with debug symbols
#   ./build.sh -clean         Clean release build
#   ./build.sh -flush         Rebuild with fresh resources
#   ./build.sh -debug -clean  Clean debug build

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CONFIG="Release"
BUILD_SUBDIR=""
CLEAN=0
CONFIG_ONLY=0
BUILD_DEPS=0
FLUSH=0
JOBS=""
ARCH=""

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        -deps)    BUILD_DEPS=1 ;;
        -debug)   CONFIG="RelWithDebInfo"; BUILD_SUBDIR="_debug" ;;
        -clean)   CLEAN=1 ;;
        -config)  CONFIG_ONLY=1 ;;
        -flush)   FLUSH=1 ;;
        -jobs)    JOBS="$2"; shift ;;
        -arch)    ARCH="$2"; shift ;;
        -h|-help|--help)
            sed -n '8,25p' "$0"
            exit 0
            ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
    shift
done

# ============================================================================
# Detect platform
# ============================================================================
PLATFORM_OS="$(uname -s)"
IS_WINDOWS=0
IS_MACOS=0

case "$PLATFORM_OS" in
    MINGW*|MSYS*|CYGWIN*)
        IS_WINDOWS=1
        PLATFORM_LABEL="Windows"
        ;;
    Darwin)
        IS_MACOS=1
        PLATFORM_LABEL="macOS"
        ;;
    *)
        PLATFORM_LABEL="Linux"
        ;;
esac

# ============================================================================
# Handle -deps: delegate to build_deps.sh and exit
# ============================================================================
if [[ $BUILD_DEPS -eq 1 ]]; then
    DEPS_ARGS=""
    if [[ $CLEAN -eq 1 ]]; then
        DEPS_ARGS="-clean"
    fi
    exec "$SCRIPT_DIR/build_deps.sh" $DEPS_ARGS
fi

# ============================================================================
# App build
# ============================================================================
BUILD_DIR="$SCRIPT_DIR/build${BUILD_SUBDIR}"
DEPS_PATH_FILE="$SCRIPT_DIR/deps/build/.DEPS_PATH.txt"
START_TIME=$SECONDS

# Read deps path
if [[ ! -f "$DEPS_PATH_FILE" ]]; then
    echo "ERROR: Dependencies not built. Run ./build.sh -deps first."
    exit 1
fi
DESTDIR="$(cat "$DEPS_PATH_FILE")"

# Auto-detect job count
if [[ -z "$JOBS" ]]; then
    if [[ $IS_WINDOWS -eq 1 ]]; then
        JOBS=${NUMBER_OF_PROCESSORS:-4}
    elif [[ $IS_MACOS -eq 1 ]]; then
        JOBS=$(sysctl -n hw.ncpu)
    else
        JOBS=$(nproc)
    fi
fi

# ============================================================================
# Platform-specific cmake arguments
# ============================================================================
CMAKE_EXTRA_ARGS=""

if [[ $IS_WINDOWS -eq 1 ]]; then
    # MSVC environment is already set up by build.bat
    export WXWIN="$DESTDIR"

    # Detect architecture from MSVC environment (set by build.bat)
    if [[ -z "$ARCH" ]]; then
        if [[ "$VSCMD_ARG_TGT_ARCH" == "arm64" ]]; then
            ARCH="arm64"
        else
            ARCH="x64"
        fi
    fi

    CMAKE_EXTRA_ARGS="-DwxWidgets_ROOT_DIR=$DESTDIR"
    CMAKE_EXTRA_ARGS="$CMAKE_EXTRA_ARGS -DwxWidgets_LIB_DIR=$DESTDIR/lib/vc_${ARCH}_lib"

elif [[ $IS_MACOS -eq 1 ]]; then
    if [[ -z "$ARCH" ]]; then
        ARCH=$(uname -m)
    fi
    if [[ "$ARCH" != "arm64" && "$ARCH" != "x86_64" ]]; then
        echo "ERROR: Unsupported architecture '$ARCH'. Use arm64 or x86_64."
        exit 1
    fi

    if [[ "$ARCH" == "arm64" ]]; then
        DEPLOY_TARGET="11.0"
    else
        DEPLOY_TARGET="10.15"
    fi

    CMAKE_EXTRA_ARGS="-DCMAKE_OSX_ARCHITECTURES=$ARCH"
    CMAKE_EXTRA_ARGS="$CMAKE_EXTRA_ARGS -DCMAKE_OSX_DEPLOYMENT_TARGET=$DEPLOY_TARGET"
    # Homebrew prefix is arch-specific (Apple Silicon /opt/homebrew, Intel /usr/local).
    # Headers only, so this stays correct even when cross-compiling x86_64 on arm64.
    if command -v brew >/dev/null 2>&1; then
        CMAKE_EXTRA_ARGS="$CMAKE_EXTRA_ARGS -DCMAKE_CXX_FLAGS=-I$(brew --prefix)/include"
    fi

else
    # Linux
    CMAKE_EXTRA_ARGS="-DSLIC3R_GTK=3"
fi

# Find Python 3.14 (required for ABI compatibility with bundled runtime)
if command -v python3.14 >/dev/null 2>&1; then
    CMAKE_EXTRA_ARGS="$CMAKE_EXTRA_ARGS -DPython3_EXECUTABLE=$(command -v python3.14)"
fi

echo "**********************************************************************"
echo "** preFlight Build ($PLATFORM_LABEL)"
echo "** Config:    $CONFIG"
if [[ -n "$ARCH" ]]; then
echo "** Arch:      $ARCH"
fi
echo "** Build dir: $BUILD_DIR"
echo "** Deps:      $DESTDIR"
echo "** Jobs:      $JOBS"
echo "**********************************************************************"

# Clean if requested
if [[ $CLEAN -eq 1 && -d "$BUILD_DIR" ]]; then
    echo "** Cleaning $BUILD_DIR ..."
    rm -rf "$BUILD_DIR"
fi

# Flush resources (Windows only - forces icon/resource recompilation)
if [[ $FLUSH -eq 1 && $IS_WINDOWS -eq 1 ]]; then
    echo "** Flushing resources from $BUILD_DIR ..."
    rm -f "$BUILD_DIR/src/preFlight.rc" 2>/dev/null
    rm -f "$BUILD_DIR/src/preFlight-gcodeviewer.rc" 2>/dev/null
    rm -f "$BUILD_DIR/src/CMakeFiles/preFlight_app_gui.dir/preFlight.rc.res" 2>/dev/null
    rm -f "$BUILD_DIR/src/CMakeFiles/preFlight_app_console.dir/preFlight.rc.res" 2>/dev/null
    rm -f "$BUILD_DIR/src/CMakeFiles/preFlight_app_gcodeviewer.dir/preFlight-gcodeviewer.rc.res" 2>/dev/null
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with Ninja
echo ""
echo "** Running CMake with Ninja generator ..."
cmake "$SCRIPT_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE="$CONFIG" \
    -DCMAKE_PREFIX_PATH="$DESTDIR" \
    -DSLIC3R_STATIC=1 \
    -DSLIC3R_PCH=1 \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    $CMAKE_EXTRA_ARGS

if [[ $CONFIG_ONLY -eq 1 ]]; then
    echo ""
    echo "** Configuration complete. Skipping build (-config flag)."
    echo "** compile_commands.json: $BUILD_DIR/compile_commands.json"
    exit 0
fi

# Build
echo ""
echo "** Building with Ninja ($JOBS parallel jobs) ..."
ninja -j "$JOBS"

ELAPSED=$(( SECONDS - START_TIME ))
MINS=$(( ELAPSED / 60 ))
SECS=$(( ELAPSED % 60 ))

EXE_NAME="preFlight"
if [[ $IS_WINDOWS -eq 1 ]]; then
    EXE_NAME="preFlight.exe"
fi

echo ""
echo "**********************************************************************"
echo "** Build complete!"
echo "** Elapsed: ${MINS}m ${SECS}s"
echo "** compile_commands.json: $BUILD_DIR/compile_commands.json"
echo "** Executable: $BUILD_DIR/src/$EXE_NAME"
echo "**********************************************************************"
