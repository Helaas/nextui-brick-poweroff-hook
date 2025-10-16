// poweroff_daemon.c - Daemon that monitors for shutdown signals
// and executes proper poweroff sequence for TrimUI Brick
//
// This daemon runs in the background and intercepts shutdown signals
// to ensure the AXP2202 PMIC poweroff sequence is executed.
//
// Build: gcc -O2 -Wall -Wextra -static -o poweroff_daemon poweroff_daemon.c
// Usage: poweroff_daemon &

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef I2C_SLAVE_FORCE
#define I2C_SLAVE_FORCE 0x0706
#endif

#define I2C_BUS "/dev/i2c-6"
#define AXP_ADDR 0x34

static volatile sig_atomic_t shutdown_requested = 0;
static int log_fd = -1;

static void log_msg(const char *msg) {
    if (log_fd < 0) return;
    
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    
    char buf[512];
    int len = snprintf(buf, sizeof(buf), "[%s] %s\n", timestamp, msg);
    write(log_fd, buf, len);
    fsync(log_fd);
}

static int i2c_write(int fd, uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    return (write(fd, buf, 2) == 2) ? 0 : -1;
}

static int is_sdcard_unmounted(void) {
    FILE *fp = fopen("/proc/mounts", "r");
    if (!fp) {
        return 1; // Assume unmounted if we can't check
    }
    
    char line[512];
    int found = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "/mnt/SDCARD")) {
            found = 1;
            break;
        }
    }
    fclose(fp);
    
    return !found; // Return 1 if NOT found (unmounted)
}

static void wait_for_sdcard_unmount(void) {
    log_msg("Waiting for /mnt/SDCARD to unmount...");
    
    int timeout = 30; // 30 second timeout
    int count = 0;
    
    while (!is_sdcard_unmounted() && count < timeout) {
        sleep(1);
        count++;
    }
    
    if (is_sdcard_unmounted()) {
        log_msg("/mnt/SDCARD unmounted successfully");
    } else {
        log_msg("WARNING: /mnt/SDCARD still mounted after timeout, proceeding anyway");
    }
}

static void execute_poweroff_sequence(void) {
    log_msg("=== Executing AXP2202 Poweroff Sequence ===");
    
    // Wait for SD card to unmount
    wait_for_sdcard_unmount();
    
    // Sync filesystems
    sync();
    log_msg("Filesystems synced");
    
    int fd = open(I2C_BUS, O_RDWR);
    if (fd < 0) {
        log_msg("ERROR: Cannot open I2C bus");
        return;
    }
    log_msg("I2C bus opened");
    
    if (ioctl(fd, I2C_SLAVE_FORCE, AXP_ADDR) < 0) {
        log_msg("ERROR: Cannot communicate with PMIC");
        close(fd);
        return;
    }
    log_msg("PMIC communication established");
    
    // Execute the working sequence
    log_msg("Step 1: Disabling ALL IRQs");
    for (int i = 0x40; i <= 0x47; i++) {
        i2c_write(fd, i, 0x00);
    }
    
    log_msg("Step 2: Clearing ALL IRQ status");
    for (int i = 0x48; i <= 0x4F; i++) {
        i2c_write(fd, i, 0xFF);
    }
    
    log_msg("Step 3: Disabling wake sources");
    i2c_write(fd, 0x26, 0x00);
    i2c_write(fd, 0x27, 0x00);
    
    log_msg("Step 4: Battery disconnect");
    i2c_write(fd, 0x28, 0x00);
    usleep(100000);
    
    log_msg("Step 5: Disabling coulomb counter");
    i2c_write(fd, 0xB8, 0x00);
    usleep(100000);
    
    log_msg("Step 6: Disabling backup battery");
    i2c_write(fd, 0x35, 0x00);
    usleep(100000);
    
    log_msg("Step 7: Enabling all shutdown sources");
    i2c_write(fd, 0x22, 0xFF);
    usleep(50000);
    
    log_msg("Step 8: Configuring POK for immediate shutdown");
    i2c_write(fd, 0x23, 0x00);
    i2c_write(fd, 0x24, 0x00);
    usleep(50000);
    
    log_msg("Step 9: Triggering poweroff");
    i2c_write(fd, 0x10, 0x01);
    
    log_msg("Step 10: Disabling DCDC/LDO power rails");
    i2c_write(fd, 0x80, 0x00);  // DCDC control
    i2c_write(fd, 0x83, 0x00);  // DCDC1
    i2c_write(fd, 0x84, 0x00);  // DCDC2
    i2c_write(fd, 0x85, 0x00);  // DCDC3
    i2c_write(fd, 0x90, 0x00);  // LDO control
    i2c_write(fd, 0x91, 0x00);  // LDO1
    i2c_write(fd, 0x92, 0x00);  // LDO2
    
    usleep(200000);
    log_msg("Power cut sequence complete!");
    
    close(fd);
    
    // Final sync
    sync();
    sleep(1);
}

