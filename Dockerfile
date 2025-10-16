# Dockerfile for TrimUI Brick Kernel Module Development
# Uses GCC 7.4.1 Linaro toolchain to match device kernel

FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    bc \
    bison \
    flex \
    libssl-dev \
    libelf-dev \
    make \
    file \
    xz-utils \
    && rm -rf /var/lib/apt/lists/*

# Copy and extract the Linaro GCC 7.4 toolchain
COPY toolchain/gcc-linaro-7.4.1-aarch64.tar.xz /tmp/
RUN tar -C /opt -xf /tmp/gcc-linaro-7.4.1-aarch64.tar.xz \
    && rm /tmp/gcc-linaro-7.4.1-aarch64.tar.xz

# Set up environment script
RUN echo '#!/bin/bash' > /root/setup-env.sh && \
    echo 'export PATH="/opt/gcc-linaro-7.4.1-2019.02-x86_64_aarch64-linux-gnu/bin:$PATH"' >> /root/setup-env.sh && \
    echo 'export CROSS_COMPILE=aarch64-linux-gnu-' >> /root/setup-env.sh && \
    echo 'export ARCH=arm64' >> /root/setup-env.sh && \
    chmod +x /root/setup-env.sh

WORKDIR /work

CMD ["/bin/bash"]
