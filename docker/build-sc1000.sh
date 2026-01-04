#!/bin/bash
# Build SC1000 software using buildroot toolchain
# Run from host: docker exec <container> /home/builder/sc1000/docker/build-sc1000.sh
# Or from inside container: /home/builder/sc1000/docker/build-sc1000.sh

set -e

BUILDROOT_DIR="/home/builder/buildroot-2018.08"
SC1000_DIR="/home/builder/sc1000/software"
OUTPUT_DIR="$SC1000_DIR/src/Build/Release"

# Check if buildroot config exists, copy if needed
if [ ! -f "$BUILDROOT_DIR/.config" ]; then
    echo "=============================================="
    echo "Setting up buildroot configuration..."
    echo "=============================================="
    cd "$BUILDROOT_DIR"
    cp /home/builder/sc1000/os/buildroot/buildroot_config .config

    # Enable C++ support (required for SC1000)
    sed -i 's/# BR2_TOOLCHAIN_BUILDROOT_CXX is not set/BR2_TOOLCHAIN_BUILDROOT_CXX=y/' .config

    make olddefconfig
fi

# Build toolchain if not already built
if [ ! -f "$BUILDROOT_DIR/output/host/bin/arm-linux-g++" ]; then
    echo "=============================================="
    echo "Building buildroot toolchain (this takes ~30-60 min)..."
    echo "=============================================="
    cd "$BUILDROOT_DIR"
    make toolchain
fi

# Build alsa-lib for headers if not present
if [ ! -f "$BUILDROOT_DIR/output/staging/usr/include/alsa/asoundlib.h" ]; then
    echo "=============================================="
    echo "Building alsa-lib..."
    echo "=============================================="
    cd "$BUILDROOT_DIR"
    make alsa-lib
fi

TOOLCHAIN_PREFIX="$BUILDROOT_DIR/output/host/bin/arm-linux"

echo "=============================================="
echo "Building SC1000 with Make + buildroot toolchain"
echo "=============================================="

cd "$SC1000_DIR"

# Clean and build with Release optimization
make clean 2>/dev/null || true
make ARCH=SC1000 build=Release BUILDROOT_PREFIX="$BUILDROOT_DIR" -j$(nproc)

echo "=============================================="
echo "Build complete!"
echo "Binary: $OUTPUT_DIR/sc1000"
echo "=============================================="

# Show binary info
file "$OUTPUT_DIR/sc1000"
echo ""
echo "Library dependencies:"
"$TOOLCHAIN_PREFIX-readelf" -d "$OUTPUT_DIR/sc1000" | grep NEEDED || true

echo ""
echo "To deploy: copy $OUTPUT_DIR/sc1000 to device as /usr/bin/sc1000"
