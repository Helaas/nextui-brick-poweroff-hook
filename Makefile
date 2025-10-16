# Makefile for TrimUI Brick Power-Off Hook Kernel Module
# This Makefile builds a kernel module using Docker cross-compilation

.PHONY: all build clean docker-build docker-shell help deploy-copy deploy-load deploy-unload deploy-test

# Project configuration
PROJECT_NAME := poweroff-hook
MODULE_NAME := poweroff_hook
DOCKER_IMAGE := trimui-brick-gcc74
SRC_DIR := src
DEPLOY_DIR := deploy
KERNEL_HEADERS := kernel-headers/linux-4.9

# Device configuration
DEVICE_IP := 192.168.0.156
DEVICE_USER := root
DEVICE_PASSWORD := tina
DEVICE_MODULE_DIR := /lib/modules/4.9.191
KERNEL_VERSION := 4.9.191

# Build output
MODULE_KO := $(SRC_DIR)/$(MODULE_NAME).ko

# Default target
all: build

# Build the kernel module using Docker and kernel build system
build:
	docker run --rm \
		-v "$(PWD):/work" \
		-w /work \
		$(DOCKER_IMAGE) \
		bash -c "source /root/setup-env.sh && \
			chmod -R 777 /work/src && \
			make -C $(KERNEL_HEADERS) M=/work/src ARCH=arm64 CROSS_COMPILE=\$${CROSS_COMPILE} EXTRA_CFLAGS='-fno-stack-protector -march=armv8-a -mtune=cortex-a53' modules && \
			chmod -R 777 /work/src"
	@if [ -f "$(MODULE_KO)" ]; then \
		echo ""; \
		echo "Build successful: $(MODULE_KO)"; \
		ls -lh $(MODULE_KO); \
		file $(MODULE_KO); \
	else \
		echo "Build failed: $(MODULE_KO) not found"; \
		exit 1; \
	fi

# Check if kernel headers are available
check-headers:
	@if [ ! -d "$(KERNEL_HEADERS)" ]; then \
		echo "Error: Kernel headers not found at $(KERNEL_HEADERS)"; \
		echo "Run './download_kernel_headers.sh' first"; \
		exit 1; \
	fi

# Build the Docker image
docker-build:
	@if [ -z "$$(docker images -q $(DOCKER_IMAGE))" ]; then \
		echo "Building Docker image $(DOCKER_IMAGE)..."; \
		docker build --platform linux/amd64 -t $(DOCKER_IMAGE) .; \
	else \
		echo "Docker image $(DOCKER_IMAGE) already exists."; \
	fi

# Open a shell in the Docker container for debugging
docker-shell: docker-build
	@echo "Starting Docker shell..."
	@docker run -it --rm \
		-v "$(PWD)":/root/workspace \
		$(DOCKER_IMAGE) \
		/bin/bash

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	@cd $(SRC_DIR) && rm -f *.o *.ko *.mod.c *.mod *.order *.symvers .*.cmd
	@cd $(SRC_DIR) && rm -rf .tmp_versions
	@echo "Clean complete."

# Deployment targets (require sshpass or manual password entry)

# Copy module to device
deploy-copy:
	@echo "Copying $(MODULE_KO) to device $(DEVICE_USER)@$(DEVICE_IP)..."
	@if [ ! -f "$(MODULE_KO)" ]; then \
		echo "Error: Module not built. Run 'make build' first."; \
		exit 1; \
	fi
	@if command -v sshpass >/dev/null 2>&1; then \
		sshpass -p "$(DEVICE_PASSWORD)" scp "$(MODULE_KO)" $(DEVICE_USER)@$(DEVICE_IP):/tmp/; \
	else \
		echo "Note: sshpass not found, you'll need to enter password manually"; \
		scp "$(MODULE_KO)" $(DEVICE_USER)@$(DEVICE_IP):/tmp/; \
	fi
	@echo "Module copied to device:/tmp/$(MODULE_NAME).ko"

