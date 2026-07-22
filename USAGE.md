# ESP32-RYN4 Usage Guide

## Quick Start

### Basic Setup with Unified Mapping

```cpp
#include <RYN4.h>

// Your application relay states
struct RelayStates {
    bool heatingPump;
    bool waterPump;
    bool burnerEnable;
    bool halfPower;
    bool waterMode;
    bool valve;
} myRelays;

void setup() {
    // 1. Create RYN4 instance (Modbus address 1)
    RYN4 ryn4(1, "RYN4");

    // 2. Set hardware configuration (constexpr in flash - zero RAM)
    ryn4.setHardwareConfig(ryn4::DEFAULT_HARDWARE_CONFIG.data());

    // 3. Bind relay state pointers
    //    Library will write directly to these variables
    std::array<bool*, 8> relayPointers = {
        &myRelays.heatingPump,
        &myRelays.waterPump,
        &myRelays.burnerEnable,
        &myRelays.halfPower,
        &myRelays.waterMode,
        &myRelays.valve,
        nullptr,  // relay 7 unused
        nullptr   // relay 8 unused
    };
    ryn4.bindRelayPointers(relayPointers);

    // 4. Initialize device
    auto result = ryn4.initialize();
    if (result.isError()) {
        Serial.println("RYN4 initialization failed!");
        return;
    }

    Serial.println("RYN4 ready!");
}

void loop() {
    // Control single relay - convenience methods (recommended)
    ryn4.turnOnRelay(1);    // Turn on relay 1
    ryn4.turnOffRelay(1);   // Turn off relay 1
    ryn4.toggleRelay(1);    // Toggle relay 1

    // Or use generic control for dynamic actions
    ryn4.controlRelay(1, ryn4::RelayAction::ON);

    // Control multiple relays at once (atomic operation)
    std::array<bool, 8> states = {
        true,   // Relay 1: ON
        false,  // Relay 2: OFF
        true,   // Relay 3: ON
        false,  // Relay 4: OFF
        false,  // Relay 5: OFF
        false,  // Relay 6: OFF
        false,  // Relay 7: OFF
        false   // Relay 8: OFF
    };
    ryn4.setMultipleRelayStates(states);

    // Read relay states (automatically updated via bound pointers!)
    Serial.printf("Heating Pump: %s\n", myRelays.heatingPump ? "ON" : "OFF");
    Serial.printf("Water Pump: %s\n", myRelays.waterPump ? "ON" : "OFF");

    delay(1000);
}
```

## Key Concepts

### 1. Hardware Configuration (Flash)
```cpp
// Defined in ryn4/RelayDefs.h
ryn4::DEFAULT_HARDWARE_CONFIG  // Constexpr array with event bits
```

### 2. Runtime Bindings (RAM)
```cpp
std::array<bool*, 8> pointers;  // Only 32 bytes RAM
```

### 3. Direct Pointer Updates
When relay states change (via Modbus or commands), the library writes directly to your bound pointers. No polling required!

## Memory Usage

- **Hardware Config**: Lives in flash (zero RAM)
- **Runtime Pointers**: 32 bytes (8 pointers × 4 bytes)
- **Internal State**: ~10 bytes per relay

## Thread Safety

All public methods are thread-safe with mutex protection.
