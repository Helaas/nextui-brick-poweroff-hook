# Quick Start Guide

## Building the Module

```bash
cd /Users/kevinvranken/GitHub/limbo/poweroff-hook
make clean && make
```

Output: `src/poweroff_hook.ko` (215KB)

## Loading on Device

```bash
# Copy to device
cat src/poweroff_hook.ko | ssh root@192.168.0.156 "cat > /tmp/poweroff_hook.ko"

# Load module
ssh root@192.168.0.156 "insmod /tmp/poweroff_hook.ko"

# Verify it's loaded
ssh root@192.168.0.156 "lsmod | grep poweroff"
```

Expected output:
```
poweroff_hook           3397  0
```

## Testing

```bash
# Check kernel messages
ssh root@192.168.0.156 "dmesg | grep poweroff"
```

Expected:
```
poweroff_hook: Successfully registered power-off hook
poweroff_hook: Will trigger only on SYS_POWER_OFF events
```

## Trigger Power-Off Test

```bash
ssh root@192.168.0.156 "poweroff"
```

After device powers off and you power it back on:

```bash
ssh root@192.168.0.156 "cat /poweroff_log.txt"
```

## Unloading

```bash
ssh root@192.168.0.156 "rmmod poweroff_hook"
```

## Permanent Installation

```bash
# Install to system modules
ssh root@192.168.0.156 "cp /tmp/poweroff_hook.ko /lib/modules/4.9.191/ && depmod -a"

# Auto-load at boot
ssh root@192.168.0.156 "echo 'poweroff_hook' >> /etc/modules"
```

## Troubleshooting

### Module won't load
```bash
# Check kernel messages
ssh root@192.168.0.156 "dmesg | tail -20"

# Verify module format
file src/poweroff_hook.ko

# Should show: ELF 64-bit LSB relocatable, ARM aarch64
```

### Check if hook is registered
```bash
ssh root@192.168.0.156 "dmesg | grep 'Successfully registered'"
```

### Remove module
```bash
ssh root@192.168.0.156 "rmmod poweroff_hook 2>&1"
```

## Build Environment

- **Docker Image:** `trimui-brick-gcc74`
- **Toolchain:** Linaro GCC 7.4.1-2019.02
- **Target:** aarch64-linux-gnu
- **Kernel:** Linux 4.9.191

## Device Access

- **IP:** 192.168.0.156
- **User:** root
- **Password:** tina
- **Kernel:** 4.9.191

## Files

- **Source:** `src/poweroff_hook.c` (195 lines)
- **Build:** `Makefile`
- **Deploy:** `deploy/deploy.sh` (290 lines)
- **Docs:** `README.md`, `SUCCESS.md`, `PROJECT_STATUS.md`
