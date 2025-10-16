/*
 * poweroff_hook.c - TrimUI Brick AXP2202 PMIC Clean Poweroff Module
 * 
 * PURPOSE:
 * This kernel module ensures proper shutdown of the AXP2202 Power Management IC
 * when the TrimUI Brick powers off. Without this module, the battery can overheat
 * during shutdown due to incomplete power rail disconnection.
 * 
 * PROBLEM:
 * The default shutdown sequence leaves some PMIC circuits active, causing:
 * - Battery overheating during and after shutdown
 * - Incomplete power rail shutdown
 * - Potential battery damage from prolonged current draw
 * 
 * SOLUTION:
 * This module hooks the kernel's power-off event (SYS_POWER_OFF only, NOT reboot)
 * and executes a specific sequence of I2C register writes to the AXP2202 PMIC:
 * 1. Disable all interrupts and wake sources
 * 2. Disconnect battery and disable charging circuits
 * 3. Disable coulomb counter and backup battery
 * 4. Enable all shutdown sources and configure immediate poweroff
 * 5. Trigger poweroff and disable all DC-DC converters and LDOs
 * 
 * This sequence ensures complete power disconnection and prevents battery overheating.
 * 
 * Target: TrimUI Brick (kernel 4.9.191, aarch64, AXP2202 PMIC on I2C bus 6)
 * License: GPL v2
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/reboot.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/time.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <generated/utsrelease.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("TrimUI Brick Power-Off Hook");
MODULE_DESCRIPTION("AXP2202 PMIC clean poweroff to prevent battery overheating");
MODULE_VERSION("1.0");

/* Priority for the reboot notifier - higher runs earlier */
#define POWEROFF_HOOK_PRIORITY 200

/* AXP2202 PMIC I2C configuration */
#define I2C_BUS_NUMBER 6
#define AXP2202_I2C_ADDR 0x34

/* File paths to try in order (primary + fallbacks) for logging */
static const char *log_paths[] = {
    "/mnt/SDCARD/.userdata/tg5040/logs/poweroff_hook.log",
    "/mnt/SDCARD/poweroff_hook.log",
    "/tmp/poweroff_hook.log",
    NULL
};

/* Global I2C adapter for AXP2202 communication */
static struct i2c_adapter *i2c_adapter = NULL;

/*
 * I2C register write to AXP2202 PMIC
 * Returns 0 on success, negative error code on failure
 */
static int axp2202_write_reg(u8 reg, u8 value)
{
    struct i2c_msg msg;
    u8 buf[2];
    int ret;

    if (!i2c_adapter) {
        pr_err("poweroff_hook: I2C adapter not initialized\n");
        return -ENODEV;
    }

    buf[0] = reg;
    buf[1] = value;

    msg.addr = AXP2202_I2C_ADDR;
    msg.flags = 0; /* Write */
    msg.len = 2;
    msg.buf = buf;

    ret = i2c_transfer(i2c_adapter, &msg, 1);
    if (ret != 1) {
        pr_err("poweroff_hook: I2C write failed: reg=0x%02x, ret=%d\n", reg, ret);
        return ret < 0 ? ret : -EIO;
    }

    return 0;
}

/*
 * Execute AXP2202 PMIC clean poweroff sequence
 * This sequence ensures complete power disconnection to prevent battery overheating
 */
