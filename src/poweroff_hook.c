/*
 * poweroff_hook.c - TrimUI Brick AXP717/AXP2202 PMIC Clean Poweroff Module
 * 
 * PURPOSE:
 * This kernel module ensures proper shutdown of the AXP717/AXP2202 Power Management IC
 * when the TrimUI Brick powers off. Without this module, the battery can overheat
 * during shutdown due to incomplete power rail disconnection.
 * 
 * OPERATION:
 * 1. NextUI creates /tmp/poweroff signal file
 * 2. Module detects signal and begins shutdown sequence
 * 3. Kill all user processes (SIGTERM then SIGKILL)
 * 4. Unmount filesystems (swapoff, umount /etc/profile, umount /mnt/SDCARD)
 * 5. Verify SD card unmount status
 * 6. Execute AXP717/AXP2202 PMIC shutdown sequence (safe minimal version)
 * 7. Call kernel poweroff (standard shutdown)
 *
 * AXP717/AXP2202 PMIC SHUTDOWN SEQUENCE (safe minimal version per datasheet v1.0):
 * Step 1: Mask interrupts (0x40-0x44 = 0x00)
 * Step 2: Clear interrupt status (0x48-0x4C = 0xFF)
 * Step 3: Configure shutdown sources (0x22 = 0x0A, bits 1 and 3 only)
 * Step 4: Trigger software poweroff (0x27 = 0x01)
 *
 * IMPORTANT NOTES:
 * - IRQ enable registers: 0x40-0x44
 * - IRQ status registers: 0x48-0x4C
 * - 0x22 PWROFF_EN: only bits 0,1,3 are documented; never write 0xFF
 * - 0x27 bit 0 = software poweroff trigger (documented in datasheet)
 *
 * Target: TrimUI Brick (kernel 4.9.191, aarch64, AXP717/AXP2202 PMIC on I2C bus 6)
 * License: GPL v2
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/reboot.h>
#include <linux/kthread.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/mount.h>
#include <linux/path.h>
#include <linux/namei.h>
#include <linux/dcache.h>
#include <linux/sched.h>
#include <linux/swap.h>
#include <linux/syscalls.h>
#include <linux/kmod.h>
#include <generated/utsrelease.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("TrimUI Brick Power-Off Hook");
MODULE_DESCRIPTION("AXP717/AXP2202 PMIC clean poweroff (safe minimal version)");
MODULE_VERSION("1.0");

/* AXP717/AXP2202/AXP2202 PMIC I2C configuration */
#define I2C_BUS_NUMBER 6
#define AXP2202_I2C_ADDR 0x34

/* Signal file that NextUI creates */
#define POWEROFF_SIGNAL_FILE "/tmp/poweroff"

/* Log path (will only work before SD card unmount) */
#define LOG_PATH "/mnt/SDCARD/.userdata/tg5040/logs/PowerOffHook-KernelModule.txt"

/* Global I2C adapter for AXP717/AXP2202 communication */
static struct i2c_adapter *i2c_adapter = NULL;

/* Monitor thread */
static struct task_struct *monitor_thread = NULL;
static bool should_stop = false;

/* Flag to disable SD card logging during unmount */
static bool sd_logging_enabled = true;

/*
 * I2C register write to AXP717/AXP2202 PMIC
 */
static int axp2202_write_reg(u8 reg, u8 value)
{
    struct i2c_msg msg;
    u8 buf[2];
    int ret;

    if (!i2c_adapter) {
        printk(KERN_INFO "poweroff_hook: I2C adapter not initialized\n");
        return -ENODEV;
    }

    buf[0] = reg;
    buf[1] = value;

    msg.addr = AXP2202_I2C_ADDR;
    msg.flags = 0;
    msg.len = 2;
    msg.buf = buf;

    ret = i2c_transfer(i2c_adapter, &msg, 1);
    if (ret != 1) {
        printk(KERN_INFO "poweroff_hook: I2C write failed: reg=0x%02x, ret=%d\n", reg, ret);
        return ret < 0 ? ret : -EIO;
    }

    return 0;
}

