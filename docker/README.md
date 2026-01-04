# SC1000 Buildroot Docker Environment

This Docker setup provides an isolated buildroot 2018.08 environment for building
SC1000/SC500 binaries that are compatible with the device's uClibc-based OS.

## Why Docker?

The SC1000/SC500 uses an older Linux with **uClibc** (not glibc). Standard ARM
cross-compilers produce glibc binaries that won't run on the device. The buildroot
2018.08 toolchain produces compatible uClibc binaries.

## Prerequisites

- Docker installed (`sudo apt install docker.io`)
- Your user in the docker group (`sudo usermod -aG docker $USER`, then re-login)

## Quick Start

```bash
# 1. Build the Docker image (one-time setup, ~2 minutes)
./build-docker.sh

# 2. Enter the build environment
./run-docker.sh

# 3. Inside the container, build SC1000:
./sc1000/docker/build-sc1000.sh
```

The build script automatically:
1. Configures buildroot with C++ support
2. Builds the ARM cross-compiler toolchain (first run only, ~30-60 min)
3. Builds ALSA library headers
4. Compiles SC1000 using CMake

## What's Inside

- Ubuntu 18.04 (matches buildroot 2018.08 era)
- Buildroot 2018.08 with SC1000 config
- CMake for the build system
- ARM cross-compiler targeting Cortex-A8 with NEON
- uClibc C library (matches device)

## Build Output

The compiled binary will be at:
```
software/build-buildroot/sc1000
```

To deploy to the device, copy this to `/root/xwax`:
```bash
# From host machine with USB stick mounted:
cp software/build-buildroot/sc1000 /media/username/USB_STICK/xwax
```

Or via serial/SSH:
```bash
scp software/build-buildroot/sc1000 root@sc1000:/root/xwax
```

## Verifying the Binary

A correctly built binary should show:
```
$ file build-buildroot/sc1000
ELF 32-bit LSB executable, ARM, EABI5 version 1 (SYSV),
dynamically linked, interpreter /lib/ld-uClibc.so.0, ...
```

Key indicators:
- `interpreter /lib/ld-uClibc.so.0` - uses uClibc (correct)
- NOT `interpreter /lib/ld-linux-armhf.so.3` - that's glibc (wrong)

## Build Times

| Step | First Build | Subsequent |
|------|-------------|------------|
| Docker image | ~2 min | Cached |
| Toolchain | 30-60 min | Cached |
| ALSA lib | ~2 min | Cached |
| SC1000 | ~30 sec | ~30 sec |

Buildroot caches everything in Docker volumes, so only SC1000 itself
is recompiled on subsequent builds.

## Manual Build Steps

If you need more control, here are the manual steps inside the container:

```bash
# 1. Setup buildroot config (one-time)
cd /home/builder/buildroot-2018.08
cp /home/builder/sc1000/os/buildroot/buildroot_config .config

# 2. Enable C++ (one-time)
sed -i 's/# BR2_TOOLCHAIN_BUILDROOT_CXX is not set/BR2_TOOLCHAIN_BUILDROOT_CXX=y/' .config
make olddefconfig

# 3. Build toolchain (one-time, ~30-60 min)
make toolchain

# 4. Build ALSA headers (one-time)
make alsa-lib

# 5. Build SC1000 with CMake
cd /home/builder/sc1000/software
mkdir -p build-buildroot && cd build-buildroot
cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/buildroot-uclibc.cmake \
      -DBUILDROOT_DIR=/home/builder/buildroot-2018.08 \
      -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

## Troubleshooting

### Build fails with download errors
Buildroot 2018.08 has some outdated package URLs. The toolchain and alsa-lib
should still work, but if you encounter issues:
1. Check buildroot-2018.08/dl/ for partially downloaded files
2. Manually download the package and place in dl/

### Binary won't run on device
Check interpreter with: `readelf -l sc1000 | grep interpreter`
- Should show `/lib/ld-uClibc.so.0`
- If it shows `/lib/ld-linux-armhf.so.3`, you used the wrong toolchain

### CMake can't find compiler
Make sure you're inside the Docker container and the toolchain was built:
```bash
ls /home/builder/buildroot-2018.08/output/host/bin/arm-linux-g++
```

## Device Notes

- **SC1000**: Full device with beat deck volume ADC
- **SC500**: Simplified version, auto-detected via GPIO 6:11

The same binary works on both devices - the SC500 detection is automatic.