static void signal_handler(int signum) {
    char msg[128];
    snprintf(msg, sizeof(msg), "Received signal %d (%s)", 
             signum, 
             signum == SIGTERM ? "SIGTERM" :
             signum == SIGINT ? "SIGINT" :
             signum == SIGUSR1 ? "SIGUSR1" :
             signum == SIGPWR ? "SIGPWR" : "UNKNOWN");
    log_msg(msg);
    
    shutdown_requested = 1;
    
    // Check if this is a reboot or poweroff
    // Read /run/systemd/shutdown/scheduled or check runlevel
    int is_poweroff = 1; // Assume poweroff by default
    
    // Try to determine if it's a reboot by checking for reboot command
    FILE *fp = fopen("/proc/sys/kernel/poweroff_cmd", "r");
    if (fp) {
        char cmd[256];
        if (fgets(cmd, sizeof(cmd), fp)) {
            if (strstr(cmd, "reboot")) {
                is_poweroff = 0;
            }
        }
        fclose(fp);
    }
    
    // Alternative check: examine shutdown state
    fp = fopen("/tmp/shutdown_type", "r");
    if (fp) {
        char type[32];
        if (fgets(type, sizeof(type), fp)) {
            if (strstr(type, "reboot")) {
                is_poweroff = 0;
            }
        }
        fclose(fp);
    }
    
    if (!is_poweroff) {
        log_msg("Reboot detected - skipping poweroff sequence");
        return;
    }
    
    // Execute poweroff sequence only for actual poweroff
    execute_poweroff_sequence();
    
    // If we're still here, try system shutdown as fallback
    if (signum == SIGTERM || signum == SIGINT || signum == SIGPWR) {
        sync();
        reboot(RB_POWER_OFF);
    }
}

static void daemonize(void) {
    pid_t pid = fork();
    
    if (pid < 0) {
        exit(EXIT_FAILURE);
    }
    
    if (pid > 0) {
        // Parent: write PID to HOME directory and exit
        const char *home = getenv("HOME");
        if (home) {
            char pid_file[512];
            snprintf(pid_file, sizeof(pid_file), "%s/poweroff_daemon.pid", home);
            FILE *f = fopen(pid_file, "w");
            if (f) {
                fprintf(f, "%d\n", pid);
                fclose(f);
            }
        }
        exit(EXIT_SUCCESS);
    }
    
    // Child continues as daemon
    
    // Create new session
    if (setsid() < 0) {
        exit(EXIT_FAILURE);
    }
    
    // Set file creation mask
    umask(0);
    
    // Change to root directory
    if (chdir("/") < 0) {
        exit(EXIT_FAILURE);
    }
    
    // Close standard file descriptors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    
    // Open log file from LOGS_PATH environment variable
    const char *logs_path = getenv("LOGS_PATH");
    const char *pak_name = getenv("PAK_NAME");
    char log_file[512];
    
    if (logs_path && pak_name) {
        snprintf(log_file, sizeof(log_file), "%s/%s.service.txt", logs_path, pak_name);
    } else {
        snprintf(log_file, sizeof(log_file), "/tmp/poweroff_daemon.log");
    }
    
    log_fd = open(log_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
}

int main(int argc, char *argv[]) {
    int foreground = 0;
    
    // Parse arguments
    if (argc > 1 && strcmp(argv[1], "-f") == 0) {
        foreground = 1;
    }
    
    if (!foreground) {
        daemonize();
    } else {
        log_fd = STDOUT_FILENO;
    }
    
    log_msg("=== TrimUI Brick Poweroff Daemon Started ===");
    log_msg("Monitoring for shutdown signals...");
    
    // Set up signal handlers
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    
    // Catch shutdown signals
    sigaction(SIGTERM, &sa, NULL);  // Standard shutdown signal
    sigaction(SIGINT, &sa, NULL);   // Ctrl+C (for testing)
    sigaction(SIGUSR1, &sa, NULL);  // Custom trigger
    sigaction(SIGPWR, &sa, NULL);   // Power failure signal
    
    log_msg("Signal handlers installed");
    
    // Main loop - just sleep and wait for signals
    while (!shutdown_requested) {
        sleep(60);
        
        // Periodic heartbeat
        if (!shutdown_requested) {
            log_msg("Daemon alive and monitoring...");
        }
    }
    
    log_msg("Daemon shutting down");
    
    if (!foreground) {
        // Clean up PID file
        const char *home = getenv("HOME");
        if (home) {
            char pid_file[512];
            snprintf(pid_file, sizeof(pid_file), "%s/poweroff_daemon.pid", home);
            unlink(pid_file);
        }
        close(log_fd);
    }
    
    return 0;
}