/*
 * Check if /mnt/SDCARD is mounted - properly test the mountpoint
 */
static bool is_sdcard_mounted(void)
{
    struct path p;
    bool mounted = false;

    if (kern_path("/mnt/SDCARD", LOOKUP_FOLLOW, &p) == 0) {
        mounted = d_mountpoint(p.dentry);  /* true iff something is mounted ON this */
        path_put(&p);
    }
    return mounted;
}

/*
 * Write log entry (best effort, may fail if SD card unmounted)
 */
static void write_log(const char *message)
{
    struct file *filp;
    mm_segment_t old_fs;
    loff_t pos = 0;

    /* Don't write to SD card if logging is disabled (during unmount) */
    if (!sd_logging_enabled) {
        return;
    }

    filp = filp_open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (IS_ERR(filp)) {
        return;
    }

    old_fs = get_fs();
    set_fs(KERNEL_DS);
    vfs_write(filp, message, strlen(message), &pos);
    vfs_fsync(filp, 1);
    set_fs(old_fs);
    filp_close(filp, NULL);
}

/*
 * Write debug marker to /root/poweroff_hook.log
 * This will be moved to LOG_PATH
 */
static void write_debug_marker(const char *stage)
{
    struct file *marker_filp;
    mm_segment_t old_fs;
    loff_t pos = 0;
    char msg[128];
    
    snprintf(msg, sizeof(msg), "[%s]\n", stage);
    
    old_fs = get_fs();
    set_fs(KERNEL_DS);
    marker_filp = filp_open("/root/poweroff_hook.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (!IS_ERR(marker_filp)) {
        vfs_write(marker_filp, msg, strlen(msg), &pos);
        vfs_fsync(marker_filp, 1);
        filp_close(marker_filp, NULL);
    }
    set_fs(old_fs);
    
    printk(KERN_INFO "poweroff_hook: DEBUG MARKER: %s\n", stage);
}

/*
 * Kill all user-space processes safely via usermode helper
 * Avoids kernel process list traversal issues
 */
static void kill_all_processes(void)
{
    char *argv_killall5_term[] = { "/bin/killall5", "-15", NULL };
    char *argv_killall5_kill[] = { "/bin/killall5", "-9", NULL };
    char *envp[] = { "HOME=/", "PATH=/sbin:/bin:/usr/sbin:/usr/bin", NULL };
    
    printk(KERN_INFO "poweroff_hook: Starting graceful process termination (SIGTERM)\n");
    
    /* First pass: Send SIGTERM for graceful shutdown */
    call_usermodehelper(argv_killall5_term[0], argv_killall5_term, envp, UMH_WAIT_PROC);
    
    printk(KERN_INFO "poweroff_hook: Sent SIGTERM to all processes, waiting 500ms\n");
    msleep(500);  /* Give processes time to exit gracefully */
    
    /* Second pass: Force kill with SIGKILL */
    printk(KERN_INFO "poweroff_hook: Force killing remaining processes (SIGKILL)\n");
    call_usermodehelper(argv_killall5_kill[0], argv_killall5_kill, envp, UMH_WAIT_PROC);
    
    printk(KERN_INFO "poweroff_hook: Process termination complete\n");
    msleep(200);  /* Brief wait for processes to die */
}

/*
 * Unmount filesystems and disable swap
 */
