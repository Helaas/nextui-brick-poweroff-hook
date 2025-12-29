# PowerOff Hook for NextUI — Kernel Module for shutting down the TrimUI Brick safely

> ℹ️ **Project Status Update**  
> Thank you to all testers who helped validate this project! The PowerOff Hook logic has been incorporated into:
> - **MinUI** starting from release [MinUI-20251123-1](https://github.com/shauninman/MinUI/releases)
> - **NextUI** starting from release [v6.6.0](https://github.com/LoveRetro/NextUI/releases/)
> - **PakUI** starting from release [v1.3.2](https://github.com/tenlevels/PakUI/releases/)

> ⚠️ **Warning**  
> Testing for this kernel module is *concluded*. It's best that you **uninstall** it, and update your launcher to at least the versions listed above.
> To safely uninstall, disable the hook from the Tools menu (set both options to Off). After that you can delete the PowerOffHook.pak folder from the tools folder.

## Overview

The **PowerOff Hook** is a kernel module that ensures proper and safe shutdown of the TrimUI Brick’s AXP2202 Power Management IC (PMIC).  
It prevents **battery overheating during shutdown** by executing a minimal, datasheet-compliant PMIC power-off sequence.

The module works seamlessly with NextUI’s `poweroff_next` script to coordinate a clean system shutdown:
1. Gracefully terminate user processes  
2. Unmount filesystems and the SD card  
3. Execute the safe PMIC shutdown sequence  
4. Power off the system cleanly  

---

## Installation

### PAKZ Installation
1. Download **PowerOffHook.pakz** from the [Releases page](https://github.com/Helaas/nextui-brick-poweroff-hook/releases).  
2. Turn off the TrimUI Brick, eject the SD card, and insert it into your computer.  
3. Copy the `.pakz` file to the **root of your SD card**.  
4. Reinsert the SD card into your TrimUI Brick.  
5. **Power on the device** — NextUI will automatically detect and install the package.  
6. After installation, NextUI removes the `.pakz` file automatically.  

---

### PAKZ Reinstallation
The PAKZ overwrites the `poweroff_next` file located at  
`/mnt/SDCARD/.system/tg5040/bin/poweroff_next`  
with a modified version that supports the PowerOff Hook module.

Updating NextUI will overwrite this file again, so you’ll need to reinstall the PAKZ after any NextUI updates.  
Once the module has been tested and confirmed stable across devices, future versions of NextUI may include this functionality natively.

---

### PAK Store Installation
Currently **unavailable** until stability is verified across a wider range of devices.

---

### Manual Verification
After the device boots, the PowerOff Hook package is installed to:

- **Tools directory**:  
  `/mnt/SDCARD/Tools/tg5040/PowerOffHook.pak/`
- **System binary (wrapper script)**:  
  `/mnt/SDCARD/.system/tg5040/bin/poweroff_next`

---

## Enabling and Disabling the Module

Once installed, you can manage the PowerOff Hook through the **NextUI Tools interface**:

1. Go to **Tools** → Select **PowerOffHook**  
2. Two toggle options are available:  
   - **Enable/Disable** — Activates or deactivates the module for the current session  
   - **Start on Boot** — Automatically loads the module during system startup  

---

### What Each Option Does

#### **Enable/Disable Toggle**
- **Enabled (✓)**  
  - Loads the kernel module into memory  
  - Routes shutdown sequences through the safe PMIC shutdown routine  
  - The `poweroff_next` script detects the module and coordinates shutdown  

- **Disabled (✗)**  
  - Unloads the kernel module  
  - Falls back to standard BusyBox `poweroff`  
  - No special PMIC handling occurs  

#### **Start on Boot Toggle**
- **Enabled (✓)**  
  - Automatically loads the module at boot  
  - Ensures protection against battery overheating  
  - **Recommended** for everyday use  

- **Disabled (✗)**  
  - Module will not load automatically  
  - Must be manually enabled after each reboot  
  - Useful for testing or temporary deactivation  

---

## How the Module Works During Shutdown

When enabled, the shutdown process proceeds as follows:

1. **User holds the power button** → NextUI initiates shutdown  
2. The **`poweroff_next` script** executes:  
   - Checks if the module is loaded  
   - Gracefully terminates user processes  
   - Syncs filesystem data to prevent corruption  
   - Unmounts the SD card and other filesystems  
3. The **PowerOff Hook module** detects the shutdown signal and performs:  
   - **Step 1:** Mask all PMIC interrupts (registers `0x40–0x44`)  
   - **Step 2:** Clear interrupt status (registers `0x48–0x4C`)  
   - **Step 3:** Configure shutdown sources (`register 0x22 = 0x0A`)  
   - **Step 4:** Trigger software power-off (`register 0x27 = 0x01`)  
4. **Kernel executes `poweroff`** → System powers down cleanly  
5. **Battery protection:** The proper PMIC shutdown prevents overheating  

---

## Key Features

✅ **Safe PMIC Shutdown** — Fully compliant with AXP717 datasheet  
*(Note: AXP2202 is backward compatible with the AXP717 shutdown sequence; no public datasheet is available.)* <br />
✅ **Minimal Kernel Interaction** — Only 4 essential register operations  
✅ **SD Card Protection** — Ensures filesystems are unmounted first  
✅ **Charging Preserved** — Battery charging remains functional in standby  
✅ **Fail-Safe** — Safe fallback if the module fails to load  
✅ **Simple Management** — Enable/disable directly from the Tools menu  

---

## Troubleshooting

### Checking Logs
Using NextUI’s built-in **Files** tool, you can inspect system logs for PowerOff Hook messages.

When you open the app, you’ll start in `/mnt/SDCARD`.  
Navigate to:  
`.userdata/tg5040/logs/`  
Look for log files starting with **PowerOffHook**.

---

### Advanced Troubleshooting

> **Note:** Replace `DEVICE_IP` in all commands below with the actual IP address of your TrimUI Brick.  
> You can find the IP from your router, or by checking the SSH server info in the PAK Store.

#### Check if the Module Is Loaded
If you have the SSH server enabled via the PAK Store:
```bash
ssh root@DEVICE_IP "lsmod | grep poweroff_hook"
```
If the module appears in the output, it’s active.

#### View Kernel Logs
```bash
ssh root@DEVICE_IP "dmesg | grep poweroff_hook | tail -20"
```
Displays kernel-level debug messages from the module.

#### Check SD Card Unmount Issues
If shutdown appears to hang:
```bash
ssh root@DEVICE_IP "mount | grep SDCARD"
```
If the SD card is still mounted:
```bash
ssh root@DEVICE_IP "fuser /mnt/SDCARD"
```
This shows which processes are preventing unmount.

#### Force Reload the Module
```bash
ssh root@DEVICE_IP "rmmod poweroff_hook 2>/dev/null; insmod /mnt/SDCARD/Tools/tg5040/PowerOffHook.pak/bin/poweroff_hook.ko"
```

#### Manual Shutdown Test
To simulate the shutdown sequence without powering off:
```bash
ssh root@DEVICE_IP "touch /tmp/poweroff && dmesg | tail -20"
```

---

## Technical Details
See [README_Technical.md](README_Technical.md) for developer documentation and detailed module behavior.
