# Makefile for TrimUI Brick Power-Off Hook Kernel Module
# This Makefile builds a kernel module using Docker cross-compilation

.PHONY: all build clean docker-build docker-shell help deploy-copy deploy-load deploy-unload deploy-test deploy setup-deps setup-toolchain setup-headers distclean

# Project configuration
PROJECT_NAME := poweroff-hook
MODULE_NAME := poweroff_hook
DOCKER_IMAGE := trimui-brick-gcc74
SRC_DIR := src
BIN_DIR := bin
DEPLOY_DIR := deploy
KERNEL_HEADERS_DIR := kernel-headers
KERNEL_HEADERS := $(KERNEL_HEADERS_DIR)/linux-4.9
TOOLCHAIN_DIR := toolchain
TOOLCHAIN_URL := https://releases.linaro.org/components/toolchain/binaries/7.4-2019.02/aarch64-linux-gnu/gcc-linaro-7.4.1-2019.02-x86_64_aarch64-linux-gnu.tar.xz
TOOLCHAIN_FILE := $(TOOLCHAIN_DIR)/gcc-linaro-7.4.1-2019.02-x86_64_aarch64-linux-gnu.tar.xz

# Utility versions for pak
JQ_VERSION := 1.7.1
MINUI_LIST_VERSION := 0.11.4
MINUI_PRESENTER_VERSION := 0.7.0
ARCH := linux-arm64
DEVICE := tg5040

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

# Setup all dependencies (toolchain + headers)
setup-deps: setup-toolchain setup-headers
	@echo "All dependencies set up successfully!"

# Download and extract GCC toolchain
setup-toolchain:
	@if [ -d "$(TOOLCHAIN_DIR)/gcc-linaro-7.4.1-2019.02-x86_64_aarch64-linux-gnu" ]; then \
		echo "Toolchain already exists at $(TOOLCHAIN_DIR)"; \
	else \
		echo "Downloading Linaro GCC 7.4.1 toolchain..."; \
		mkdir -p $(TOOLCHAIN_DIR); \
		curl -L -o $(TOOLCHAIN_FILE) $(TOOLCHAIN_URL); \
		echo "Extracting toolchain..."; \
		cd $(TOOLCHAIN_DIR) && tar xf gcc-linaro-7.4.1-2019.02-x86_64_aarch64-linux-gnu.tar.xz; \
		echo "Toolchain setup complete!"; \
	fi

# Download and configure kernel headers
setup-headers:
	@if [ -d "$(KERNEL_HEADERS)" ]; then \
		echo "Kernel headers already configured at $(KERNEL_HEADERS)"; \
	else \
		echo "Setting up kernel headers..."; \
		./download_kernel_headers.sh; \
		./configure_kernel_headers.sh; \
		echo "Kernel headers setup complete!"; \
	fi

# Build the kernel module using Docker and kernel build system
build: docker-build check-headers
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
	@rm -f $(BIN_DIR)/$(MODULE_NAME).ko
	@rm -f $(BIN_DIR)/jq $(BIN_DIR)/jq.LICENSE
	@rm -f $(BIN_DIR)/minui-list $(BIN_DIR)/minui-presenter
	@rm -rf $(DEPLOY_DIR)
	@rm -rf pakz/Tools pakz/.system/$(DEVICE)
	@echo "Clean complete."

# Full clean including dependencies
distclean: clean
	@echo "Cleaning all dependencies..."
	@rm -rf $(KERNEL_HEADERS_DIR)
	@rm -rf $(TOOLCHAIN_DIR)
	@echo "Full clean complete."

