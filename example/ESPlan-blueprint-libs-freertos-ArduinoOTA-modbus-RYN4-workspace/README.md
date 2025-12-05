# RYN4 Example - ModbusDevice Architecture

This example demonstrates how to use the RYN4 8-channel relay controller with the new ModbusDevice architecture.

## Key Features

1. **Global ModbusRTU Management**: The ModbusDevice base class uses a global ModbusRTU instance set via `setGlobalModbusRTU()`
2. **Automatic Response Routing**: Devices are registered in a global map, and responses are automatically routed to the correct device
3. **Hybrid Sync/Async Operation**: RYN4 uses synchronous reads during initialization, then switches to async mode for normal operation
4. **Two-Phase Initialization**: Supports the circular dependency resolution pattern

## Architecture Overview

```
ModbusDevice (base class)
    ↑
SimpleModbusDevice (sync-only operations)
    ↑
QueuedModbusDevice (adds async queue support)
    ↑
RYN4 (8-channel relay controller)
```

## Initialization Sequence

```cpp
// 1. Initialize Serial for RS485
Serial1.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);

// 2. Set global ModbusRTU instance (CRITICAL - must be done first!)
setGlobalModbusRTU(&modbusMaster);

// 3. Setup Modbus callbacks
modbusMaster.onData(mainHandleData);  // Global handler from ModbusDevice.h
modbusMaster.onError(handleError);     // Global error handler
modbusMaster.begin();

// 4. Create device instance
RYN4* device = new RYN4(MODBUS_ADDRESS, "RYN4");

// 5. Register in global device map
registerModbusDevice(MODBUS_ADDRESS, device);

// 6. Apply custom configurations
device->setCustomRelayMappings(relayConfigs);

// 7. Initialize device (uses sync reads)
auto result = device->initialize();

// 8. Wait for initialization to complete
device->waitForInitializationComplete(timeout);

// 9. Device automatically switches to async mode after init
```

## Key Differences from Old Architecture

### Old Way
```cpp
// Had to pass modbusMaster to constructor
RYN4* device = new RYN4(address, modbusMaster, "RYN4");

// Manual callback registration per device
device->setModbusCallback(...);
```

### New Way
```cpp
// No modbusMaster parameter needed
RYN4* device = new RYN4(address, "RYN4");

// Automatic response routing via global map
registerModbusDevice(address, device);
```

## Configuration

Edit `src/config/ProjectConfig.h` to configure:
- Modbus address
- Serial pins
- Baud rate
- Relay default states
- Task priorities and stack sizes

## Building

```bash
# For USB debugging with selective logging
pio run -e esp32dev_usb_debug_selective

# For release build
pio run -e esp32dev_usb_release

# Upload
pio run -t upload -e esp32dev_usb_debug_selective
```

## Troubleshooting

1. **Initialization Timeout**: Check RS485 connections and verify DIP switch settings match configured address
2. **No Response**: Ensure `setGlobalModbusRTU()` is called before creating devices
3. **Relay Not Responding**: Verify the relay mapping configuration matches your wiring

## Tasks

The example includes two main tasks:

1. **RelayControlTask**: Processes relay control commands from a queue
2. **RelayStatusTask**: Periodically reads relay states and monitors for changes

Both tasks are designed to work with the device's event groups for efficient notification.