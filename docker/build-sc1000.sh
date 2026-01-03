#!/bin/bash
# Build SC1000 software using buildroot toolchain
# Run this INSIDE the Docker container

set -e

BUILDROOT_DIR="/home/builder/buildroot-2018.08"
SC1000_DIR="/home/builder/sc1000/software"
OUTPUT_DIR="$SC1000_DIR/build-buildroot"

# First, we need to build the buildroot toolchain (if not already built)
if [ ! -f "$BUILDROOT_DIR/output/host/bin/arm-linux-gcc" ]; then
    echo "=============================================="
    echo "Building buildroot toolchain (this takes a while)..."
    echo "=============================================="
    cd "$BUILDROOT_DIR"

    # Just build the toolchain, not the full image
    make toolchain
fi

TOOLCHAIN_PREFIX="$BUILDROOT_DIR/output/host/bin/arm-linux"

echo "=============================================="
echo "Building SC1000 with buildroot toolchain"
echo "=============================================="

# Create build directory
mkdir -p "$OUTPUT_DIR"
cd "$OUTPUT_DIR"

# Configure with CMake using buildroot toolchain
cmake \
    -DCMAKE_C_COMPILER="${TOOLCHAIN_PREFIX}-gcc" \
    -DCMAKE_CXX_COMPILER="${TOOLCHAIN_PREFIX}-g++" \
    -DCMAKE_C_FLAGS="-mcpu=cortex-a8 -mfpu=neon" \
    -DCMAKE_CXX_FLAGS="-mcpu=cortex-a8 -mfpu=neon" \
    -DCMAKE_SYSROOT="$BUILDROOT_DIR/output/staging" \
    -DCMAKE_FIND_ROOT_PATH="$BUILDROOT_DIR/output/staging" \
    -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
    -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
    ..

# Build
make -j$(nproc)

echo "=============================================="
echo "Build complete!"
echo "Binary: $OUTPUT_DIR/sc1000"
echo "=============================================="

# Show binary info
file "$OUTPUT_DIR/sc1000"
