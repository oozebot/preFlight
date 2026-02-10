#!/usr/bin/env bash
#/|/ Copyright (c) preFlight 2025+ oozeBot, LLC
#/|/
#/|/ Released under AGPLv3 or higher
#/|/
# Creates an AppImage package from a completed build.
# Prerequisites: Run ./build_linux.sh first to build the application.
#
# Usage: ./pack_appimage.sh [options]
#   -build-dir DIR   Path to build directory (default: ./build)
#   -output DIR      Output directory for .AppImage (default: ./releases)
#   -clean           Remove AppDir before assembling

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
OUTPUT_DIR="$SCRIPT_DIR/releases"
CLEAN=0

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        -build-dir) BUILD_DIR="$2"; shift ;;
        -output)    OUTPUT_DIR="$2"; shift ;;
        -clean)     CLEAN=1 ;;
        -h|-help|--help)
            sed -n '8,12p' "$0"
            exit 0
            ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
    shift
done

APPDIR="$BUILD_DIR/AppDir"
ARCH="$(uname -m)"
APPIMAGETOOL="$BUILD_DIR/appimagetool-$ARCH.AppImage"

# ---------------------------------------------------------------------------
# Read version from version.inc
# ---------------------------------------------------------------------------
VERSION=$(grep 'set(SLIC3R_VERSION ' "$SCRIPT_DIR/version.inc" | sed 's/.*"\(.*\)".*/\1/')
if [[ -z "$VERSION" ]]; then
    echo "ERROR: Could not read version from version.inc"
    exit 1
fi

echo "**********************************************************************"
echo "** preFlight AppImage Packager"
echo "** Version:   $VERSION"
echo "** Arch:      $ARCH"
echo "** Build dir: $BUILD_DIR"
echo "** Output:    $OUTPUT_DIR"
echo "**********************************************************************"

# ---------------------------------------------------------------------------
# Verify the build exists
# ---------------------------------------------------------------------------
BINARY="$BUILD_DIR/src/preflight"
if [[ ! -x "$BINARY" ]]; then
    BINARY="$BUILD_DIR/src/preFlight"
    if [[ ! -x "$BINARY" ]]; then
        echo "ERROR: Built executable not found. Run ./build_linux.sh first."
        exit 1
    fi
fi
echo "** Found executable: $BINARY"

# ---------------------------------------------------------------------------
# Download appimagetool if not cached
# ---------------------------------------------------------------------------
if [[ ! -x "$APPIMAGETOOL" ]]; then
    echo ""
    echo "** Downloading appimagetool ..."
    APPIMAGETOOL_URL="https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-$ARCH.AppImage"
    wget -q --show-progress -O "$APPIMAGETOOL" "$APPIMAGETOOL_URL"
    chmod +x "$APPIMAGETOOL"
    echo "** Cached at: $APPIMAGETOOL"
fi

# ---------------------------------------------------------------------------
# Assemble the AppDir
# ---------------------------------------------------------------------------
if [[ $CLEAN -eq 1 && -d "$APPDIR" ]]; then
    echo ""
    echo "** Cleaning existing AppDir ..."
    rm -rf "$APPDIR"
fi

echo ""
echo "** Assembling AppDir ..."

mkdir -p "$APPDIR/usr/bin"
mkdir -p "$APPDIR/usr/lib"

# Copy executable
cp "$BINARY" "$APPDIR/usr/bin/preflight"
chmod +x "$APPDIR/usr/bin/preflight"

# Create gcodeviewer symlink
ln -sf preflight "$APPDIR/usr/bin/preflight-gcodeviewer"

# Copy entire resources directory
# Resource path resolution (src/CLI/Setup.cpp:328-332):
#   binary at usr/bin/preflight looks for ../resources relative to bin/
#   → usr/resources/ is the correct location
echo "** Copying resources ..."
rm -rf "$APPDIR/usr/resources"
cp -r "$SCRIPT_DIR/resources" "$APPDIR/usr/resources"

# Desktop file and icon
cp "$SCRIPT_DIR/src/platform/unix/preFlight.desktop" "$APPDIR/preFlight.desktop"
cp "$SCRIPT_DIR/resources/icons/preFlight.svg" "$APPDIR/preFlight.svg"
mkdir -p "$APPDIR/usr/share/applications"
mkdir -p "$APPDIR/usr/share/icons/hicolor/scalable/apps"
cp "$SCRIPT_DIR/src/platform/unix/preFlight.desktop" "$APPDIR/usr/share/applications/"
cp "$SCRIPT_DIR/resources/icons/preFlight.svg" "$APPDIR/usr/share/icons/hicolor/scalable/apps/"