# Deploy target - build, download utilities, and create pak zip
deploy: build
	@echo "Preparing deployment package..."
	@mkdir -p $(DEPLOY_DIR)
	@echo "Copying kernel module to bin directory..."
	@cp $(MODULE_KO) $(BIN_DIR)/$(MODULE_NAME).ko
	@echo "Downloading utilities..."
	@curl -f -o $(BIN_DIR)/jq -sSL https://github.com/jqlang/jq/releases/download/jq-$(JQ_VERSION)/jq-$(ARCH)
	@chmod +x $(BIN_DIR)/jq
	@curl -sSL -o $(BIN_DIR)/jq.LICENSE "https://raw.githubusercontent.com/jqlang/jq/refs/tags/jq-$(JQ_VERSION)/COPYING"
	@curl -f -o $(BIN_DIR)/minui-list -sSL https://github.com/josegonzalez/minui-list/releases/download/$(MINUI_LIST_VERSION)/minui-list-$(DEVICE)
	@chmod +x $(BIN_DIR)/minui-list
	@curl -f -o $(BIN_DIR)/minui-presenter -sSL https://github.com/josegonzalez/minui-presenter/releases/download/$(MINUI_PRESENTER_VERSION)/minui-presenter-$(DEVICE)
	@chmod +x $(BIN_DIR)/minui-presenter
	@echo "Creating PowerOffHook.pak.zip..."
	@cd $(DEPLOY_DIR) && rm -f PowerOffHook.pak.zip
	@zip -r $(DEPLOY_DIR)/PowerOffHook.pak.zip \
		$(BIN_DIR)/ \
		launch.sh \
		settings.json \
		pak.json \
		README.md \
		LICENSE \
		-x "$(BIN_DIR)/.DS_Store"
	@echo "Deployment package created: $(DEPLOY_DIR)/PowerOffHook.pak.zip"
	@ls -lh $(DEPLOY_DIR)/PowerOffHook.pak.zip
	@echo "Creating PowerOffHook.pakz..."
	@mkdir -p pakz/Tools/$(DEVICE)/PowerOffHook.pak/bin
	@mkdir -p pakz/.system/$(DEVICE)/bin
	@cp $(BIN_DIR)/* pakz/Tools/$(DEVICE)/PowerOffHook.pak/bin/
	@cp launch.sh pakz/Tools/$(DEVICE)/PowerOffHook.pak/
	@cp settings.json pakz/Tools/$(DEVICE)/PowerOffHook.pak/
	@cp pak.json pakz/Tools/$(DEVICE)/PowerOffHook.pak/
	@cp README.md pakz/Tools/$(DEVICE)/PowerOffHook.pak/
	@cp LICENSE pakz/Tools/$(DEVICE)/PowerOffHook.pak/
	@cp $(BIN_DIR)/$(MODULE_NAME).ko pakz/.system/$(DEVICE)/bin/poweroff_next
	@cd pakz && rm -f ../$(DEPLOY_DIR)/PowerOffHook.pakz
	@cd pakz && zip -r ../$(DEPLOY_DIR)/PowerOffHook.pakz Tools/ .system/ -x "**/.DS_Store"
	@echo "Pakz file created: $(DEPLOY_DIR)/PowerOffHook.pakz"
	@ls -lh $(DEPLOY_DIR)/PowerOffHook.pakz

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
	@echo "Setup Targets:"
	@echo "  setup-deps     - Download and configure all dependencies (toolchain + headers)"
	@echo "  setup-toolchain - Download Linaro GCC 7.4.1 toolchain"
	@echo "  setup-headers  - Download and configure kernel headers"
	@echo ""
	@echo "Build Targets:"
	@echo "  all            - Build the kernel module (default)"
	@echo "  build          - Build the kernel module using Docker"
	@echo "  deploy         - Build and create PowerOffHook.pak.zip and PowerOffHook.pakz packages"
	@echo "  clean          - Remove build artifacts"
	@echo "  distclean      - Remove build artifacts and dependencies"
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
	@echo "Quick Start:"
	@echo "  1. make setup-deps  # Download toolchain and kernel headers (first time only)"
	@echo "  2. make build       # Build the kernel module"
	@echo "  3. make deploy      # Create pak.zip for TrimUI installation"
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

