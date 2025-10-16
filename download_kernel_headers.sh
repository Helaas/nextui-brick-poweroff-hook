#!/bin/bash
#
# download_kernel_headers.sh - Download Linux 4.9 kernel headers
#

set -e

KERNEL_VERSION="4.9.191"
KERNEL_MAJOR="4.9"
HEADERS_DIR="kernel-headers"

echo "Downloading Linux kernel headers for version ${KERNEL_VERSION}..."

mkdir -p ${HEADERS_DIR}
cd ${HEADERS_DIR}

# Download kernel source tarball (we only need headers)
if [ ! -f "linux-${KERNEL_MAJOR}.tar.xz" ]; then
    echo "Downloading kernel ${KERNEL_MAJOR} source..."
    curl -L -o linux-${KERNEL_MAJOR}.tar.xz https://cdn.kernel.org/pub/linux/kernel/v4.x/linux-${KERNEL_MAJOR}.tar.xz
fi

# Extract headers
if [ ! -d "linux-${KERNEL_MAJOR}" ]; then
    echo "Extracting kernel source..."
    tar xf linux-${KERNEL_MAJOR}.tar.xz
fi

echo "Kernel headers prepared in ${HEADERS_DIR}/linux-${KERNEL_MAJOR}/"
echo "You can now run 'make build'"
