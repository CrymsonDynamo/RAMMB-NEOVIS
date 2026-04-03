#!/usr/bin/env bash
# Install system dependencies and configure the build directory.
set -e

echo "==> Installing system dependencies..."
sudo apt-get update -qq
sudo apt-get install -y \
    libglfw3-dev \
    libglew-dev \
    libglm-dev \
    nlohmann-json3-dev \
    libsqlite3-dev \
    libcurl4-openssl-dev \
    libavcodec-dev \
    libavformat-dev \
    libavutil-dev \
    libswscale-dev \
    build-essential \
    cmake \
    git \
    pkg-config

echo "==> Configuring CMake build..."
cmake -B build -DCMAKE_BUILD_TYPE=Release

echo ""
echo "Done! Now run:  cmake --build build -j\$(nproc)"
echo "Then run:       ./build/stormview"
