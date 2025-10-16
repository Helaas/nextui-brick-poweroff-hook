# TrimUI Brick AXP2202 Clean Poweroff Module

A Linux kernel module for the TrimUI Brick that **prevents battery overheating** during shutdown by properly communicating with the AXP2202 Power Management IC.

## The Problem

When the TrimUI Brick powers off using the default shutdown sequence, the AXP2202 PMIC (Power Management Integrated Circuit) does not completely disconnect all power rails. This causes:

- **Battery overheating** during and after shutdown
- Incomplete power rail disconnection
- Potential battery damage from prolonged current draw
- Unsafe thermal conditions

## The Solution

This kernel module hooks into the Linux kernel's power-off event (`SYS_POWER_OFF` **only** - not reboot) and executes a specific 10-step sequence of I2C register writes to the AXP2202 PMIC that:

1. Disables all interrupts and interrupt status flags
2. Disables all wake sources
3. **Disconnects the battery** (critical for preventing overheating)
4. Disables coulomb counter (battery fuel gauge)
5. Disables backup battery charging
6. Enables all shutdown sources
7. Configures POK (Power OK) for immediate shutdown
8. Triggers PMIC poweroff command
9. Disables all DC-DC converters
10. Disables all LDO (Low-Dropout) regulators

This ensures **complete power disconnection** and prevents battery overheating.

## Features

- ✅ **Battery overheating prevention** - The primary purpose
- ✅ **Power-off only** - Does NOT trigger on reboot/restart (system needs power rails active for reboot)
- ✅ **I2C communication** - Direct register writes to AXP2202 PMIC (I2C bus 6, address 0x34)
- ✅ **Production-ready** - Thoroughly tested on real hardware
- ✅ **Diagnostic logging** - Creates `/poweroff_log.txt` with timestamp for verification
- ✅ **Clean implementation** - Uses kernel reboot notifier system with high priority
- ✅ **Kernel 4.9 compatible** - Uses appropriate APIs for Linux 4.9.191

## Hardware Requirements

- **Device:** TrimUI Brick
- **SoC:** Allwinner A133 (ARM64/aarch64)
- **PMIC:** AXP2202 on I2C bus 6 at address 0x34
- **Kernel:** Linux 4.9.191
- **Architecture:** 64-bit ARMv8-A

## Development Requirements

### Development Machine (macOS)

- Docker Desktop installed and running
- Make utility (comes with Xcode Command Line Tools)
- SSH client (built-in)
- Optional: `sshpass` for automated deployment (`brew install sshpass`)
- ~150MB disk space for toolchain
- ~500MB disk space for kernel headers

### Target Device (TrimUI Brick)

- Kernel: 4.9.191 (aarch64)
- Root access via SSH (user: root, password: tina)
- I2C subsystem enabled with AXP2202 support

## Quick Start

### Option 1: Automated Setup

```bash
# Download all dependencies (toolchain + kernel headers)
make setup-deps

# Build the kernel module
make build

# Create deployment package for TrimUI
make deploy
```

Result: `deploy/PowerOffHook.pak.zip` ready for installation

### Option 2: Manual Steps

```bash
# 1. Clone repository
git clone https://github.com/Helaas/nextui-brick-poweroff-hook.git
cd nextui-brick-poweroff-hook

# 2. Download GCC toolchain (Linaro GCC 7.4.1)
make setup-toolchain

# 3. Download and configure kernel headers
make setup-headers

# 4. Build Docker image
make docker-build

# 5. Build the module
make build

# Result: src/poweroff_hook.ko (215KB with debug symbols)
```

## Installation Methods

### Method 1: TrimUI Pak System (Recommended)

```bash
# Build deployment package
make deploy

# Copy to TrimUI SD card
cp deploy/PowerOffHook.pak.zip /Volumes/SDCARD/Tools/tg5040/

# On device: Navigate to Tools → PowerOffHook
# Enable via UI and configure auto-start on boot
```

See [PAK_INSTALL.md](PAK_INSTALL.md) for detailed pak installation instructions.

### Method 2: Direct SSH Deployment

```bash
# Configure device IP (default: 192.168.0.156)
# Edit Makefile if needed

# Load module temporarily
make deploy-load

# Or install permanently
make deploy-install
```

## Testing

```bash
# Load module
make deploy-load

# SSH into device
ssh root@192.168.0.156

# Check module is loaded
lsmod | grep poweroff_hook

# Check kernel messages
dmesg | grep poweroff_hook

# You should see:
# poweroff_hook: I2C adapter 6 acquired for AXP2202 (addr 0x34)
# poweroff_hook: Will execute ONLY on SYS_POWER_OFF (not reboot)

# Test poweroff
poweroff

# After reboot, verify log was created
cat /poweroff_log.txt
```

Expected log content:
```
TrimUI Brick AXP2202 Clean Poweroff
Timestamp: 2025-10-16 21:45:30 UTC
Event: SYS_POWER_OFF
Action: Executed AXP2202 PMIC shutdown sequence
Purpose: Prevent battery overheating via complete power disconnection
Module: poweroff_hook v1.0
```

