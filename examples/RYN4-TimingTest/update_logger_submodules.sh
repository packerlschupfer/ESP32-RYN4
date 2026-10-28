#!/bin/bash
# Script to update logger_submodule in all libraries

LIBS_DIR="~/.platformio/lib"
CURRENT_DIR=$(pwd)

# List of libraries that use Logger
LIBRARIES=(
    "workspace_Class-MutexGuard"
    "workspace_Class-SemaphoreGuard"
    "workspace_Class-ModbusDevice"
    "workspace_Class-RYN4"
    "esp32ModbusRTU"
    "workspace_Class-EthernetManager"
    "workspace_Class-TaskManager"
    "workspace_Class-OTAManager"
    "workspace_Class-Watchdog"
    "workspace_Class-IDeviceInstance"
)

echo "Starting Logger submodule updates..."
echo "=================================="

# First, make sure Logger has the required files
echo "Checking Logger repository for LogInterface.h..."
if [ -f "$LIBS_DIR/workspace_Class-Logger/src/LogInterface.h" ]; then
    echo "✓ LogInterface.h found in Logger repository"
else
    echo "✗ LogInterface.h NOT found in Logger repository!"
    echo "Please ensure LogInterface.h is committed to the Logger repository first."
    exit 1
fi

# Update each library
for lib in "${LIBRARIES[@]}"; do
    LIB_PATH="$LIBS_DIR/$lib"
    
    if [ ! -d "$LIB_PATH" ]; then
        echo "⚠ Library not found: $lib"
        continue
    fi
    
    echo ""
    echo "Processing $lib..."
    echo "-----------------"
    
    cd "$LIB_PATH"
    
    # Check if logger_submodule exists
    if [ -d "logger_submodule" ]; then
        echo "Found logger_submodule in $lib"
        
        # Initialize submodule if needed
        if [ ! -f "logger_submodule/.git" ]; then
            echo "Initializing submodule..."
            git submodule init logger_submodule
        fi
        
        # Update the submodule
        echo "Updating submodule..."
        git submodule update --remote logger_submodule
        
        # Check if LogInterface.h exists in the submodule
        if [ -f "logger_submodule/src/LogInterface.h" ]; then
            echo "✓ LogInterface.h found in submodule"
            
            # Stage the submodule update
            git add logger_submodule
            
            # Check if there are changes to commit
            if git diff --cached --quiet; then
                echo "No changes to commit"
            else
                git commit -m "Update logger_submodule to include LogInterface.h"
                echo "✓ Committed submodule update"
            fi
        else
            echo "✗ LogInterface.h NOT found in submodule after update"
            echo "  The Logger repository might not have LogInterface.h committed"
        fi
    else
        echo "No logger_submodule found in $lib"
    fi
done

# Return to original directory
cd "$CURRENT_DIR"

echo ""
echo "=================================="
echo "Submodule update process complete!"
echo ""
echo "Next steps:"
echo "1. Push changes to each library repository if needed"
echo "2. Clean and rebuild your project: rm -rf .pio && pio run"