static void execute_axp2202_poweroff(void)
{
    int i, ret;

    pr_info("poweroff_hook: ===== Starting AXP2202 Clean Poweroff Sequence =====\n");

    /* Step 1: Disable ALL interrupts (registers 0x40-0x47) */
    pr_info("poweroff_hook: Step 1/10 - Disabling all interrupts\n");
    for (i = 0x40; i <= 0x47; i++) {
        ret = axp2202_write_reg(i, 0x00);
        if (ret < 0)
            pr_warn("poweroff_hook: Failed to disable IRQ reg 0x%02x\n", i);
    }

    /* Step 2: Clear ALL interrupt status flags (registers 0x48-0x4F) */
    pr_info("poweroff_hook: Step 2/10 - Clearing all interrupt status\n");
    for (i = 0x48; i <= 0x4F; i++) {
        ret = axp2202_write_reg(i, 0xFF);
        if (ret < 0)
            pr_warn("poweroff_hook: Failed to clear IRQ status reg 0x%02x\n", i);
    }

    /* Step 3: Disable wake sources */
    pr_info("poweroff_hook: Step 3/10 - Disabling wake sources\n");
    axp2202_write_reg(0x26, 0x00);
    axp2202_write_reg(0x27, 0x00);

    /* Step 4: Battery disconnect - critical for preventing overheating */
    pr_info("poweroff_hook: Step 4/10 - Disconnecting battery (prevents overheating)\n");
    axp2202_write_reg(0x28, 0x00);
    msleep(100); /* Wait for battery disconnect to take effect */

    /* Step 5: Disable coulomb counter (battery fuel gauge) */
    pr_info("poweroff_hook: Step 5/10 - Disabling coulomb counter\n");
    axp2202_write_reg(0xB8, 0x00);
    msleep(100);

    /* Step 6: Disable backup battery charging */
    pr_info("poweroff_hook: Step 6/10 - Disabling backup battery\n");
    axp2202_write_reg(0x35, 0x00);
    msleep(100);

    /* Step 7: Enable all shutdown sources */
    pr_info("poweroff_hook: Step 7/10 - Enabling all shutdown sources\n");
    axp2202_write_reg(0x22, 0xFF);
    msleep(50);

    /* Step 8: Configure POK (Power OK) for immediate shutdown */
    pr_info("poweroff_hook: Step 8/10 - Configuring immediate shutdown\n");
    axp2202_write_reg(0x23, 0x00);
    axp2202_write_reg(0x24, 0x00);
    msleep(50);

    /* Step 9: Trigger poweroff command */
    pr_info("poweroff_hook: Step 9/10 - Triggering PMIC poweroff\n");
    axp2202_write_reg(0x10, 0x01);

    /* Step 10: Disable all DC-DC converters and LDOs (complete power cut) */
    pr_info("poweroff_hook: Step 10/10 - Disabling all power rails\n");
    axp2202_write_reg(0x80, 0x00);  /* DCDC control register */
    axp2202_write_reg(0x83, 0x00);  /* DCDC1 */
    axp2202_write_reg(0x84, 0x00);  /* DCDC2 */
    axp2202_write_reg(0x85, 0x00);  /* DCDC3 */
    axp2202_write_reg(0x90, 0x00);  /* LDO control register */
    axp2202_write_reg(0x91, 0x00);  /* LDO1 */
    axp2202_write_reg(0x92, 0x00);  /* LDO2 */
    msleep(200);

    pr_info("poweroff_hook: ===== AXP2202 Poweroff Sequence Complete =====\n");
    pr_info("poweroff_hook: Battery overheating prevention sequence executed\n");
}

/*
 * Kernel file I/O wrapper compatible with kernel 4.9
 * Kernel 4.9 still has set_fs/get_fs, removed in 5.10+
 */
static int write_log_file(const char *path, const char *content, size_t len)
{
	struct file *filp;
	mm_segment_t old_fs;
	loff_t pos = 0;
	ssize_t written;
	int ret;

	/* Open file for writing, create if doesn't exist */
	filp = filp_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (IS_ERR(filp)) {
		ret = PTR_ERR(filp);
		pr_warn("poweroff_hook: Failed to open %s: %d\n", path, ret);
		return ret;
	}

	/* Set kernel segment */
	old_fs = get_fs();
	set_fs(KERNEL_DS);

	/* Write content using vfs_write */
	written = vfs_write(filp, content, len, &pos);
	
	/* Restore user segment */
	set_fs(old_fs);

	if (written < 0) {
		ret = written;
		pr_err("poweroff_hook: Write failed to %s: %zd\n", path, written);
	} else if (written != len) {
		ret = -EIO;
		pr_err("poweroff_hook: Partial write to %s: %zd of %zu bytes\n",
		       path, written, len);
	} else {
		ret = 0;
		pr_info("poweroff_hook: Successfully wrote %zu bytes to %s\n", len, path);
	}

	/* Sync to disk */
	vfs_fsync(filp, 0);

	/* Close file */
	filp_close(filp, NULL);

	return ret;
}

