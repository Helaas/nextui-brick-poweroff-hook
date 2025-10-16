# Installing PowerOffHook as a TrimUI Pak

This kernel module can be installed as a `.pak` file on your TrimUI Brick, giving you a UI to enable/disable it and configure auto-start on boot.

## Prerequisites

- TrimUI Brick device (tg5040 platform)
- NextUI or compatible firmware
- SD card with write access

## Installation

### 1. Build the Pak Package

On your development machine:

```bash
# First time setup - download dependencies (only needed once)
make setup-deps

# Build the kernel module and create pak.zip
make deploy
```

This creates `deploy/PowerOffHook.pak.zip` (~926 KB).

### 2. Install on TrimUI Brick

1. Power off the device and remove the SD card

2. Insert the SD card into your computer

2. Copy the contents of `PowerOffHook.pak.zip` to your TrimUI Brick SD card:
   - Location: `SDCARD/Tools/tg5040/PowerOffHook.pak`

3. Eject SD card safely and insert into your TrimUI Brick

4. Power on the device

### 3. Configure via UI

1. Navigate to **Tools** → **PowerOffHook**
2. Launch the configuration interface
3. Configure settings:
   - **Enable**: Toggle to load/unload the kernel module
   - **Start on boot**: Auto-load module when device boots

### 4. Test the Module

1. Enable the module via the UI (or from the command line: `/mnt/SDCARD/Tools/tg5040/PowerOffHook.pak/bin/service-on`)
2. Power off the device normally
3. After reboot, check for the log file:
   ```bash
   cat /poweroff_log.txt
   ```
   
The log should contain entries like:
```
[2025-10-16 21:30:45] System power-off event detected
```

## Pak Structure

The pak contains:

```
PowerOffHook.pak/
├── bin/
│   ├── on-boot           # Called at boot if "Start on boot" enabled
│   ├── service-on        # Loads the kernel module (insmod)
│   ├── service-off       # Unloads the kernel module (rmmod)
│   ├── service-is-running # Check if module is loaded
│   ├── poweroff_hook.ko  # The kernel module (244 KB)
│   ├── jq                # JSON processor for settings UI
│   ├── minui-list        # Settings UI component
│   └── minui-presenter   # Message display component
├── launch.sh             # Main UI launcher
├── settings.json         # UI configuration
├── pak.json              # Pak metadata
├── README.md             # Full documentation
└── LICENSE               # GPL-2.0 license
```

## Manual Control (SSH/Terminal)

If you prefer command-line control:

```bash
# Navigate to pak directory
cd /mnt/SDCARD/Tools/tg5040/PowerOffHook.pak

# Load module
./bin/service-on

# Check status
./bin/service-is-running && echo "Module loaded" || echo "Module not loaded"

# Unload module
./bin/service-off

# View kernel logs
dmesg | grep poweroff
```

## Log File Locations

The module attempts to write to these locations in order:
1. `/mnt/SDCARD/.userdata/tg5040/logs/poweroff_log.txt` (the GUI app in Tools)
2. `/mnt/SDCARD/.userdata/tg5040/logs/poweroff_hook.log` (the actual kernel hook)

## Troubleshooting

### Module won't load
- Check kernel logs: `dmesg | tail -20`
- Verify kernel version: `uname -r` (should be 4.9.191)
- Ensure no conflicting modules are loaded


### Log file not created
- Check dmesg for error messages
- Verify filesystem is writable
- Module may not have permissions to write to chosen location

## Uninstallation

1. Via UI: Disable the module and "Start on boot"
2. Delete the pak file from Tools directory
3. Reboot device (optional)

## Advanced Usage

### Enable at Boot (Manual)

Edit `/mnt/SDCARD/.userdata/tg5040/auto.sh` and add:

```bash
test -f "$SDCARD_PATH/Tools/tg5040/PowerOffHook.pak/bin/on-boot" && \
  "$SDCARD_PATH/Tools/tg5040/PowerOffHook.pak/bin/on-boot"
```

### Build Custom Version

Modify `src/poweroff_hook.c` and rebuild:

```bash
make clean
make deploy
```

Copy the new pak.zip to your device.

## See Also

- [README.md](README.md) - Full project documentation