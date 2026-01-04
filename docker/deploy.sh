#!/bin/bash
# Build and deploy SC1000 to USB stick
# Usage: ./deploy.sh

set -e

CONTAINER="awesome_lichterman"
USB_MOUNT="/media/niklas/STORE N GO"
OUTPUT_FILE="sc1000_new"
SOFTWARE_DIR="/home/niklas/CLionProjects/SC1000/software"

echo "=== Syncing source files to container ==="
docker cp "$SOFTWARE_DIR/src" "$CONTAINER:/home/builder/sc1000/software/"

echo "=== Building SC1000 ==="
docker exec "$CONTAINER" bash -c "cd /home/builder/sc1000/software && rm -rf src/Build/Release/obj && make ARCH=SC1000 build=Release BUILDROOT_PREFIX=/home/builder/buildroot-2018.08 -j\$(nproc)"

echo "=== Copying files to USB stick ==="
if [ ! -d "$USB_MOUNT" ]; then
    echo "ERROR: USB stick not mounted at: $USB_MOUNT"
    exit 1
fi

docker cp "$CONTAINER:/home/builder/sc1000/software/src/Build/Release/sc1000" "$USB_MOUNT/$OUTPUT_FILE"
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