/*
 * Try to write log file to multiple paths (fallback strategy)
 */
static int try_write_log(void)
{
    char message[256];
    struct timespec ts;
    struct tm tm;
    int i, ret = -1;

    /* Get current time */
    getnstimeofday(&ts);
    time_to_tm(ts.tv_sec, 0, &tm);

    /* Format message with detailed information */
    snprintf(message, sizeof(message),
             "TrimUI Brick AXP2202 Clean Poweroff\n"
             "Timestamp: %04ld-%02d-%02d %02d:%02d:%02d UTC\n"
             "Event: SYS_POWER_OFF\n"
             "Action: Executed AXP2202 PMIC shutdown sequence\n"
             "Purpose: Prevent battery overheating via complete power disconnection\n"
             "Module: poweroff_hook v1.0\n",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);

    /* Try each path in order */
    for (i = 0; log_paths[i] != NULL; i++) {
        ret = write_log_file(log_paths[i], message, strlen(message));
        if (ret == 0) {
            printk(KERN_INFO "poweroff_hook: Successfully wrote log to %s\n", 
                   log_paths[i]);
            return 0;
        }
        printk(KERN_DEBUG "poweroff_hook: Failed to write to %s (error %d)\n", 
               log_paths[i], ret);
    }

    printk(KERN_WARNING "poweroff_hook: Failed to write log to any path\n");
    return ret;
}

/*
 * Write module load confirmation to log file
 */
static int write_load_log(void)
{
    char message[512];
    struct timespec ts;
    struct tm tm;
    int i, ret = -1;

    /* Get current time */
    getnstimeofday(&ts);
    time_to_tm(ts.tv_sec, 0, &tm);

    /* Format message */
    snprintf(message, sizeof(message),
             "TrimUI Brick AXP2202 Clean Poweroff Module - LOADED\n"
             "Timestamp: %04ld-%02d-%02d %02d:%02d:%02d UTC\n"
             "Status: Module successfully loaded and active\n"
             "I2C Bus: %d, PMIC Address: 0x%02x\n"
             "Priority: %d\n"
             "Purpose: Prevent battery overheating on shutdown\n"
             "Module: poweroff_hook v1.0\n"
             "Note: Will execute ONLY on SYS_POWER_OFF (not reboot)\n",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec,
             I2C_BUS_NUMBER, AXP2202_I2C_ADDR, POWEROFF_HOOK_PRIORITY);

    /* Try each path in order */
    for (i = 0; log_paths[i] != NULL; i++) {
        ret = write_log_file(log_paths[i], message, strlen(message));
        if (ret == 0) {
            printk(KERN_INFO "poweroff_hook: Successfully wrote load log to %s\n", 
                   log_paths[i]);
            return 0;
        }
        printk(KERN_DEBUG "poweroff_hook: Failed to write load log to %s (error %d)\n", 
               log_paths[i], ret);
    }

    printk(KERN_WARNING "poweroff_hook: Failed to write load log to any path\n");
    return ret;
}

/*
 * Reboot notifier callback
 * This is called when the system is shutting down or rebooting.
 * CRITICAL: Only responds to SYS_POWER_OFF, NOT SYS_RESTART or SYS_HALT.
 * This ensures the battery disconnect sequence only runs on actual poweroff,
 * not during reboots where the system needs to come back up.
 */
static int poweroff_notifier_callback(struct notifier_block *nb, 
                                      unsigned long event, void *data)
{
    /* Only act on power-off, ignore reboot/halt/restart */
    if (event != SYS_POWER_OFF) {
        printk(KERN_DEBUG "poweroff_hook: Ignoring event %lu (not SYS_POWER_OFF)\n", 
               event);
        return NOTIFY_DONE;
    }

    printk(KERN_INFO "poweroff_hook: ============================================\n");
    printk(KERN_INFO "poweroff_hook: SYS_POWER_OFF event detected\n");
    printk(KERN_INFO "poweroff_hook: Initiating clean poweroff to prevent battery overheating\n");
    printk(KERN_INFO "poweroff_hook: ============================================\n");

    /* Execute AXP2202 PMIC clean poweroff sequence */
    execute_axp2202_poweroff();

    /* Write log file for debugging/verification */
    try_write_log();

    printk(KERN_INFO "poweroff_hook: Clean poweroff sequence completed\n");

    /* Note: emergency_sync() not available in this kernel, 
     * but the kernel's shutdown path will handle syncing */

    return NOTIFY_DONE;
}

