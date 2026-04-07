#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
BINARY="$BUILD_DIR/rammb-neovis"

cd "$SCRIPT_DIR"

echo "==> Configuring..."
cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -Wno-dev 2>&1 \
    | grep -v "^--" || true

echo "==> Building..."
cmake --build "$BUILD_DIR" -j"$(nproc)" 2>&1

echo "==> Launching RAMMB-NEOVIS..."
exec "$BINARY"
