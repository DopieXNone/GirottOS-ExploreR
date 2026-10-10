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

if ! pkg-config --exists "gtk4 >= 4.12" 2>/dev/null; then
    echo "ERROR: GTK 4 (>= 4.12) development files not found."
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

# ── check source files ────────────────────────────────────────────────────────

SRC=(src/main.c src/gui.c src/settings.c)

for f in "${SRC[@]}" src/myfm.h; do
    if [ ! -f "$f" ]; then
        echo "ERROR: $f not found."
        echo
        echo "Please run this script from the project root (the folder with src/)."
        exit 1
    fi
done

# ── pick compiler ─────────────────────────────────────────────────────────────

if command -v gcc >/dev/null 2>&1; then
    CC=gcc
else
    CC=clang
fi

# ── Tela icon theme (file and folder icons) ──────────────────────────────────
# ExploreR draws its file/folder icons from Tela. Without it the program
# still works and shows emoji instead.

tela_installed() {
    for d in /usr/share/icons/Tela /usr/local/share/icons/Tela \
             "$HOME/.local/share/icons/Tela" "$HOME/.icons/Tela"; do
        [ -f "$d/index.theme" ] && return 0
    done
    command -v pacman >/dev/null 2>&1 && pacman -Qq tela-icon-theme >/dev/null 2>&1 && return 0
    return 1
}

if tela_installed; then
    echo "Icons      : Tela icon theme found"
else
    echo "Icons      : Tela icon theme not found, installing it..."
    if command -v yay >/dev/null 2>&1; then
        yay -S --needed tela-icon-theme || true
    elif command -v paru >/dev/null 2>&1; then
        paru -S --needed tela-icon-theme || true
    else
        echo "             Neither yay nor paru is available."
        echo "             Install Tela by hand: https://github.com/vinceliuice/Tela-icon-theme"
    fi
    if tela_installed; then
        echo "Icons      : Tela installed"
    else
        echo "Icons      : Tela still missing, ExploreR will show emoji icons"
    fi
fi
echo

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
echo "Sources  : ${SRC[*]}"
echo "Output   : ./ExploreR"
echo

echo "Compiling..."
# shellcheck disable=SC2086
$CC $GTK_CFLAGS $POPPLER_CFLAGS \
    -D_GNU_SOURCE $POPPLER_DEFINE \
    -Wall -Wextra \
    -O2 \
    -o ExploreR \
    "${SRC[@]}" \
    $GTK_LIBS $POPPLER_LIBS

echo
echo "================================"
echo "Build complete!"
echo "Executable: ./ExploreR"
echo "================================"
