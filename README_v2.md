# PowerOff Hook v2.0 - Signal-Based Design

## How It Works

### Overview
The poweroff_hook module now uses a signal-based approach that ensures the PMIC shutdown happens AFTER the SD card is unmounted, preventing file system corruption while ensuring battery protection.

### Workflow

1. **User presses power button** → MinUI creates `/tmp/poweroff` → `poweroff_next` is called

2. **poweroff_next script checks for module**:
   - If module loaded: Creates `/tmp/poweroff_signal` → proceeds with shutdown
   - If module NOT loaded: Falls back to normal `busybox poweroff`

3. **Module monitor thread** (running in background):
   - Detects `/tmp/poweroff_signal` file
   - Logs signal reception to SD card (before unmount)
   - **Waits** for `/mnt/SDCARD` to be unmounted (max 30 seconds)

4. **poweroff_next script** (in parallel):
   - Kills user processes
   - Syncs filesystem
   - **Unmounts /mnt/SDCARD**

5. **Module detects unmount**:
   - Executes 10-step AXP2202 PMIC shutdown sequence
   - Calls `kernel_power_off()`
   - System powers down cleanly

### Key Improvements

✅ **No file modification during shutdown** - Logs written BEFORE SD unmount  
✅ **No kernel thread killing** - Module runs independently  
✅ **Guaranteed execution order** - SD card unmounted before PMIC shutdown  
✅ **Fallback safety** - Works even if module not loaded  
✅ **Clean separation** - Script handles process cleanup, module handles PMIC  

## Installation

### 1. Deploy Updated Module

```bash
# Copy new module to device
cat src/poweroff_hook.ko | ssh root@192.168.0.156 "cat > /tmp/poweroff_hook.ko"

# Load it
ssh root@192.168.0.156 "rmmod poweroff_hook 2>/dev/null; insmod /tmp/poweroff_hook.ko"
```

### 2. Update poweroff_next Script

```bash
# Backup original
ssh root@192.168.0.156 "cp /mnt/SDCARD/.system/tg5040/bin/poweroff_next /mnt/SDCARD/.system/tg5040/bin/poweroff_next.bak"

# Deploy new version
cat poweroff_next.sh | ssh root@192.168.0.156 "cat > /mnt/SDCARD/.system/tg5040/bin/poweroff_next && chmod +x /mnt/SDCARD/.system/tg5040/bin/poweroff_next"
```

### 3. Test

```bash
# Via SSH (should work as before)
ssh root@192.168.0.156 "poweroff"

# Via power button (now works!)
# Press and hold power button on device
```

### 4. Verify

After reboot, check the log:

```bash
ssh root@192.168.0.156 "cat /mnt/SDCARD/.userdata/tg5040/logs/poweroff_hook.log"
```

You should see:
- Module load message
- Signal received message with timestamp
- "Waiting for /mnt/SDCARD to unmount..." message

## Technical Details

### Signal File
- Location: `/tmp/poweroff_signal`
- Created by: `poweroff_next` script
- Monitored by: Kernel module's monitor thread
- Polling interval: 100ms

### SD Card Unmount Detection
- Uses `kern_path()` to check if `/mnt/SDCARD` is accessible
- Waits up to 30 seconds for unmount
- Proceeds anyway if timeout (safety fallback)

### Logging Strategy
- Logs written to `/mnt/SDCARD/.userdata/tg5040/logs/poweroff_hook.log`
- Only logs BEFORE SD card unmount
- Module load log: Written at boot
- Signal received log: Written immediately when signal detected
- PMIC sequence logs: Only in dmesg (SD card already unmounted)

### Process Isolation
The module runs in its own kernel thread, so:
- ✅ Not affected by `task_killer` killing user processes
- ✅ Continues running even after all user processes killed
- ✅ Can wait for SD card unmount asynchronously
- ✅ Has full kernel access to I2C bus

## Debugging

### Check if module is loaded
```bash
lsmod | grep poweroff_hook
```

### Check monitor thread is running
```bash
ps | grep poweroff_monitor
```

### Check signal file handling
```bash
# Create signal manually
touch /tmp/poweroff_signal

# Watch dmesg
dmesg | grep poweroff_hook | tail -20

# Clean up
rm /tmp/poweroff_signal
```

### View kernel logs
```bash
dmesg | grep -i poweroff
```

## Comparison: Old vs New

| Aspect | Old Design | New Design |
|--------|-----------|------------|
| Trigger | `pm_power_off` hook | Signal file |
| Timing | During kernel shutdown | Before kernel shutdown |
| SD Card | May be unmounted | Guaranteed unmounted |
| Process Safety | Vulnerable to killing | Isolated kernel thread |
| Logging | Fails if SD unmounted | Completes before unmount |
| Fallback | None | Busybox poweroff |

## Troubleshooting

**Module doesn't load:**
- Check `dmesg` for errors
- Verify I2C adapter 6 exists: `ls /sys/bus/i2c/devices/`

**Power button doesn't work:**
- Check `poweroff_next` script is updated
- Verify module shows in `lsmod`
- Check `/tmp/poweroff_signal` is created

**Still get battery overheating:**
- Check dmesg shows all 10 PMIC steps completed
- Verify I2C communication isn't failing
- May need to check I2C bus permissions

