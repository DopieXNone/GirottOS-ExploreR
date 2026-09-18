#!/bin/bash

clear
set -e

echo "=== MyFM Build ==="
echo

# Check required commands
missing=()

if ! command -v meson >/dev/null 2>&1; then
    missing+=("meson")
fi

if ! command -v ninja >/dev/null 2>&1 && ! command -v samurai >/dev/null 2>&1; then
    missing+=("ninja-build")
fi

if ! command -v gcc >/dev/null 2>&1 && ! command -v clang >/dev/null 2>&1; then
    missing+=("gcc (or clang)")
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

    # Give some common distro hints
    if command -v apt >/dev/null 2>&1; then
        echo "On Debian/Ubuntu:"
        echo "  sudo apt install meson ninja-build build-essential"
    elif command -v dnf >/dev/null 2>&1; then
        echo "On Fedora:"
        echo "  sudo dnf install meson ninja-build gcc"
    elif command -v pacman >/dev/null 2>&1; then
        echo "On Arch Linux:"
        echo "  sudo pacman -S meson ninja gcc"
    elif command -v zypper >/dev/null 2>&1; then
        echo "On openSUSE:"
        echo "  sudo zypper install meson ninja gcc"
    fi

    exit 1
fi

# Check that this is the project directory
if [ ! -f "meson.build" ]; then
    echo "ERROR: meson.build not found."
    echo
    echo "Please run this script from the root directory of the project."
    exit 1
fi

# Configure Meson
if [ ! -d "build" ]; then
    echo "Configuring project..."
    meson setup build
else
    echo "Reconfiguring project..."
    meson setup --reconfigure build >/dev/null
fi

echo
echo "Building MyFM..."
echo

meson compile -C build

echo
echo "================================"
echo "Build complete!"
echo "Executable: ./build/myfm"
echo "================================"
