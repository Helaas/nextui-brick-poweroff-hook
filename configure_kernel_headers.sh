#!/bin/bash
#
# configure_kernel_headers.sh - Configure kernel headers for module building
#

set -e

KERNEL_DIR="kernel-headers/linux-4.9"

if [ ! -d "$KERNEL_DIR" ]; then
    echo "Error: Kernel directory not found: $KERNEL_DIR"
    echo "Run './download_kernel_headers.sh' first"
    exit 1
fi

cd "$KERNEL_DIR"

echo "Configuring kernel headers for ARM64..."

# Create a minimal .config for ARM64 module building
cat > .config << 'EOF'
CONFIG_ARM64=y
CONFIG_64BIT=y
CONFIG_ARM64_PAGE_SHIFT=12
CONFIG_ARM64_VA_BITS=39
CONFIG_ARM64_PA_BITS=40
CONFIG_PGTABLE_LEVELS=3
CONFIG_HZ=250
CONFIG_PREEMPT=y
CONFIG_PREEMPT_COUNT=y
CONFIG_MODULES=y
CONFIG_MODULE_UNLOAD=y
CONFIG_MODVERSIONS=n
CONFIG_MODULE_SRCVERSION_ALL=n
CONFIG_LOCALVERSION=""
CONFIG_DEFAULT_HOSTNAME="TrimUI"
CONFIG_SYSVIPC=y
CONFIG_TREE_RCU=y
CONFIG_RCU_FANOUT=64
CONFIG_ARCH_USE_CMPXCHG_LOCKREF=y
CONFIG_BASE_SMALL=0
CONFIG_SPLIT_PTLOCK_CPUS=4
CONFIG_COMPACTION=y
CONFIG_MIGRATION=y
CONFIG_ZSMALLOC=n
CONFIG_FS_ENCRYPTION=n
CONFIG_MMU=y
CONFIG_SMP=y
CONFIG_NR_CPUS=8
CONFIG_RADIX_TREE_MULTIORDER=y
CONFIG_TRANSPARENT_HUGEPAGE=n
CONFIG_TIME_LOW_RES=n
CONFIG_ARM64_UAO=n
CONFIG_ARCH_ENABLE_SPLIT_PMD_PTLOCK=n
CONFIG_FSL_ERRATUM_A008585=n
EOF

echo "Running make olddefconfig..."
make ARCH=arm64 olddefconfig

echo "Preparing modules..."
make ARCH=arm64 modules_prepare

echo ""
echo "Kernel headers configured successfully!"
echo "You can now run 'make build' to compile the module."
