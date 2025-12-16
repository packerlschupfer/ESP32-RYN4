# RYN4 Timing Test Example

This example is designed to identify the source of the 12-second initialization delay in RYN4.

## Quick Fix

If you're experiencing 12-second delays, try this in your code:

```cpp
// Create custom init config  
RYN4::InitConfig config;
config.resetRelaysOnInit = false;   // Skip relay reset
config.skipRelayStateRead = true;   // Skip relay state read

// Initialize with custom config
relayController->initialize(config);
```

## Purpose

Measure exact timing of each operation during RYN4 initialization to identify bottlenecks.

## Features

1. **Detailed Timing Logs**:
   - Measures time for each Modbus operation
   - Shows total initialization time
   - Identifies which operations are slow

2. **Test Configuration**:
   - `resetRelaysOnInit = false` - Skips relay reset to save time
   - `skipRelayStateRead = true` - Skips reading relay states

## Expected Output

```
[TIMING] initializeModuleSettings() started
[TIMING] Testing module responsiveness...
[TIMING] isModuleResponsive() register read took: XXX ms
[TIMING] Module responsive check took: XXX ms
[TIMING] Reading module configuration registers...
[TIMING] Configuration registers read took: XXX ms
[TIMING] Skipping relay state read (skipRelayStateRead = true)
[TIMING] Total initializeModuleSettings() time: XXX ms
```

## Analysis

If initialization still takes 12 seconds with both options disabled, the delay is likely in:
1. Module responsiveness check timing out
2. Configuration register reads timing out
3. Modbus communication issues at 9600 baud

## Usage

1. Build and flash this example
2. Monitor serial output
3. Look for operations taking >1000ms (likely timeouts)
4. Adjust configuration based on findings