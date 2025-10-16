/*
 * poweroff_hook.c - Kernel module that runs only on system power-off
 * 
 * This module registers a reboot notifier that triggers only on SYS_POWER_OFF
 * events (not on reboot or halt). It writes a log file to the filesystem as
 * proof of concept before the system powers off.
 * 
 * Target: TrimUI Brick (kernel 4.9.191, aarch64)
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
#include <generated/utsrelease.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("TrimUI Brick Power-Off Hook");
MODULE_DESCRIPTION("Kernel module that runs only on power-off");
MODULE_VERSION("1.0");

/* Priority for the reboot notifier - higher runs earlier */
#define POWEROFF_HOOK_PRIORITY 128

/* File paths to try in order (primary + fallbacks) */
static const char *log_paths[] = {
    "/poweroff_log.txt",
    "/data/poweroff_log.txt",
    "/mnt/SDCARD/poweroff_log.txt",
    "/tmp/poweroff_log.txt",
    NULL
};

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

    /* Format message */
    snprintf(message, sizeof(message),
             "TrimUI Brick Power-Off Hook\n"
             "Timestamp: %04ld-%02d-%02d %02d:%02d:%02d UTC\n"
             "Event: SYS_POWER_OFF\n"
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
 * Reboot notifier callback
 * This is called when the system is shutting down or rebooting
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

    printk(KERN_INFO "poweroff_hook: SYS_POWER_OFF event detected\n");
    printk(KERN_INFO "poweroff_hook: System is about to power off\n");

    /* Write log file */
    try_write_log();

    printk(KERN_INFO "poweroff_hook: Power-off hook completed\n");

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

    printk(KERN_INFO "poweroff_hook: Initializing power-off hook module\n");
    printk(KERN_INFO "poweroff_hook: Target kernel: %s\n", UTS_RELEASE);
    printk(KERN_INFO "poweroff_hook: Module version: 1.0\n");

    /* Register reboot notifier */
    ret = register_reboot_notifier(&poweroff_notifier);
    if (ret) {
        printk(KERN_ERR "poweroff_hook: Failed to register reboot notifier (error %d)\n", 
               ret);
        return ret;
    }

    printk(KERN_INFO "poweroff_hook: Successfully registered power-off hook\n");
    printk(KERN_INFO "poweroff_hook: Will trigger only on SYS_POWER_OFF events\n");
    printk(KERN_INFO "poweroff_hook: Log file will be written to %s (with fallbacks)\n", 
           log_paths[0]);

    return 0;
}

/*
 * Module cleanup
 */
static void __exit poweroff_hook_exit(void)
{
    printk(KERN_INFO "poweroff_hook: Unloading power-off hook module\n");

    /* Unregister reboot notifier */
    unregister_reboot_notifier(&poweroff_notifier);

    printk(KERN_INFO "poweroff_hook: Power-off hook unregistered\n");
}

module_init(poweroff_hook_init);
module_exit(poweroff_hook_exit);
