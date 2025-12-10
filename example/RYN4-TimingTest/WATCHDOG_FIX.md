# Watchdog Timeout Fix for RYN4 Initialization

## Problem
The system crashes with watchdog timeout during RYN4 initialization because:
- RYN4 initialization takes ~12 seconds
- The RYN4Proc task has a watchdog timeout that triggers before initialization completes
- The task is blocked waiting for initialization and can't feed the watchdog

## Quick Fix

Add this to your main.cpp before RYN4 initialization:

```cpp
// Disable task watchdog during initialization
#include <esp_task_wdt.h>
esp_task_wdt_delete(NULL);  // Remove current task from watchdog

// Initialize RYN4
auto initResult = relayController->initialize();

// Re-enable watchdog after initialization
esp_task_wdt_add(NULL);
esp_task_wdt_reset();
```

## Better Solution

Use the optimized initialization configuration:

```cpp
RYN4::InitConfig config;
config.resetRelaysOnInit = false;   // Skip relay reset
config.skipRelayStateRead = true;   // Skip relay state read

// This should complete in <1 second instead of 12 seconds
relayController->initialize(config);
```

## Root Cause
The 12-second delay is likely caused by multiple Modbus timeouts (1 second each) when:
- Module is not responding at the configured address
- Baud rate mismatch
- Wiring issues

The timing logs will show exactly which operations are timing out.