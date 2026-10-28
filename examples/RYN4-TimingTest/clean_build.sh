#!/bin/bash
# Clean build script to ensure correct library versions are used

echo "Cleaning PlatformIO build..."
rm -rf .pio
rm -rf .cache

echo "Cleaning done. Now run:"
echo "pio run -t upload && pio device monitor"