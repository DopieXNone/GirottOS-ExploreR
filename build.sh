#!/bin/bash

clear
set -e

if [ ! -d "build" ]; then
    meson setup build
else
    # picks up new/renamed source files without a full rebuild
    meson setup --reconfigure build >/dev/null
fi

meson compile -C build

echo
echo "Build complete!"
echo "Executable: ./build/myfm"
