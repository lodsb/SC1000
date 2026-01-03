#!/bin/bash
# Run the SC1000 buildroot Docker container

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

echo "Starting SC1000 buildroot environment..."
echo "Project mounted at: /home/builder/sc1000"
echo ""

docker run -it --rm \
    -v "$PROJECT_DIR:/home/builder/sc1000:rw" \
    -v "$PROJECT_DIR/os/buildroot/buildroot_config:/home/builder/buildroot-2018.08/.config:ro" \
    sc1000-buildroot \
    /bin/bash

