# PowerOff Hook — Technical Implementation Guide

This document covers the technical implementation details of the PowerOff Hook kernel module.  
For user documentation, see [README.md](README.md).

---

## Build System & Architecture

### Project Structure

```
.
├── src/
│   ├── poweroff_hook.c                 # Main kernel module (~612 lines)
│   └── Kbuild                          # Kernel build configuration
├── bin/
│   ├── on-boot                         # Auto-start script (pak system)
│   ├── service-on                      # Loads module (insmod wrapper)
│   ├── service-off                     # Unloads module (rmmod wrapper)
│   ├── service-is-running              # Module status check
│   ├── poweroff_hook.ko                # Compiled module (populated by make deploy)
│   ├── jq                              # JSON processor (downloaded by make deploy)
│   ├── minui-list                      # UI list component (downloaded)
│   └── minui-presenter                 # Message display (downloaded)
├── pakz/
│   ├── .system/tg5040/bin/             # System binaries directory
│   │   └── poweroff_next               # Power-off wrapper script
│   └── Tools/tg5040/PowerOffHook.pak/  # Staging for pakz structure
├── deploy/
│   ├── PowerOffHook.pak.zip            # PAK Store package
│   └── PowerOffHook.pakz               # Direct SDCARD installation package
├── kernel-headers/                     # Linux 4.9 kernel headers (auto-downloaded)
├── toolchain/                          # Linaro GCC 7.4.1 (auto-downloaded)
├── Dockerfile                          # Docker cross-compilation environment
├── Makefile                            # Build & deployment automation
├── launch.sh                           # PAK UI entry point (launch.sh logic)
├── pak.json                            # PAK metadata
├── settings.json                       # UI configuration
├── README.md                           # Installation & usage guide
├── LICENSE                             # GPL-2.0
└── Makefile                            # Complete build system
```

### Makefile Targets

#### Setup (One-Time)
```bash
make setup-deps          # Download toolchain + kernel headers (~611 MB)
make setup-toolchain     # Download Linaro GCC 7.4.1 (~111 MB)
make setup-headers       # Download & configure Linux 4.9 headers
```

#### Build
```bash
make build               # Compile module with Docker cross-compiler
make deploy              # Build + create PowerOffHook.pak.zip + PowerOffHook.pakz
make clean               # Remove build artifacts (keeps dependencies)
make distclean            # Remove everything including downloaded dependencies
make docker-build        # Build/verify Docker image
make docker-shell        # Interactive Docker shell for debugging
```

#### Deployment
```bash
make deploy-copy         # SCP module to device /tmp/
make deploy-load         # Copy + insmod on device
make deploy-unload       # rmmod on device
make deploy-test         # Load → pause → unload workflow
make deploy-install      # Copy to /lib/modules/4.9.191/
```

---

## Kernel Module Architecture

### Module Metadata

- **Name:** `poweroff_hook`
- **Version:** 1.0
- **License:** GPL v2
- **Size:** 215 KB (with debug symbols), ~3.4 KB runtime
- **Target:** Linux 4.9.191, aarch64, TrimUI Brick
- **PMIC:** AXP717/AXP2202 on I2C bus 6 (address 0x34)

### Core Components

#### 1. I2C Communication Layer
```c
// Registers PMIC on I2C bus 6
static struct i2c_adapter *i2c_adapter = NULL;
#define I2C_BUS_NUMBER 6
#define AXP2202_I2C_ADDR 0x34

// Write register via I2C
static int axp2202_write_reg(u8 reg, u8 value)
```

#### 2. PMIC Shutdown Sequence (4-step safe minimal version)
```
Step 1: Mask interrupts (0x40-0x44 = 0x00)
Step 2: Clear interrupt status (0x48-0x4C = 0xFF)
Step 3: Configure shutdown sources (0x22 = 0x0A, only bits 1 & 3)
Step 4: Trigger software poweroff (0x27 = 0x01)
```

**Safe Design Notes:**
- Only 4 essential operations (vs original 10)
- Register 0x22 = 0x0A (NOT 0xFF) — prevents reserved bit corruption
- IRQ register ranges validated against AXP717 datasheet v1.0
- Battery charging preserved (bit 0 of 0x19 = 0x01)
- All operations have error checking and msleep delays

#### 3. Signal File Monitoring
```c
#define POWEROFF_SIGNAL_FILE "/tmp/poweroff"
```
- Module detects signal file creation
- Triggers safe shutdown sequence
- Only activates on actual poweroff (not reboot)

#### 4. Logging Infrastructure
```c
#define LOG_PATH "/mnt/SDCARD/.userdata/tg5040/logs/PowerOffHook-KernelModule.txt"
```
- Writes debug markers throughout shutdown
- Disables SD logging before unmount to prevent file locks
- Clears log after append to prevent corruption