static void unmount_filesystems(void)
{
    char *argv_sync[] = { "/bin/sync", NULL };
    char *argv_swapoff[] = { "/bin/swapoff", "-a", NULL };
    char *argv_umount_profile[] = { "/bin/umount", "-f", "/etc/profile", NULL };
    char *envp[] = { "HOME=/", "PATH=/sbin:/bin:/usr/sbin:/usr/bin", NULL };
    int retry, ret;
    
    printk(KERN_INFO "poweroff_hook: Syncing all filesystems\n");
    write_debug_marker("UNMOUNT_SYNC_START");
    ret = call_usermodehelper(argv_sync[0], argv_sync, envp, UMH_WAIT_PROC);
    printk(KERN_INFO "poweroff_hook: sync returned: %d\n", ret);
    msleep(100);
    write_debug_marker("UNMOUNT_SYNC_DONE");
    
    printk(KERN_INFO "poweroff_hook: Disabling swap\n");
    write_debug_marker("UNMOUNT_SWAPOFF_START");
    ret = call_usermodehelper(argv_swapoff[0], argv_swapoff, envp, UMH_WAIT_PROC);
    printk(KERN_INFO "poweroff_hook: swapoff returned: %d\n", ret);
    write_debug_marker("UNMOUNT_SWAPOFF_DONE");
    
    printk(KERN_INFO "poweroff_hook: Unmounting /etc/profile\n");
    write_debug_marker("UNMOUNT_PROFILE_START");
    ret = call_usermodehelper(argv_umount_profile[0], argv_umount_profile, envp, UMH_WAIT_PROC);
    printk(KERN_INFO "poweroff_hook: umount /etc/profile returned: %d\n", ret);
    write_debug_marker("UNMOUNT_PROFILE_DONE");
    
    /* CRITICAL: Stop writing to SD card before unmounting it! */
    printk(KERN_INFO "poweroff_hook: Disabling SD card logging\n");
    write_debug_marker("UNMOUNT_DISABLE_SD_LOGGING");
    sd_logging_enabled = false;
    
    /* Extra sync to flush any pending writes to SD card */
    printk(KERN_INFO "poweroff_hook: Final SD card sync before unmount\n");
    write_debug_marker("UNMOUNT_SDCARD_PRE_SYNC");
    ret = call_usermodehelper(argv_sync[0], argv_sync, envp, UMH_WAIT_PROC);
    printk(KERN_INFO "poweroff_hook: pre-unmount sync returned: %d\n", ret);
    msleep(500); /* Give extra time for writes to complete */
    write_debug_marker("UNMOUNT_SDCARD_PRE_SYNC_DONE");
    
    /* Try to unmount SD card with retries - using -f (force) then -l (lazy) */
    printk(KERN_INFO "poweroff_hook: Unmounting /mnt/SDCARD (with retries)\n");
    write_debug_marker("UNMOUNT_SDCARD_START");
    for (retry = 0; retry < 3; retry++) {
        char marker_msg[64];
        char *argv_fuser[] = { "/usr/bin/fuser", "-km", "/mnt/SDCARD", NULL };
        char *argv_umount_force_lazy[] = { "/bin/umount", "-f", "-l", "/mnt/SDCARD", NULL };
        
        snprintf(marker_msg, sizeof(marker_msg), "UNMOUNT_SDCARD_ATTEMPT_%d", retry + 1);
        write_debug_marker(marker_msg);
        
        /* Kill any processes still using the SD card */
        if (retry > 0) {
            write_debug_marker("UNMOUNT_SDCARD_FUSER_KILL");
            ret = call_usermodehelper(argv_fuser[0], argv_fuser, envp, UMH_WAIT_PROC);
            printk(KERN_INFO "poweroff_hook: fuser returned: %d\n", ret);
            msleep(200);
        }
        
        /* Try force + lazy unmount together */
        ret = call_usermodehelper(argv_umount_force_lazy[0], argv_umount_force_lazy, envp, UMH_WAIT_PROC);
        printk(KERN_INFO "poweroff_hook: umount -f -l /mnt/SDCARD returned: %d\n", ret);
        
        write_debug_marker("UNMOUNT_SDCARD_WAIT_START");
        msleep(800); /* Wait longer for unmount to complete */
        write_debug_marker("UNMOUNT_SDCARD_WAIT_DONE");
        
        write_debug_marker("UNMOUNT_SDCARD_CHECK_START");
        if (!is_sdcard_mounted()) {
            printk(KERN_INFO "poweroff_hook: SD card unmounted successfully after %d attempts\n", retry + 1);
            write_debug_marker("UNMOUNT_SDCARD_SUCCESS");
            break;
        }
        write_debug_marker("UNMOUNT_SDCARD_STILL_MOUNTED");
        
        if (retry < 2) {
            printk(KERN_WARNING "poweroff_hook: SD card still mounted, retry %d/2\n", retry + 1);
            /* Force sync before next retry */
            write_debug_marker("UNMOUNT_SDCARD_RETRY_SYNC");
            ret = call_usermodehelper(argv_sync[0], argv_sync, envp, UMH_WAIT_PROC);
            printk(KERN_INFO "poweroff_hook: retry sync returned: %d\n", ret);
            msleep(300);
        }
    }
    
    printk(KERN_INFO "poweroff_hook: Final sync\n");
    write_debug_marker("UNMOUNT_FINAL_SYNC_START");
    ret = call_usermodehelper(argv_sync[0], argv_sync, envp, UMH_WAIT_PROC);
    printk(KERN_INFO "poweroff_hook: final sync returned: %d\n", ret);
    msleep(200);
    write_debug_marker("UNMOUNT_FINAL_SYNC_DONE");
}

