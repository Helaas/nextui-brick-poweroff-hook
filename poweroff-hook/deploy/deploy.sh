#!/bin/bash
#
# deploy.sh - Deploy poweroff_hook module to TrimUI Brick
#
# Usage: ./deploy.sh [copy|load|unload|test|install|status]
#

set -e

# Configuration
DEVICE_IP="192.168.0.156"
DEVICE_USER="root"
DEVICE_PASSWORD="tina"
MODULE_NAME="poweroff_hook"
MODULE_KO="../src/${MODULE_NAME}.ko"
DEVICE_TMP="/tmp/${MODULE_NAME}.ko"
DEVICE_MODULE_DIR="/lib/modules/4.9.191"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if sshpass is available
USE_SSHPASS=false
if command -v sshpass >/dev/null 2>&1; then
    USE_SSHPASS=true
    SSH_CMD="sshpass -p ${DEVICE_PASSWORD} ssh"
    SCP_CMD="sshpass -p ${DEVICE_PASSWORD} scp"
else
    SSH_CMD="ssh"
    SCP_CMD="scp"
    echo -e "${YELLOW}Note: sshpass not installed. You'll need to enter password manually.${NC}"
    echo -e "${YELLOW}Install with: brew install sshpass${NC}"
fi

# SSH/SCP shortcuts
ssh_exec() {
    $SSH_CMD ${DEVICE_USER}@${DEVICE_IP} "$@"
}

scp_copy() {
    $SCP_CMD "$@"
}

# Check if module is built
check_module() {
    if [ ! -f "$MODULE_KO" ]; then
        echo -e "${RED}Error: Module not built.${NC}"
        echo "Run 'make build' from the project root first."
        exit 1
    fi
}

# Copy module to device
copy_module() {
    echo -e "${GREEN}Copying module to device...${NC}"
    check_module
    scp_copy "$MODULE_KO" ${DEVICE_USER}@${DEVICE_IP}:${DEVICE_TMP}
    echo -e "${GREEN}Module copied to ${DEVICE_TMP}${NC}"
}

# Load module on device
load_module() {
    echo -e "${GREEN}Loading module on device...${NC}"
    copy_module
    
    echo "Inserting module..."
    ssh_exec "insmod ${DEVICE_TMP}"
    
    echo ""
    echo -e "${GREEN}Module loaded successfully!${NC}"
    echo "Kernel messages (last 20 lines):"
    echo "================================"
    ssh_exec "dmesg | tail -20"
    echo ""
    echo -e "${YELLOW}Module is now active and will trigger on power-off.${NC}"
}

# Unload module from device
unload_module() {
    echo -e "${GREEN}Unloading module from device...${NC}"
    
    ssh_exec "rmmod ${MODULE_NAME}" || {
        echo -e "${RED}Failed to unload module. It may not be loaded.${NC}"
        return 1
    }
    
    echo ""
    echo -e "${GREEN}Module unloaded successfully!${NC}"
    echo "Kernel messages (last 10 lines):"
    echo "================================"
    ssh_exec "dmesg | tail -10"
}

# Check module status
check_status() {
    echo -e "${GREEN}Checking module status...${NC}"
    echo ""
    
    echo "Loaded modules:"
    ssh_exec "lsmod | grep ${MODULE_NAME} || echo 'Module not loaded'"
    
    echo ""
    echo "Module file in /tmp:"
    ssh_exec "ls -lh ${DEVICE_TMP} 2>/dev/null || echo 'Not found in /tmp'"
    
    echo ""
    echo "Installed in ${DEVICE_MODULE_DIR}:"
    ssh_exec "ls -lh ${DEVICE_MODULE_DIR}/${MODULE_NAME}.ko 2>/dev/null || echo 'Not installed'"
    
    echo ""
    echo "Recent kernel messages (poweroff_hook):"
    ssh_exec "dmesg | grep poweroff_hook | tail -15 || echo 'No messages found'"
}

# Install module permanently
install_module() {
    echo -e "${GREEN}Installing module permanently...${NC}"
    copy_module
    
    echo "Copying to ${DEVICE_MODULE_DIR}..."
    ssh_exec "cp ${DEVICE_TMP} ${DEVICE_MODULE_DIR}/"
    
    echo "Running depmod..."
    ssh_exec "depmod -a"
    
    echo ""
    echo -e "${GREEN}Module installed successfully!${NC}"
    echo -e "${YELLOW}To load at boot, add '${MODULE_NAME}' to /etc/modules${NC}"
}

# Test module (interactive)
test_module() {
    echo -e "${GREEN}Testing power-off hook module${NC}"
    echo "================================"
    echo ""
    
    load_module
    
    echo ""
    echo -e "${YELLOW}Module is now loaded and ready.${NC}"
    echo ""
    echo "To test the power-off hook:"
    echo "  1. Power off the device from the UI, or"
    echo "  2. SSH to the device and run: poweroff"
    echo "  3. After the device powers off and reboots, check for:"
    echo "     - /poweroff_log.txt"
    echo "     - /data/poweroff_log.txt"
    echo "     - /mnt/SDCARD/poweroff_log.txt"
    echo "     - /tmp/poweroff_log.txt"
    echo ""
    echo -e "${YELLOW}Press Enter to unload the module, or Ctrl+C to keep it loaded...${NC}"
    read -r
    
    unload_module
}

# Check log files
check_logs() {
    echo -e "${GREEN}Checking for power-off log files...${NC}"
    echo ""
    
    for path in /poweroff_log.txt /data/poweroff_log.txt /mnt/SDCARD/poweroff_log.txt /tmp/poweroff_log.txt; do
        echo "Checking ${path}:"
        ssh_exec "if [ -f ${path} ]; then echo 'Found:'; cat ${path}; else echo 'Not found'; fi"
        echo ""
    done
}

# Show usage
usage() {
    echo "TrimUI Brick Power-Off Hook Deployment Script"
    echo ""
    echo "Usage: $0 [command]"
    echo ""
    echo "Commands:"
    echo "  copy      - Copy module to device /tmp/"
    echo "  load      - Load module on device"
    echo "  unload    - Unload module from device"
    echo "  test      - Load module, wait for user test, then unload"
    echo "  install   - Install module permanently to ${DEVICE_MODULE_DIR}"
    echo "  status    - Check module status"
    echo "  logs      - Check for power-off log files"
    echo "  help      - Show this help message"
    echo ""
    echo "Device: ${DEVICE_USER}@${DEVICE_IP}"
    echo ""
    echo "Examples:"
    echo "  $0 load        # Load module for testing"
    echo "  $0 test        # Interactive test (load, test, unload)"
    echo "  $0 logs        # Check if log files were created"
    echo ""
}

# Main script
case "${1:-help}" in
    copy)
        copy_module
        ;;
    load)
        load_module
        ;;
    unload)
        unload_module
        ;;
    test)
        test_module
        ;;
    install)
        install_module
        ;;
    status)
        check_status
        ;;
    logs)
        check_logs
        ;;
    help|--help|-h)
        usage
        ;;
    *)
        echo -e "${RED}Unknown command: $1${NC}"
        echo ""
        usage
        exit 1
        ;;
esac