---

## I2C & PMIC Register Details

### AXP717/AXP2202 Register Map (Safe Minimal Version)

| Register | Hex | Purpose | Value | Notes |
|----------|-----|---------|-------|-------|
| IRQ Enable 1 | 0x40 | Mask all interrupts | 0x00 | Step 1 |
| IRQ Enable 2-5 | 0x41-0x44 | Additional masks | 0x00 | Step 1 |
| IRQ Status 1 | 0x48 | Clear IRQ flags | 0xFF | Step 2 |
| IRQ Status 2-5 | 0x49-0x4C | Additional status | 0xFF | Step 2 |
| PWROFF_EN | 0x22 | Shutdown sources | 0x0A | Step 3 (bits 1,3 only) |
| Soft Poweroff | 0x27 | Trigger shutdown | 0x01 | Step 4 |

### Why This Sequence Works

1. **Mask IRQs** — Prevents interrupt storms during shutdown
2. **Clear Status** — Ensures clean interrupt state
3. **Configure PWROFF_EN** — Only enables documented shutdown bits (avoids reserved bits)
4. **Trigger Poweroff** — Initiates PMIC shutdown sequence

The AXP717 datasheet (v1.0) validates all register ranges. AXP2202 is backward compatible.

---

## File Coordination Between Components

### `poweroff_next` Script Workflow
Located at: `/mnt/SDCARD/.system/tg5040/bin/poweroff_next`

```bash
1. Detect if module loaded: lsmod | grep poweroff_hook
2. If loaded:
   - Write "/root/poweroff_hook.log" debug marker
   - Sleep 10 seconds (allows module shutdown sequence)
   - Falls through to busybox poweroff
3. If not loaded:
   - Direct fallback to busybox poweroff
```

### Module Workflow
Loaded via: `/mnt/SDCARD/Tools/tg5040/PowerOffHook.pak/bin/service-on`

```c
1. Load module: insmod poweroff_hook.ko
2. Module init:
   - Acquire I2C adapter 6
   - Prepare PMIC communication
3. On shutdown signal:
   - Detect /tmp/poweroff signal file
   - Execute 4-step PMIC shutdown
   - Kernel handles final poweroff
```

### Shell Scripts Location

- **On-boot:** `bin/on-boot` — Called if "Start on Boot" enabled
- **Service control:** `bin/service-on`, `bin/service-off` — Module load/unload
- **Status check:** `bin/service-is-running` — Verify module is loaded

---

## Launch.sh UI Integration

The `launch.sh` script provides the NextUI Tools interface:

```bash
# Load current settings from settings.json
current_settings()

# Show minui-list UI with options
main_screen()

# Apply changes
if [ "$enabled" = "1" ]; then
    ./bin/service-on      # Load module
else
    ./bin/service-off     # Unload module
fi

if [ "$start_on_boot" = "1" ]; then
    enable_start_on_boot  # Add to auto.sh
else
    disable_start_on_boot # Remove from auto.sh
fi
```

Settings are stored in: `/mnt/SDCARD/Tools/tg5040/PowerOffHook.pak/settings.json`

---

## Compilation & Toolchain

### GCC Version Requirements

**Critical:** Must use Linaro GCC 7.4.1-2019.02

Kernel modules require **exact GCC major version match** as the kernel:
- Kernel compiled with: GCC 7.4.1
- Module must use: GCC 7.4.1
- Using GCC 8.x or newer: **Causes segmentation faults on load**

Reason: Kernel module ABI incompatibility (calling conventions, register allocation differ between major versions).

### Build Environment

```dockerfile
# FROM: ubuntu:18.04
# Installs:
#   - Linaro GCC 7.4.1 (aarch64-linux-gnu)
#   - Build tools (make, gcc, binutils)
#   - Linux 4.9 kernel headers
#   - Cross-compilation flags
```

### Compilation Command

```bash
make -C kernel-headers/linux-4.9 \
  M=/work/src \
  ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu- \
  EXTRA_CFLAGS='-fno-stack-protector -march=armv8-a -mtune=cortex-a53' \
  modules
```

---

## Packaging Formats

### PowerOffHook.pak.zip (PAK Store)
- **Structure:** Flat files at root
- **Files:**
  - `bin/` — Compiled module + utilities
  - `launch.sh`, `pak.json`, `settings.json`
  - `README.md`, `LICENSE`
- **Installation:** Via NextUI PAK Store
- **Size:** ~926 KB
- **Automatic Installation:** NO (manual via PAK Store UI)