# ---------------------------------------------------------------------------
# Bundle ALL shared library dependencies (including glibc, libstdc++)
# ---------------------------------------------------------------------------
# linuxdeploy excludes glibc/libstdc++ by default, which makes AppImages
# non-portable across distros with different glibc versions. Instead, we
# bundle everything ourselves and use a custom AppRun with the bundled
# dynamic linker — the same approach used by PrusaSlicer community AppImages.
echo "** Bundling shared libraries ..."

# Collect all shared libraries needed by the executable
LIBS_TO_BUNDLE=$(ldd "$APPDIR/usr/bin/preflight" | grep "=> /" | awk '{print $3}' | sort -u)

# Also collect transitive deps (libs needed by the libs we bundle)
ALL_LIBS="$LIBS_TO_BUNDLE"
for lib in $LIBS_TO_BUNDLE; do
    TRANSITIVE=$(ldd "$lib" 2>/dev/null | grep "=> /" | awk '{print $3}') || true
    ALL_LIBS=$(echo -e "$ALL_LIBS\n$TRANSITIVE")
done
ALL_LIBS=$(echo "$ALL_LIBS" | sort -u | grep -v '^$')

LIB_COUNT=0
for lib in $ALL_LIBS; do
    if [[ -f "$lib" ]]; then
        cp -n "$lib" "$APPDIR/usr/lib/" 2>/dev/null || true
        LIB_COUNT=$((LIB_COUNT + 1))
    fi
done

# Bundle the dynamic linker itself
LD_LINUX=$(ldd "$APPDIR/usr/bin/preflight" | grep "ld-linux" | awk '{print $1}')
if [[ -z "$LD_LINUX" ]]; then
    LD_LINUX="/lib64/ld-linux-x86-64.so.2"
fi
# Resolve to actual file path
LD_LINUX_PATH=$(readlink -f "$LD_LINUX")
cp "$LD_LINUX_PATH" "$APPDIR/usr/lib/ld-linux-x86-64.so.2"
chmod +x "$APPDIR/usr/lib/ld-linux-x86-64.so.2"

echo "** Bundled $LIB_COUNT libraries + dynamic linker"

# ---------------------------------------------------------------------------
# Bundle WebKit2GTK subprocess binaries (Tauri approach)
# ---------------------------------------------------------------------------
# WebKit2GTK uses a multi-process architecture with hardcoded absolute paths
# to its subprocess binaries compiled into libwebkit2gtk*.so. WEBKIT_EXEC_PATH
# only works on developer builds and is ignored on distro packages.
#
# Solution (same as Tauri): copy subprocess binaries preserving the directory
# structure, then binary-patch libwebkit*.so to replace "/usr" with "././".
# The AppRun script cds into $APPDIR/usr/ before launching, so "././" resolves
# to the correct bundled paths.
echo "** Bundling WebKit2GTK subprocesses ..."

WEBKIT_BUNDLED=0
for d in /usr/lib/x86_64-linux-gnu/webkit2gtk-4.1 /usr/lib/x86_64-linux-gnu/webkit2gtk-4.0 \
         /usr/lib64/webkit2gtk-4.1 /usr/lib64/webkit2gtk-4.0; do
    if [[ -x "$d/WebKitWebProcess" ]]; then
        # Copy subprocess binaries and patchelf them to use the bundled ld-linux
        # and bundled libs. This avoids the GLIBC_PRIVATE symbol mismatch that
        # occurs when the system's ld-linux tries to load the bundled glibc.
        for proc in WebKitWebProcess WebKitNetworkProcess; do
            if [[ -x "$d/$proc" ]]; then
                mkdir -p "$APPDIR/$d"
                cp "$d/$proc" "$APPDIR/$d/$proc"
                chmod +x "$APPDIR/$d/$proc"

                # Patch interpreter to bundled ld-linux (relative to CWD which
                # AppRun sets to $APPDIR/usr/) and set rpath to bundled libs
                # ($ORIGIN/../.. navigates from webkit2gtk-X.Y/ up to usr/lib/)
                patchelf --set-interpreter ./lib/ld-linux-x86-64.so.2 \
                         --force-rpath --set-rpath '$ORIGIN/../..' \
                         "$APPDIR/$d/$proc"
                echo "   Patched ELF: $proc"

                # Bundle any additional libs these binaries need
                PROC_LIBS=$(ldd "$d/$proc" 2>/dev/null | grep "=> /" | awk '{print $3}') || true
                for lib in $PROC_LIBS; do
                    if [[ -f "$lib" ]]; then
                        cp -n "$lib" "$APPDIR/usr/lib/" 2>/dev/null || true
                    fi
                done
            fi
        done

        # Copy injected bundle library if present
        if [[ -f "$d/injected-bundle/libwebkit2gtkinjectedbundle.so" ]]; then
            mkdir -p "$APPDIR/$d/injected-bundle"
            cp "$d/injected-bundle/libwebkit2gtkinjectedbundle.so" "$APPDIR/$d/injected-bundle/"
        fi

        WEBKIT_BUNDLED=1
        echo "** Bundled WebKit2GTK subprocesses from: $d"
        break
    fi