## How It Works

### 1. Module Initialization

When the module loads (`insmod poweroff_hook.ko`):

- Acquires I2C adapter for bus 6 (AXP2202 PMIC)
- Registers a reboot notifier with priority 200 (high priority, runs early)
- Prints diagnostic information to kernel log

### 2. Power-Off Event Detection

When the user triggers shutdown:

- Kernel's reboot notifier chain is called
- Module checks event type
- **If `SYS_POWER_OFF`**: Execute AXP2202 poweroff sequence
- **If `SYS_RESTART` or `SYS_HALT`**: Do nothing (skip for reboots)

### 3. AXP2202 Poweroff Sequence

10-step register write sequence via I2C:

```
Step 1: Disable all IRQs (0x40-0x47 = 0x00)
Step 2: Clear IRQ status (0x48-0x4F = 0xFF)
Step 3: Disable wake sources (0x26, 0x27 = 0x00)
Step 4: Battery disconnect (0x28 = 0x00) ← CRITICAL for overheating prevention
Step 5: Disable coulomb counter (0xB8 = 0x00)
Step 6: Disable backup battery (0x35 = 0x00)
Step 7: Enable shutdown sources (0x22 = 0xFF)
Step 8: Configure immediate shutdown (0x23, 0x24 = 0x00)
Step 9: Trigger poweroff (0x10 = 0x01)
Step 10: Disable power rails (0x80-0x92 = 0x00)
```

Each step includes appropriate delays (`msleep`) and error checking.

### 4. Final Steps

- Write diagnostic log to `/poweroff_log.txt`
- Sync all filesystems (`emergency_sync()`)
- Return control to kernel shutdown sequence

## Project Structure

```
.
├── src/
│   ├── poweroff_hook.c          # Main kernel module (300+ lines)
│   └── Kbuild                    # Kernel build configuration
├── bin/
│   ├── on-boot                   # Auto-start script (pak system)
│   ├── service-on                # Load module (insmod)
│   ├── service-off               # Unload module (rmmod)
│   └── service-is-running        # Check module status
├── deploy/
│   └── PowerOffHook.pak.zip      # TrimUI pak package (created by 'make deploy')
├── kernel-headers/               # Linux 4.9 headers (downloaded)
├── toolchain/                    # Linaro GCC 7.4.1 (downloaded)
├── Dockerfile                    # Cross-compilation environment
├── Makefile                      # Build system
├── launch.sh                     # Pak UI launcher
├── pak.json                      # Pak metadata
├── settings.json                 # Pak UI settings
├── README.md                     # This file
├── PAK_INSTALL.md                # Pak installation guide
├── QUICKSTART.md                 # Developer quick start
└── LICENSE                       # GPL-2.0 license
```

## Makefile Targets

### Setup Targets
```bash
make setup-deps      # Download toolchain + kernel headers (first time only)
make setup-toolchain # Download Linaro GCC 7.4.1 (111 MB)
make setup-headers   # Download & configure Linux 4.9 headers
```

### Build Targets
```bash
make build           # Build kernel module (default)
make deploy          # Build + create PowerOffHook.pak.zip
make clean           # Remove build artifacts
make distclean       # Remove build artifacts + dependencies
make docker-build    # Build/check Docker image
make docker-shell    # Open interactive Docker shell
```

### Deployment Targets
```bash
make deploy-copy     # Copy module to device /tmp/
make deploy-load     # Copy + load module (insmod)
make deploy-unload   # Unload module (rmmod)
make deploy-test     # Load, wait for user, unload
make deploy-install  # Install to /lib/modules/4.9.191/
```

### Help
```bash
make help            # Show all available targets
```

## Technical Details

### Kernel Module Details

- **Name:** `poweroff_hook`
- **File:** `src/poweroff_hook.ko`
- **Size:** 215 KB (with debug symbols), ~3.4 KB loaded
- **License:** GPL v2
- **Dependencies:** I2C subsystem, kernel 4.9 APIs

### Compilation Flags

```bash
-fno-stack-protector    # Disable stack protection (kernel module)
-march=armv8-a          # ARMv8-A architecture
-mtune=cortex-a53       # Optimize for Cortex-A53 cores
```

### GCC Toolchain

**Critical:** Must use GCC 7.4.1 (exact match for TrimUI Brick kernel)

- **Version:** Linaro GCC 7.4.1-2019.02
- **Target:** aarch64-linux-gnu
- **Size:** 111 MB (compressed)
- **Why:** Kernel modules must use same GCC major version as the kernel

Using a different GCC version (e.g., 8.x) causes segmentation faults on module load due to ABI incompatibility.

### I2C Communication

```c
// I2C message structure for AXP2202
struct i2c_msg msg = {
    .addr = 0x34,        // AXP2202 I2C address
    .flags = 0,          // Write operation
    .len = 2,            // Register + value
    .buf = {reg, value}  // Data to write
};

// Transfer via I2C bus 6
i2c_transfer(i2c_adapter, &msg, 1);
```

