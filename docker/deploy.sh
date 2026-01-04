#!/bin/bash
# Build and deploy SC1000 to USB stick
# Usage: ./deploy.sh [--make]
#   --make: Use legacy Makefile instead of CMake

set -e

CONTAINER="awesome_lichterman"
USB_MOUNT="/media/niklas/STORE N GO"
OUTPUT_FILE="sc1000_new"
SOFTWARE_DIR="/home/niklas/CLionProjects/SC1000/software"
USE_CMAKE=true

# Parse arguments
if [ "$1" = "--make" ]; then
    USE_CMAKE=false
    echo "Using legacy Makefile build"
fi

echo "=== Syncing source files to container ==="
docker cp "$SOFTWARE_DIR/src" "$CONTAINER:/home/builder/sc1000/software/"
docker cp "$SOFTWARE_DIR/cmake" "$CONTAINER:/home/builder/sc1000/software/"
docker cp "$SOFTWARE_DIR/CMakeLists.txt" "$CONTAINER:/home/builder/sc1000/software/"

echo "=== Building SC1000 ==="
if [ "$USE_CMAKE" = true ]; then
    # CMake build (primary) - using traditional syntax for CMake 3.10 compatibility
    docker exec "$CONTAINER" bash -c "cd /home/builder/sc1000/software && \
        rm -rf build-arm && mkdir -p build-arm && cd build-arm && \
        cmake -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_TOOLCHAIN_FILE=../cmake/buildroot-uclibc.cmake .. && \
        make -j\$(nproc)"
    BUILD_OUTPUT="/home/builder/sc1000/software/build-arm/sc1000"
else
    # Legacy Makefile build
    docker exec "$CONTAINER" bash -c "cd /home/builder/sc1000/software && \
        rm -rf src/Build/Release/obj && \
        make ARCH=SC1000 build=Release BUILDROOT_PREFIX=/home/builder/buildroot-2018.08 -j\$(nproc)"
    BUILD_OUTPUT="/home/builder/sc1000/software/src/Build/Release/sc1000"
fi

echo "=== Copying files to USB stick ==="
if [ ! -d "$USB_MOUNT" ]; then
    echo "ERROR: USB stick not mounted at: $USB_MOUNT"
    exit 1
fi

docker cp "$CONTAINER:$BUILD_OUTPUT" "$USB_MOUNT/$OUTPUT_FILE"
cp "$SOFTWARE_DIR/sc_settings.json" "$USB_MOUNT/sc_settings.json"
sync

echo "=== Unmounting USB stick ==="
udisksctl unmount -b /dev/disk/by-label/STORE\ N\ GO 2>/dev/null || umount "$USB_MOUNT" 2>/dev/null || true

echo ""
echo "Done! Safe to remove USB stick."
echo "On device:"
echo "  cp /media/sda/$OUTPUT_FILE /usr/bin/sc1000"
echo "  cp /media/sda/sc_settings.json /media/sda/sc_settings.json"
echo "  /usr/bin/sc1000"
