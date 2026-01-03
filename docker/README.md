# SC1000 Buildroot Docker Environment

This Docker setup provides an isolated buildroot 2018.08 environment for building
SC1000 binaries that are compatible with the device's uClibc-based OS.

## Prerequisites

- Docker installed (`sudo apt install docker.io`)
- Your user in the docker group (`sudo usermod -aG docker $USER`, then re-login)

## Quick Start

```bash
# 1. Build the Docker image (one-time setup, ~5 minutes)
./build-docker.sh

# 2. Enter the build environment
./run-docker.sh

# 3. Inside the container, build SC1000:
./sc1000/docker/build-sc1000.sh
```

## What's Inside

- Ubuntu 18.04 (matches buildroot 2018.08 era)
- Buildroot 2018.08 with SC1000 config
- ARM cross-compiler targeting Cortex-A8 with NEON
- uClibc C library (matches device)

## Build Output

The compiled binary will be at:
```
software/build-buildroot/sc1000
```

Copy this to a USB stick as `xwax` to run on the device.

## First Build Time

The first build takes ~30-60 minutes because buildroot needs to:
1. Download all source packages
2. Build the cross-compiler
3. Build uClibc and other dependencies

Subsequent builds are fast (just recompiles SC1000 code).

## Troubleshooting

If buildroot fails to download packages (old URLs), you may need to:
1. Update package hashes in buildroot
2. Or manually download and place in `buildroot-2018.08/dl/`