done

if [[ $WEBKIT_BUNDLED -eq 0 ]]; then
    echo "** WARNING: WebKit2GTK subprocess binaries not found - embedded webview may not work"
fi

# Binary-patch libwebkit*.so to use relative paths instead of /usr
# "././" resolves correctly because AppRun cds into $APPDIR/usr/ before launch
echo "** Patching WebKit libraries for relative paths ..."
PATCHED=0
find "$APPDIR/usr/lib" -maxdepth 1 -name 'libwebkit*' -type f | while read -r wklib; do
    if grep -q '/usr' "$wklib" 2>/dev/null; then
        sed -i -e 's|/usr|././|g' "$wklib"
        echo "   Patched: $(basename "$wklib")"
        PATCHED=1
    fi
done
# NOTE: Do NOT sed-patch the subprocess ELF binaries — their interpreter and
# rpath are already set by patchelf above. Binary sed on ELF files corrupts
# the dynamic string table and breaks library resolution.

# ---------------------------------------------------------------------------
# Create custom AppRun script
# ---------------------------------------------------------------------------
# Uses the bundled ld-linux to launch with bundled glibc/libs, making the
# AppImage portable across glibc versions.
cat > "$APPDIR/AppRun" << 'APPRUN_EOF'
#!/bin/bash
HERE="$(dirname "$(readlink -f "$0")")"

# Do NOT export LD_LIBRARY_PATH — it leaks to child processes and causes
# system tools (bash, WebKit subprocesses) to load the bundled glibc, which
# crashes due to GLIBC_PRIVATE symbol mismatches with the system ld-linux.
# The main process uses --library-path (ld-linux argument) instead, and
# WebKit subprocesses are patchelf'd with bundled interpreter + rpath.

# GTK/GDK needs to find system modules (theming, input methods, etc.)
if [ -z "$GTK_PATH" ]; then
    for d in /usr/lib/x86_64-linux-gnu/gtk-3.0 /usr/lib64/gtk-3.0; do
        if [ -d "$d" ]; then
            export GTK_PATH="$d"
            break
        fi
    done
fi

# cd into usr/ so that binary-patched "././" paths in libwebkit2gtk resolve
# to the bundled WebKit subprocess binaries, and patchelf'd relative interpreter
# paths (./lib/ld-linux-x86-64.so.2) also resolve correctly.
cd "$HERE/usr"
exec ./lib/ld-linux-x86-64.so.2 \
    --library-path ./lib \
    ./bin/preflight "$@"
APPRUN_EOF
chmod +x "$APPDIR/AppRun"

echo "** AppDir assembled."

# ---------------------------------------------------------------------------
# Create AppImage with appimagetool
# ---------------------------------------------------------------------------
mkdir -p "$OUTPUT_DIR"

echo ""
echo "** Creating AppImage ..."

export APPIMAGE_EXTRACT_AND_RUN=1
APPIMAGE_FILE="$OUTPUT_DIR/preFlight-$VERSION-$ARCH.AppImage"

"$APPIMAGETOOL" \
    --comp zstd \
    "$APPDIR" \
    "$APPIMAGE_FILE"

# ---------------------------------------------------------------------------
# Done
# ---------------------------------------------------------------------------
if [[ -f "$APPIMAGE_FILE" ]]; then
    SIZE=$(du -h "$APPIMAGE_FILE" | cut -f1)
    echo ""
    echo "**********************************************************************"
    echo "** AppImage created successfully!"
    echo "** File: $APPIMAGE_FILE"
    echo "** Size: $SIZE"
    echo "**"
    echo "** To run:  chmod +x $(basename "$APPIMAGE_FILE") && ./$(basename "$APPIMAGE_FILE")"
    echo "**********************************************************************"
else
    echo ""
    echo "** WARNING: Expected AppImage not found at: $APPIMAGE_FILE"
    echo "** Check appimagetool output above for errors."
    ls -la "$OUTPUT_DIR"/*.AppImage 2>/dev/null || true
    exit 1
fi