### PowerOffHook.pakz (Direct Installation)
- **Structure:** Preserves directory hierarchy
  - `Tools/tg5040/PowerOffHook.pak/` — Full pak structure
  - `.system/tg5040/bin/poweroff_next` — System wrapper script (preserved)
- **Installation:** Copy to SD card root, reboot
- **Size:** Similar to pak.zip
- **Automatic Installation:** YES (NextUI auto-detects and installs)
- **Auto-removal:** NextUI removes `.pakz` file after installation

### Makefile Deploy Logic

```makefile
deploy: build
  # 1. Create pak.zip (flat structure for PAK Store)
  #    - Copies bin/* launch.sh pak.json settings.json README.md LICENSE
  
  # 2. Create pakz directory structure
  #    mkdir -p pakz/Tools/tg5040/PowerOffHook.pak/bin
  #    cp bin/* → pakz/Tools/.../bin/
  #    cp configs → pakz/Tools/.../
  #    (preserves existing .system/tg5040/bin/poweroff_next)
  
  # 3. Create pakz zip
  #    zip -r PowerOffHook.pakz Tools/ .system/
  
  # 4. Cleanup staging directory
  #    rm -rf pakz/Tools/
```

---

## Debugging & Logging

### Debug Markers in Code

Throughout `poweroff_hook.c`:
```c
printk(KERN_INFO "poweroff_hook: [MARKER_NAME] message\n");
```

Examples:
- `[MODULE_LOADED]` — Module initialized successfully
- `[BEFORE_PMIC_SHUTDOWN]` — About to execute PMIC sequence
- `[STEP1_COMPLETE]` through `[STEP4_COMPLETE]` — Each PMIC register operation
- `[UNMOUNT_SDCARD_SUCCESS]` — SD card successfully unmounted
- `[UNMOUNT_SDCARD_STILL_MOUNTED]` — Retry needed (with retry count)
- `[SD_STILL_MOUNTED_EMERGENCY]` — Timeout, proceeding anyway
- `[EMERGENCY_KERNEL_POWEROFF]` — Final kernel poweroff call

### Log Locations

1. **Kernel logs:** `dmesg` (always available)
   ```bash
   dmesg | grep poweroff_hook
   ```

2. **Pre-unmount log:** `/mnt/SDCARD/.userdata/tg5040/logs/PowerOffHook-KernelModule.txt`
   - Written before SD card unmount
   - Available after reboot via NextUI Files app

3. **Module load log:** `/root/poweroff_hook.log`
   - Created by `poweroff_next` script
   - Survives SD unmount (on root filesystem)

### Debug Testing

```bash
# Check if module is loaded
lsmod | grep poweroff_hook

# Simulate shutdown signal (WITHOUT actually powering off)
touch /tmp/poweroff
sleep 1
dmesg | tail -20
rm /tmp/poweroff

# Force reload after changes
rmmod poweroff_hook 2>/dev/null
insmod /mnt/SDCARD/Tools/tg5040/PowerOffHook.pak/bin/poweroff_hook.ko

# Check module status from device
lsmod | grep poweroff_hook && echo "Loaded" || echo "Not loaded"
```

---

## Development Workflow

### Local Development

```bash
# 1. Make changes to src/poweroff_hook.c

# 2. Build locally
make clean && make build

# 3. Create deployment package
make deploy

# 4. Copy to device (or SD card)
# Option A: Direct SSH
make deploy-load

# Option B: Manual SD card copy
# - Copy PowerOffHook.pak.zip contents to Tools/
# - Copy PowerOffHook.pakz to SD root

# 5. Test on device
ssh root@DEVICE_IP "poweroff"

# 6. After reboot, verify
ssh root@DEVICE_IP "dmesg | grep poweroff_hook"
ssh root@DEVICE_IP "cat /root/poweroff_hook.log"

# 7. Check module behavior
ssh root@DEVICE_IP "lsmod | grep poweroff_hook"
```

### Docker Build Environment

The Dockerfile sets up a complete cross-compilation environment. To debug builds:

```bash
# Open interactive shell in Docker
make docker-shell

# Inside container:
cd /work
make -C kernel-headers/linux-4.9 M=/work/src ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- modules

# Check build output
file src/poweroff_hook.ko
readelf -h src/poweroff_hook.ko
```

---

## References & Resources

- **AXP717 Datasheet:** See `README.md` for official X-Powers documentation
- **Linux Kernel API:** kernel.org (I2C, mount APIs, kernel threads)
- **Device Information:** TrimUI Brick (tg5040 platform)
- **Related Code:** `poweroff_next` wrapper script

---

**Last Updated:** October 24, 2025  
**Module Version:** 1.0  
**Target:** TrimUI Brick (tg5040)  
**Kernel:** Linux 4.9.191