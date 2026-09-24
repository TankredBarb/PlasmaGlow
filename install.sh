#!/bin/bash
# Exit on any error
set -e
cd "$(dirname "$(readlink -f "$0")")"

echo "=== Building and Installing PlasmaGlow ==="

# 1. Create build directory
mkdir -p build

# 2. Configure with CMake using system-wide prefix
echo "Configuring project..."
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/usr -DPLASMAGLOW_BUILD_KWIN_EFFECT=ON

# 3. Build the plugin
echo "Building project..."
cmake --build build -j "$(nproc)"

# 4. Install the plugin and package system-wide (requires sudo)
echo "Installing project..."
sudo cmake --install build

# 5. Rebuild KDE system configuration cache and update icon cache
echo "Rebuilding KDE sycoca cache..."
kbuildsycoca6
echo "Updating system icon cache..."
sudo touch /usr/share/icons/hicolor

echo "=== Installation Completed Successfully! ==="
echo "You can now add 'PlasmaGlow' to your panel or desktop via the Widget Explorer."