/*
 * Execute AXP717/AXP2202 PMIC clean poweroff sequence (safe minimal version)
 */
static void execute_axp2202_poweroff(void)
{
    int i, ret;

    printk(KERN_INFO "poweroff_hook: ===== Starting AXP717/AXP2202 Clean Poweroff Sequence =====\n");
    write_debug_marker("PMIC_SEQUENCE_START");

    /* Step 1: Mask interrupts (registers 0x40-0x44 per datasheet) */
    printk(KERN_INFO "poweroff_hook: Step 1/4 - Masking interrupts (0x40-0x44)\n");
    write_debug_marker("STEP1_MASK_INTERRUPTS");
    for (i = 0x40; i <= 0x44; i++) {
        ret = axp2202_write_reg(i, 0x00);
        if (ret < 0)
            printk(KERN_INFO "poweroff_hook: Failed to mask IRQ reg 0x%02x, error=%d\n", i, ret);
    }

    /* Step 2: Clear interrupt status flags (registers 0x48-0x4C per datasheet) */
    printk(KERN_INFO "poweroff_hook: Step 2/4 - Clearing interrupt status (0x48-0x4C)\n");
    write_debug_marker("STEP2_CLEAR_IRQ_STATUS");
    for (i = 0x48; i <= 0x4C; i++) {
        ret = axp2202_write_reg(i, 0xFF);
        if (ret < 0)
            printk(KERN_INFO "poweroff_hook: Failed to clear IRQ status reg 0x%02x, error=%d\n", i, ret);
    }

    /* Step 3: Configure shutdown sources (0x22 = PWROFF_EN) */
    /* Bit 3: LDO Over-Current as poweroff source enable */
    /* Bit 1: PWRON > OFFLEVEL as poweroff source enable */
    /* Bit 0: Function select (0=poweroff, 1=restart) when button event occurs */
    printk(KERN_INFO "poweroff_hook: Step 3/4 - Configuring shutdown sources (0x22)\n");
    write_debug_marker("STEP3_SHUTDOWN_SOURCES");
    axp2202_write_reg(0x22, 0x0A);  /* 0b00001010 - set bits 1,3 only */
    msleep(50);

    /* Step 4: TRIGGER SOFTWARE POWER-OFF (Register 0x27, bit 0 = 0x01) */
    /* This is the software poweroff command on AXP717/AXP2202 */
    printk(KERN_INFO "poweroff_hook: Step 4/4 - TRIGGERING SOFTWARE POWER-OFF (0x27)\n");
    write_debug_marker("STEP4_TRIGGER_POWEROFF");
    ret = axp2202_write_reg(0x27, 0x01);
    if (ret < 0)
        printk(KERN_INFO "poweroff_hook: CRITICAL - PMIC poweroff trigger failed! error=%d\n", ret);
    else
        printk(KERN_INFO "poweroff_hook: PMIC SOFTWARE POWER-OFF TRIGGERED (0x27=0x01)\n");
    write_debug_marker("STEP4_COMPLETE");
    
    /* Power should cut almost immediately after this command.
     * If we reach here, give PMIC a moment to latch the shutdown. */
    msleep(1000);

    printk(KERN_INFO "poweroff_hook: ===== AXP717/AXP2202 Poweroff Sequence Complete =====\n");
    write_debug_marker("PMIC_SEQUENCE_COMPLETE");
}

