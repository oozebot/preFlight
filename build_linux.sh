#!/usr/bin/env bash
#/|/ Copyright (c) preFlight 2025+ oozeBot, LLC
#/|/
#/|/ Released under AGPLv3 or higher
#/|/
# Builds preFlight application for Linux using Ninja.
# Prerequisites: Run ./build_deps.sh first to build dependencies.
#
# Usage: ./build_linux.sh [options]
#   -debug    Build RelWithDebInfo instead of Release
#   -jobs N   Number of parallel build jobs (default: auto-detect)
#   -clean    Remove build directory and reconfigure from scratch
#   -config   Run cmake configure only, don't build

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CONFIG="Release"
BUILD_SUBDIR=""
JOBS=$(nproc)
CLEAN=0
CONFIG_ONLY=0

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        -debug)   CONFIG="RelWithDebInfo"; BUILD_SUBDIR="_debug" ;;
        -jobs)    JOBS="$2"; shift ;;
        -clean)   CLEAN=1 ;;
        -config)  CONFIG_ONLY=1 ;;
        -h|-help|--help)
            sed -n '8,13p' "$0"
            exit 0
            ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
    shift
done

BUILD_DIR="$SCRIPT_DIR/build${BUILD_SUBDIR}"
DEPS_PATH_FILE="$SCRIPT_DIR/deps/build/.DEPS_PATH.txt"
START_TIME=$SECONDS

# Read deps path
if [[ ! -f "$DEPS_PATH_FILE" ]]; then
    echo "ERROR: Dependencies not built. Run ./build_deps.sh first."
    exit 1
fi
DESTDIR="$(cat "$DEPS_PATH_FILE")"

echo "**********************************************************************"
echo "** preFlight Linux Build (Ninja)"
echo "** Config:    $CONFIG"
echo "** Build dir: $BUILD_DIR"
echo "** Deps:      $DESTDIR"
echo "** Jobs:      $JOBS"
echo "**********************************************************************"

# Clean if requested
if [[ $CLEAN -eq 1 && -d "$BUILD_DIR" ]]; then
    echo "** Cleaning $BUILD_DIR ..."
    rm -rf "$BUILD_DIR"
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
    -DSLIC3R_GTK=3 \
    -DSLIC3R_PCH=1 \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

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

echo ""
echo "**********************************************************************"
echo "** Build complete!"
echo "** Elapsed: ${MINS}m ${SECS}s"
echo "** compile_commands.json: $BUILD_DIR/compile_commands.json"
echo "** Executable: $BUILD_DIR/src/preFlight"
echo "**********************************************************************"