# Load module on device
deploy-load: deploy-copy
	@echo "Loading module on device..."
	@if command -v sshpass >/dev/null 2>&1; then \
		sshpass -p "$(DEVICE_PASSWORD)" ssh $(DEVICE_USER)@$(DEVICE_IP) "insmod /tmp/$(MODULE_NAME).ko && dmesg | tail -20"; \
	else \
		ssh $(DEVICE_USER)@$(DEVICE_IP) "insmod /tmp/$(MODULE_NAME).ko && dmesg | tail -20"; \
	fi
	@echo "Module loaded. Check dmesg output above for confirmation."

# Unload module from device
deploy-unload:
	@echo "Unloading module from device..."
	@if command -v sshpass >/dev/null 2>&1; then \
		sshpass -p "$(DEVICE_PASSWORD)" ssh $(DEVICE_USER)@$(DEVICE_IP) "rmmod $(MODULE_NAME) && dmesg | tail -10"; \
	else \
		ssh $(DEVICE_USER)@$(DEVICE_IP) "rmmod $(MODULE_NAME) && dmesg | tail -10"; \
	fi
	@echo "Module unloaded."

# Test module (load, show status, unload)
deploy-test: deploy-load
	@echo ""
	@echo "Module loaded. To test power-off functionality:"
	@echo "  1. Keep SSH connection open to monitor"
	@echo "  2. Power off the device from the UI or run: poweroff"
	@echo "  3. After reboot, check for /poweroff_log.txt"
	@echo ""
	@echo "Press Enter to unload module, or Ctrl+C to keep it loaded..."
	@read -p "" dummy
	@$(MAKE) deploy-unload

# Install module permanently (copies to /lib/modules)
deploy-install: deploy-copy
	@echo "Installing module permanently to $(DEVICE_MODULE_DIR)..."
	@if command -v sshpass >/dev/null 2>&1; then \
		sshpass -p "$(DEVICE_PASSWORD)" ssh $(DEVICE_USER)@$(DEVICE_IP) \
			"cp /tmp/$(MODULE_NAME).ko $(DEVICE_MODULE_DIR)/ && depmod -a"; \
	else \
		ssh $(DEVICE_USER)@$(DEVICE_IP) \
			"cp /tmp/$(MODULE_NAME).ko $(DEVICE_MODULE_DIR)/ && depmod -a"; \
	fi
	@echo "Module installed. Add to /etc/modules to load at boot."

# Show help
help:
	@echo "TrimUI Brick Power-Off Hook Kernel Module Build System"
	@echo ""
	@echo "Usage: make [target]"
	@echo ""
	@echo "Build Targets:"
	@echo "  all            - Build the kernel module (default)"
	@echo "  build          - Build the kernel module using Docker"
	@echo "  clean          - Remove build artifacts"
	@echo "  docker-build   - Build/check Docker cross-compilation image"
	@echo "  docker-shell   - Open interactive Docker shell"
	@echo ""
	@echo "Deployment Targets (to $(DEVICE_USER)@$(DEVICE_IP)):"
	@echo "  deploy-copy    - Copy module to device /tmp/"
	@echo "  deploy-load    - Copy and load module (insmod)"
	@echo "  deploy-unload  - Unload module (rmmod)"
	@echo "  deploy-test    - Load module, wait for user, then unload"
	@echo "  deploy-install - Install module to $(DEVICE_MODULE_DIR)"
	@echo ""
	@echo "Testing:"
	@echo "  1. make deploy-load     # Load module"
	@echo "  2. ssh root@$(DEVICE_IP)  # Connect to device"
	@echo "  3. poweroff             # Trigger power-off"
	@echo "  4. After reboot, check: cat /poweroff_log.txt"
	@echo ""
	@echo "Note: Install 'sshpass' for automated password entry:"
	@echo "  macOS: brew install sshpass"
	@echo "  Linux: apt-get install sshpass"