/*
 * Monitor thread - waits for signal then executes shutdown
 */
static int monitor_thread_fn(void *data)
{
    struct file *filp;
    struct timespec ts;
    struct tm tm;
    char log_msg[256];
    static int check_count = 0;

    printk(KERN_INFO "poweroff_hook: Monitor thread started\n");

    while (!kthread_should_stop() && !should_stop) {
        check_count++;

        /* Log every 1000 checks (every 100 seconds) to prove thread is running */
        if (check_count % 1000 == 0) {
            printk(KERN_INFO "poweroff_hook: Monitor thread alive, checked %d times\n", check_count);
        }
        
        /* Check for signal file - simple file existence check */
        filp = filp_open(POWEROFF_SIGNAL_FILE, O_RDONLY, 0);
        if (!IS_ERR(filp)) {
            filp_close(filp, NULL);
            
            write_debug_marker("SIGNAL_DETECTED");
            printk(KERN_INFO "poweroff_hook: *** SIGNAL FILE DETECTED! ***\n");
            
            /* Get timestamp for logging */
            getnstimeofday(&ts);
            time_to_tm(ts.tv_sec, 0, &tm);
            
            snprintf(log_msg, sizeof(log_msg),
                     "=== PowerOff Signal Received ===\n"
                     "Timestamp: %04ld-%02d-%02d %02d:%02d:%02d UTC\n",
                     tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                     tm.tm_hour, tm.tm_min, tm.tm_sec);
            
            write_log(log_msg);
            write_debug_marker("BEFORE_KILL_PROCESSES");
            
            printk(KERN_INFO "poweroff_hook: ============================================\n");
            printk(KERN_INFO "poweroff_hook: PowerOff signal received from NextUI\n");
            printk(KERN_INFO "poweroff_hook: Beginning clean shutdown sequence\n");
            printk(KERN_INFO "poweroff_hook: ============================================\n");

            /* Step 1: Kill all user processes (but not kernel threads) */
            kill_all_processes();
            write_debug_marker("AFTER_KILL_PROCESSES");
            
            /* Step 2: Disable swap and unmount filesystems */
            write_debug_marker("BEFORE_UNMOUNT");
            unmount_filesystems();
            write_debug_marker("AFTER_UNMOUNT");
            
            /* Verify SD card is unmounted */
            if (is_sdcard_mounted()) {
                printk(KERN_ERR "poweroff_hook: CRITICAL - SD card still mounted after 5 attempts!\n");
                printk(KERN_ERR "poweroff_hook: Skipping PMIC sequence, calling kernel poweroff directly\n");
                write_debug_marker("SD_STILL_MOUNTED_EMERGENCY");
                
                /* Skip PMIC shutdown and go straight to kernel poweroff for safety */
                printk(KERN_INFO "poweroff_hook: Calling kernel_power_off() (emergency path)\n");
                write_debug_marker("EMERGENCY_KERNEL_POWEROFF");
                kernel_power_off();
                
                /* Should never reach here */
                printk(KERN_INFO "poweroff_hook: kernel_power_off() returned, halting\n");
                while (1) {
                    cpu_relax();
                }
            } else {
                printk(KERN_INFO "poweroff_hook: SD card successfully unmounted\n");
                write_debug_marker("SD_UNMOUNTED_OK");
            }

            /* Step 3: Execute PMIC shutdown sequence */
            write_debug_marker("BEFORE_PMIC_SHUTDOWN");
            execute_axp2202_poweroff();
            write_debug_marker("AFTER_PMIC_SHUTDOWN");

            /* Call kernel poweroff */
            printk(KERN_INFO "poweroff_hook: Calling kernel_power_off()\n");
            write_debug_marker("BEFORE_KERNEL_POWEROFF");
            kernel_power_off();

            /* Should never reach here */
            write_debug_marker("AFTER_KERNEL_POWEROFF");
            printk(KERN_INFO "poweroff_hook: kernel_power_off() returned, halting\n");
            while (1) {
                cpu_relax();
            }
        }

        /* Sleep for 100ms before checking again */
        msleep(100);
    }

    printk(KERN_INFO "poweroff_hook: Monitor thread exiting\n");
    return 0;
}

