#!/bin/bash

clear
set -e

echo "======== ExploreR Build ========"
echo

# ── check required tools ──────────────────────────────────────────────────────

missing=()

if ! command -v gcc >/dev/null 2>&1 && ! command -v clang >/dev/null 2>&1; then
    missing+=("gcc (or clang)")
fi

if ! command -v pkg-config >/dev/null 2>&1; then
    missing+=("pkg-config")
fi

if [ ${#missing[@]} -ne 0 ]; then
    echo "ERROR: Some required build tools are missing:"
    echo

    for dep in "${missing[@]}"; do
        echo "  - $dep"
    done

    echo
    echo "Please install the required build dependencies and try again."
    echo

    if command -v apt >/dev/null 2>&1; then
        echo "On Debian/Ubuntu:"
        echo "  sudo apt install build-essential pkg-config libgtk-4-dev"
    elif command -v dnf >/dev/null 2>&1; then
        echo "On Fedora:"
        echo "  sudo dnf install gcc pkg-config gtk4-devel"
    elif command -v pacman >/dev/null 2>&1; then
        echo "On Arch Linux:"
        echo "  sudo pacman -S base-devel pkgconf gtk4"
    elif command -v zypper >/dev/null 2>&1; then
        echo "On openSUSE:"
        echo "  sudo zypper install gcc pkg-config gtk4-devel"
    fi

    exit 1
fi

# ── check GTK 4 ───────────────────────────────────────────────────────────────

if ! pkg-config --exists "gtk4 >= 4.10" 2>/dev/null; then
    echo "ERROR: GTK 4 (>= 4.10) development files not found."
    echo
    if command -v apt >/dev/null 2>&1; then
        echo "  sudo apt install libgtk-4-dev"
    elif command -v dnf >/dev/null 2>&1; then
        echo "  sudo dnf install gtk4-devel"
    elif command -v pacman >/dev/null 2>&1; then
        echo "  sudo pacman -S gtk4"
    elif command -v zypper >/dev/null 2>&1; then
        echo "  sudo zypper install gtk4-devel"
    fi
    exit 1
fi

# ── check source file ─────────────────────────────────────────────────────────

SRC="src/main.c"

if [ ! -f "$SRC" ]; then
    echo "ERROR: $SRC not found."
    echo
    echo "Please run this script from the directory that contains $SRC."
    exit 1
fi

# ── pick compiler ─────────────────────────────────────────────────────────────

if command -v gcc >/dev/null 2>&1; then
    CC=gcc
else
    CC=clang
fi

# ── optional: PDF preview support (poppler-glib) ──────────────────────────────
# Not required to build MyFM. When present, the preview pane (Ctrl+P) can
# render an actual thumbnail of a PDF's first page instead of a generic
# "no preview available" placeholder.

POPPLER_CFLAGS=""
POPPLER_LIBS=""
POPPLER_DEFINE=""

if pkg-config --exists poppler-glib 2>/dev/null; then
    POPPLER_CFLAGS=$(pkg-config --cflags poppler-glib)
    POPPLER_LIBS=$(pkg-config --libs poppler-glib)
    POPPLER_DEFINE="-DMYFM_HAVE_POPPLER"
    echo "PDF preview: enabled (poppler-glib $(pkg-config --modversion poppler-glib))"
else
    echo "PDF preview: disabled (poppler-glib not found — install libpoppler-glib-dev"
    echo "             / poppler-glib-devel to enable real PDF thumbnails)"
fi
echo

# ── build ─────────────────────────────────────────────────────────────────────

GTK_CFLAGS=$(pkg-config --cflags gtk4)
GTK_LIBS=$(pkg-config --libs gtk4)

echo "Compiler : $CC"
echo "GTK4     : $(pkg-config --modversion gtk4)"
echo "Source   : $SRC"
echo "Output   : ./ExploreR"
echo

echo "Compiling..."
# shellcheck disable=SC2086
$CC $GTK_CFLAGS $POPPLER_CFLAGS \
    -D_GNU_SOURCE $POPPLER_DEFINE \
    -Wall -Wextra \
    -O2 \
    -o ExploreR \
    "$SRC" \
    $GTK_LIBS $POPPLER_LIBS

echo
echo "================================"
echo "Build complete!"
echo "Executable: ./ExploreR"
echo "================================"