### AXP2202 PMIC Registers

Key registers used in poweroff sequence:

| Register | Name | Purpose |
|----------|------|---------|
| 0x10 | Power Control | Trigger shutdown |
| 0x22 | Shutdown Enable | Enable shutdown sources |
| 0x23-0x24 | POK Configuration | Power OK timing |
| 0x26-0x27 | Wake Enable | Wake source control |
| 0x28 | Battery Control | **Battery disconnect** |
| 0x35 | Backup Battery | Backup battery control |
| 0x40-0x47 | IRQ Enable | Interrupt enables |
| 0x48-0x4F | IRQ Status | Interrupt status |
| 0x80-0x92 | Power Rails | DC-DC and LDO control |
| 0xB8 | Coulomb Counter | Fuel gauge control |

## Troubleshooting

### Module won't load

```bash
# Check kernel messages
dmesg | tail -20

# Common issues:
# - I2C adapter not found: Check I2C subsystem is enabled
# - Segmentation fault: Wrong GCC version (must be 7.4.1)
```

### Module loads but poweroff still causes overheating

```bash
# Check module is actually loaded
lsmod | grep poweroff_hook

# Check notifier was registered
dmesg | grep "registered power-off hook"

# Verify I2C communication works
dmesg | grep "I2C adapter 6 acquired"

# Test module actually triggers
# Power off and check dmesg on next boot
dmesg | grep "SYS_POWER_OFF event detected"
```

### Build failures

```bash
# Clean everything and rebuild
make distclean
make setup-deps
make build

# Check Docker is running
docker ps

# Verify toolchain downloaded
ls -lh toolchain/

# Verify kernel headers configured
ls -lh kernel-headers/linux-4.9/.config
```

## Safety Notes

⚠️ **IMPORTANT:** This module modifies PMIC behavior. While thoroughly tested, use at your own risk.

- ✅ **Safe:** Module only activates on actual poweroff (not reboot)
- ✅ **Safe:** Uses well-tested register sequence from userspace daemon
- ✅ **Safe:** Does not affect normal operation or reboot functionality
- ⚠️ **Caution:** If I2C communication fails, poweroff proceeds normally (safe fallback)
- ⚠️ **Caution:** Unloading module disables battery overheating prevention

## Why Not Use a Daemon?

Previous implementations used a userspace daemon (`poweroff_daemon.c`) that monitored for shutdown signals. The kernel module approach is superior because:

1. **Earlier execution** - Runs before userspace is shut down
2. **More reliable** - Guaranteed to execute on every poweroff
3. **No dependencies** - Doesn't require init system integration
4. **Cleaner** - Uses kernel's official reboot notifier API
5. **No processes** - Zero runtime overhead (only active during poweroff)

## Development Workflow

```bash
# 1. Make changes to src/poweroff_hook.c

# 2. Rebuild
make clean && make build

# 3. Test on device
make deploy-load

# 4. Power off to test
ssh root@192.168.0.156 "poweroff"

# 5. Reboot device, check log
ssh root@192.168.0.156 "cat /poweroff_log.txt"

# 6. Check kernel messages
ssh root@192.168.0.156 "dmesg | grep poweroff_hook"
```

## Contributing

This is a working, tested solution for TrimUI Brick battery overheating. Contributions welcome:

- Testing on different firmware versions
- Documentation improvements
- Code review and optimization
- Additional error handling

## License

GPL v2 - See [LICENSE](LICENSE) file

Kernel modules must be GPL-compatible to load into the Linux kernel.

## Credits

- Original AXP2202 poweroff sequence research and testing
- TrimUI Brick community for device specifications
- Linaro for the ARM64 GCC toolchain
- Linux kernel I2C and reboot notifier subsystems

## Related Documentation

- [PAK_INSTALL.md](PAK_INSTALL.md) - TrimUI pak installation guide
- [QUICKSTART.md](QUICKSTART.md) - Quick start for developers
- [SUCCESS.md](SUCCESS.md) - Project completion notes
- [poweroff_daemon.c](poweroff_daemon.c) - Original userspace daemon (deprecated)

## Disclaimer

This module is provided as-is for the TrimUI Brick community. Use at your own risk. The authors are not responsible for any damage to your device. Always test on a device you're comfortable with potentially bricking.

That said, this module has been thoroughly tested and is designed to be safe. The poweroff sequence is the same one used successfully in the userspace daemon version.

## Support

For issues, questions, or contributions:
- GitHub Issues: https://github.com/Helaas/nextui-brick-poweroff-hook/issues
- GitHub Discussions: https://github.com/Helaas/nextui-brick-poweroff-hook/discussions

---

**Last Updated:** October 16, 2025
**Module Version:** 1.0
**Target Device:** TrimUI Brick (tg5040)
**Kernel Version:** Linux 4.9.191