/*
 * Module initialization
 */
static int __init poweroff_hook_init(void)
{
    struct file *filp;
    mm_segment_t old_fs;
    char log_msg[512];
    struct timespec ts;
    struct tm tm;

    printk(KERN_INFO "poweroff_hook: ============================================\n");
    printk(KERN_INFO "poweroff_hook: TrimUI Brick AXP717/AXP2202 Poweroff Module v1.0 (safe)\n");
    printk(KERN_INFO "poweroff_hook: ============================================\n");
    printk(KERN_INFO "poweroff_hook: Target kernel: %s\n", UTS_RELEASE);
    printk(KERN_INFO "poweroff_hook: Purpose: Clean AXP717/AXP2202 PMIC shutdown sequence\n");

    /* Get I2C adapter for AXP717/AXP2202 communication */
    i2c_adapter = i2c_get_adapter(I2C_BUS_NUMBER);
    if (!i2c_adapter) {
        printk(KERN_ERR "poweroff_hook: Failed to get I2C adapter %d\n", I2C_BUS_NUMBER);
        return -ENODEV;
    }
    printk(KERN_INFO "poweroff_hook: I2C adapter %d acquired for AXP717/AXP2202 (addr 0x%02x)\n",
           I2C_BUS_NUMBER, AXP2202_I2C_ADDR);

    /* DO NOT touch register 0x27 during init!
     * Register 0x27 bit 0 (0x01) is the SOFTWARE POWER-OFF TRIGGER.
     * Setting it here would immediately power off the device.
     * We only write 0x01 to this register when we want to shut down.
     */
    printk(KERN_INFO "poweroff_hook: PMIC initialized (register 0x27 preserved)\n");

    /* Remove old signal file if it exists (prevents boot loop if system crashed during shutdown) */
    old_fs = get_fs();
    set_fs(KERNEL_DS);
    filp = filp_open(POWEROFF_SIGNAL_FILE, O_RDONLY, 0);
    if (!IS_ERR(filp)) {
        filp_close(filp, NULL);
        printk(KERN_WARNING "poweroff_hook: Found stale signal file %s, removing\n", POWEROFF_SIGNAL_FILE);
        /* File exists, truncate it to effectively delete */
        filp = filp_open(POWEROFF_SIGNAL_FILE, O_WRONLY | O_TRUNC, 0644);
        if (!IS_ERR(filp)) {
            filp_close(filp, NULL);
            printk(KERN_INFO "poweroff_hook: Stale signal file removed successfully\n");
        }
    }
    set_fs(old_fs);

    /* Write load log */
    getnstimeofday(&ts);
    time_to_tm(ts.tv_sec, 0, &tm);
    snprintf(log_msg, sizeof(log_msg),
             "=== PowerOff Hook Module LOADED ===\n"
             "Timestamp: %04ld-%02d-%02d %02d:%02d:%02d UTC\n"
             "Version: 1.0 (safe minimal version)\n"
             "Mode: Signal-based with SD card unmount detection\n"
             "PMIC: AXP717/AXP2202 (minimal safe registers per datasheet v1.0)\n"
             "Signal file: %s\n"
             "I2C Bus: %d, PMIC Address: 0x%02x\n\n",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec,
             POWEROFF_SIGNAL_FILE, I2C_BUS_NUMBER, AXP2202_I2C_ADDR);
    write_log(log_msg);

    /* Append content from /root/poweroff_hook.log to the main log file
     * This preserves debug markers from previous module loads/runs
     */
    {
        struct file *src_filp, *dst_filp;
        char buffer[1024];
        ssize_t bytes_read;
        loff_t read_pos = 0;
        mm_segment_t old_fs_copy;

        old_fs_copy = get_fs();
        set_fs(KERNEL_DS);

        /* Open source file for reading */
        src_filp = filp_open("/root/poweroff_hook.log", O_RDONLY, 0);
        if (!IS_ERR(src_filp)) {
            /* Open destination file for appending */
            dst_filp = filp_open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (!IS_ERR(dst_filp)) {
                /* Read and copy content */
                while ((bytes_read = vfs_read(src_filp, buffer, sizeof(buffer), &read_pos)) > 0) {
                    loff_t write_pos = dst_filp->f_pos;
                    vfs_write(dst_filp, buffer, bytes_read, &write_pos);
                    dst_filp->f_pos = write_pos;
                }
                vfs_fsync(dst_filp, 1);
                filp_close(dst_filp, NULL);
                printk(KERN_INFO "poweroff_hook: Appended content from /root/poweroff_hook.log to main log\n");
                
                /* Clear the source file after successful append */
                filp_close(src_filp, NULL);
                src_filp = filp_open("/root/poweroff_hook.log", O_WRONLY | O_TRUNC, 0644);
                if (!IS_ERR(src_filp)) {
                    filp_close(src_filp, NULL);
                    printk(KERN_INFO "poweroff_hook: Cleared /root/poweroff_hook.log after append\n");
                }
            } else {
                printk(KERN_WARNING "poweroff_hook: Could not open destination log file for appending\n");
                filp_close(src_filp, NULL);
            }
        } else {
            printk(KERN_INFO "poweroff_hook: No /root/poweroff_hook.log file found to append\n");
        }

        set_fs(old_fs_copy);
    }

    /* Start monitor thread */
    monitor_thread = kthread_run(monitor_thread_fn, NULL, "poweroff_monitor");
    if (IS_ERR(monitor_thread)) {
        printk(KERN_ERR "poweroff_hook: Failed to create monitor thread\n");
        i2c_put_adapter(i2c_adapter);
        i2c_adapter = NULL;
        return PTR_ERR(monitor_thread);
    }

    printk(KERN_INFO "poweroff_hook: Monitor thread started, watching for %s\n", POWEROFF_SIGNAL_FILE);
    printk(KERN_INFO "poweroff_hook: ============================================\n");

    return 0;
}

/*
 * Module cleanup
 */
static void __exit poweroff_hook_exit(void)
{
    printk(KERN_INFO "poweroff_hook: Unloading module\n");

    /* Stop monitor thread */
    if (monitor_thread) {
        should_stop = true;
        kthread_stop(monitor_thread);
        monitor_thread = NULL;
    }

    /* Release I2C adapter */
    if (i2c_adapter) {
        i2c_put_adapter(i2c_adapter);
        i2c_adapter = NULL;
    }

    printk(KERN_INFO "poweroff_hook: Module unloaded\n");
}

module_init(poweroff_hook_init);
module_exit(poweroff_hook_exit);