/* Notifier block structure */
static struct notifier_block poweroff_notifier = {
    .notifier_call = poweroff_notifier_callback,
    .priority = POWEROFF_HOOK_PRIORITY,
};

/*
 * Module initialization
 */
static int __init poweroff_hook_init(void)
{
    int ret;

    printk(KERN_INFO "poweroff_hook: ============================================\n");
    printk(KERN_INFO "poweroff_hook: TrimUI Brick AXP2202 Clean Poweroff Module\n");
    printk(KERN_INFO "poweroff_hook: ============================================\n");
    printk(KERN_INFO "poweroff_hook: Target kernel: %s\n", UTS_RELEASE);
    printk(KERN_INFO "poweroff_hook: Module version: 1.0\n");
    printk(KERN_INFO "poweroff_hook: Purpose: Prevent battery overheating on shutdown\n");

    /* Get I2C adapter for AXP2202 communication */
    i2c_adapter = i2c_get_adapter(I2C_BUS_NUMBER);
    if (!i2c_adapter) {
        printk(KERN_ERR "poweroff_hook: Failed to get I2C adapter %d\n", I2C_BUS_NUMBER);
        printk(KERN_ERR "poweroff_hook: Cannot communicate with AXP2202 PMIC\n");
        return -ENODEV;
    }
    printk(KERN_INFO "poweroff_hook: I2C adapter %d acquired for AXP2202 (addr 0x%02x)\n",
           I2C_BUS_NUMBER, AXP2202_I2C_ADDR);

    /* Register reboot notifier with high priority */
    ret = register_reboot_notifier(&poweroff_notifier);
    if (ret) {
        printk(KERN_ERR "poweroff_hook: Failed to register reboot notifier (error %d)\n", 
               ret);
        i2c_put_adapter(i2c_adapter);
        i2c_adapter = NULL;
        return ret;
    }

    printk(KERN_INFO "poweroff_hook: Successfully registered power-off hook (priority %d)\n",
           POWEROFF_HOOK_PRIORITY);
    printk(KERN_INFO "poweroff_hook: Will execute ONLY on SYS_POWER_OFF (not reboot)\n");
    printk(KERN_INFO "poweroff_hook: AXP2202 shutdown sequence will prevent battery overheating\n");
    printk(KERN_INFO "poweroff_hook: Log file will be written to %s (with fallbacks)\n", 
           log_paths[0]);
    printk(KERN_INFO "poweroff_hook: ============================================\n");

    /* Write load confirmation to log file for peace of mind */
    write_load_log();

    return 0;
}

/*
 * Module cleanup
 */
static void __exit poweroff_hook_exit(void)
{
    printk(KERN_INFO "poweroff_hook: ============================================\n");
    printk(KERN_INFO "poweroff_hook: Unloading AXP2202 clean poweroff module\n");

    /* Unregister reboot notifier */
    unregister_reboot_notifier(&poweroff_notifier);
    printk(KERN_INFO "poweroff_hook: Power-off hook unregistered\n");

    /* Release I2C adapter */
    if (i2c_adapter) {
        i2c_put_adapter(i2c_adapter);
        i2c_adapter = NULL;
        printk(KERN_INFO "poweroff_hook: I2C adapter released\n");
    }

    printk(KERN_INFO "poweroff_hook: Module unloaded - battery overheating prevention disabled\n");
    printk(KERN_INFO "poweroff_hook: WARNING: Battery may overheat on next poweroff!\n");
    printk(KERN_INFO "poweroff_hook: ============================================\n");
}

module_init(poweroff_hook_init);
module_exit(poweroff_hook_exit);
