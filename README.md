# RYN4 Relay Control Library

A comprehensive Modbus RTU library for controlling RYN404E (4-channel) and RYN408F (8-channel) relay modules with FreeRTOS support.

## Features

### Core Capabilities
- Individual relay control (open/close/toggle/latch/momentary)
- Batch relay operations with atomic updates
- State verification and confirmation
- Direct relay state getter methods with thread-safe access
- Event-driven architecture with FreeRTOS event groups
- Thread-safe operations with mutex protection
- Comprehensive error handling and logging
- Cached data access for performance
- Modular architecture with logical file separation
- Safe initialization with relay reset (all relays OFF by default)

### ⭐ Advanced Features

**Multi-Command Atomic Operations** - **Hardware Tested!**
- Send **different command types** to multiple relays in single operation
- **ALL commands work in FC 0x10**: OPEN, CLOSE, TOGGLE, LATCH, MOMENTARY, DELAY
- **Independent DELAY timing**: Each relay maintains separate hardware timer
- **6-8x faster** than sequential commands
- Perfect for complex automation sequences

```cpp
// Example: Mixed commands in one atomic operation
std::array<RYN4::RelayCommandSpec, 8> commands = {
    {RelayAction::OPEN, 0},       // Relay 1: ON permanently
    {RelayAction::MOMENTARY, 0},  // Relay 2: 1-sec pulse
    {RelayAction::DELAY, 10},     // Relay 3: 10-sec delay
    {RelayAction::TOGGLE, 0},     // Relay 4: Toggle state
    {RelayAction::CLOSE, 0}       // Relay 5-8: OFF
    // ...
};
ryn4.setMultipleRelayCommands(commands);  // All execute simultaneously!
```

**Advanced Configuration & Diagnostics**
- Auto-detect device type and firmware version
- Verify hardware DIP/jumper configuration
- Efficient bitmap status reading (8x faster)
- Factory reset, parity configuration, reply delay tuning

See [HARDWARE_TEST_RESULTS.md](HARDWARE_TEST_RESULTS.md) for complete testing documentation.

## Hardware Configuration

### Physical Setup

The RYN4 relay modules require proper hardware configuration before use:

1. **Set Slave ID** using DIP switches A0-A5 (range: 0x00 to 0x3F)
   - 6-bit address selection supporting up to 64 devices on one bus
   - Example: For slave ID 0x01, set only A0 to ON, others OFF

2. **Select Baud Rate** via jumper pads M1/M2
   - **9600 BPS** (default): M0=OPEN, M1=OPEN, M2=OPEN
   - 2400 BPS: M0=OPEN, M1=SHORT, M2=OPEN
   - 4800 BPS: M0=OPEN, M1=OPEN, M2=SHORT
   - 19200 BPS: M0=OPEN, M1=SHORT, M2=SHORT

3. **Keep M0 OPEN** for Modbus RTU mode (library requirement)
   - M0=OPEN: Modbus RTU (supported by this library) ✅
   - M0=SHORT: AT Command mode (NOT supported) ❌

**Complete hardware documentation:** [HARDWARE.md](HARDWARE.md)

### Register Map and Protocol

The RYN4 uses Modbus RTU with:
- **Function Code 0x06**: Write Single Register (control)
- **Function Code 0x03**: Read Holding Registers (status)
- **Registers**: 0x0001-0x0008 for channels, 0x0000 for broadcast

**⚠️ Critical Protocol Detail:** Command values differ from status values!

```cpp
// Sending commands (write values)
ryn4.controlRelay(1, RelayAction::OPEN);  // Sends 0x0100 to hardware

// Reading status (read values)
bool state;
ryn4.readRelayStatus(1, state);  // Receives 0x0001 (ON) or 0x0000 (OFF) from hardware
```

This asymmetry is a hardware protocol characteristic. The library handles conversion automatically.

**Full protocol specification:** [HARDWARE.md](HARDWARE.md)
**Hardware register constants:** [src/ryn4/HardwareRegisters.h](src/ryn4/HardwareRegisters.h)

## Verified Relay Control Methods

The RYN4 library now provides public verified relay control methods that automatically confirm state changes without requiring manual verification:

### Available Verified Methods

```cpp
// Control single relay with automatic verification
RelayErrorCode controlRelayVerified(uint8_t relayIndex, RelayAction action);

// Set multiple relays with verification
RelayErrorCode setMultipleRelayStatesVerified(const std::vector<bool>& states);

// Convenience method for simple ON/OFF with verification
RelayErrorCode setRelayStateVerified(uint8_t relayIndex, bool state);

// Set all relays to same state with verification
RelayErrorCode setAllRelaysVerified(bool state);
```

### Usage Example

```cpp
// Old approach - manual verification
ryn4->controlRelay(1, RelayAction::CLOSE);
vTaskDelay(pdMS_TO_TICKS(100));
bool state;
ryn4->readRelayStatus(1, state);

// New approach - automatic verification
auto result = ryn4->controlRelayVerified(1, RelayAction::CLOSE);
if (result == RelayErrorCode::SUCCESS) {
    // Relay successfully changed state
}
```

See the [verified_control example](examples/verified_control/) for detailed usage.

## Relay State Getter Methods

The RYN4 library provides thread-safe methods to query the current state of relays without needing to maintain separate cached state:

### Available Getter Methods

```cpp
// Get state of a single relay
RelayResult<bool> getRelayState(uint8_t relayIndex) const;

// Get states of all relays at once
RelayResult<std::vector<bool>> getAllRelayStates() const;
```

### Usage Example

```cpp
// Get single relay state
auto result = ryn4->getRelayState(1);
if (result.isOk()) {
    Serial.printf("Relay 1 is %s\n", result.value ? "ON" : "OFF");
}

// Get all relay states efficiently
auto allStates = ryn4->getAllRelayStates();
if (allStates.isOk()) {
    for (size_t i = 0; i < allStates.value.size(); i++) {
        Serial.printf("Relay %d: %s\n", i + 1, allStates.value[i] ? "ON" : "OFF");
    }
}

// Use in HAL implementation
State getState(uint8_t channel) const override {
    auto result = device->getRelayState(channel + 1);  // Convert 0-based to 1-based
    if (result.isOk()) {
        return result.value ? State::ON : State::OFF;
    }
    return State::UNKNOWN;
}
```

### Benefits
- No need to maintain separate state tracking
- Thread-safe with mutex protection
- Consistent with RelayResult error handling pattern
- Single source of truth for relay states

See the [RelayStateGetterExample](examples/RelayStateGetterExample/) for detailed usage.

## Initialization Behavior

By default, RYN4 resets all relays to OFF during initialization for safety:

```cpp
RYN4* ryn4 = new RYN4(slaveAddress);
ryn4->initialize();  // All relays turn OFF
```

To preserve existing relay states:

```cpp
RYN4::InitConfig config;
config.resetRelaysOnInit = false;
ryn4->initialize(config);  // Relays maintain current state
```

## Dependencies

### Required Dependencies
- ModbusDevice
- esp32ModbusRTU

### Optional Dependencies
- Logger (for custom logging support)

## Logging Configuration

The RYN4 library supports flexible logging configuration that is C++11 compatible and provides zero-overhead when custom logging is not used.

### Using ESP-IDF Logging (Default)

No configuration needed. The library automatically uses ESP-IDF logging:

```cpp
#include "RYN4.h"

void setup() {
    // Library logs will appear with "RYN4" tag in ESP-IDF format
    RYN4 relay(2);
    relay.begin();
}
```

### Using Custom Logger

Define `USE_CUSTOM_LOGGER` in your build flags:

```ini
[env:myenv]
lib_deps = 
    Logger
    RYN4
    
build_flags = 
    -D USE_CUSTOM_LOGGER  # Enable custom logging for all libraries
```

In your main.cpp:
```cpp
#ifdef USE_CUSTOM_LOGGER
    #include <LogInterfaceImpl.h>  // Include ONCE in your project
#endif

#include "RYN4.h"

void setup() {
    Serial.begin(115200);
    
    #ifdef USE_CUSTOM_LOGGER
    // Initialize Logger with custom settings
    Logger& logger = Logger::getInstance();
    logger.init(1024);  // 1KB buffer
    logger.setLogLevel(ESP_LOG_DEBUG);
    logger.enableLogging(true);
    #endif
    
    // RYN4 will now use custom Logger
    RYN4 relay(2);
    relay.begin();
}
```

### Debug Logging

To enable debug/verbose logging for the RYN4 library:

```ini
build_flags = 
    -D RYN4_DEBUG  # Enable debug logging for RYN4
```

With debug enabled, you'll see:
- Detailed Modbus protocol messages
- Relay state debugging information
- Performance timing measurements

### Complete Example with All Options

```ini
[env:debug]
build_flags = 
    -D USE_CUSTOM_LOGGER  # Use custom logger instead of ESP-IDF
    -D RYN4_DEBUG         # Enable debug logging for RYN4
```

### Include Path Convention

When including headers from external libraries, always use angle brackets:

```cpp
// Correct - for external libraries
#include <LogInterface.h>
#include <IDeviceInstance.h>
#include <MutexGuard.h>

// Correct - for internal library files
#include "RYN4.h"
#include "RelayControlModule.h"
```

This ensures proper path resolution and build reliability across different projects.

### Log Levels

The library uses different log levels:
- **ERROR**: Critical failures requiring immediate attention
- **WARN**: Important issues that don't stop operation
- **INFO**: Relay state changes and major operations
- **DEBUG**: Detailed operation information (only with RYN4_DEBUG)
- **VERBOSE**: Very detailed traces (only with RYN4_DEBUG)

### Memory Usage

- **With ESP-IDF logging**: No additional memory overhead
- **With custom Logger**: ~17KB for the Logger singleton (shared across all libraries)
- **Debug logging**: Adds minimal overhead only when RYN4_DEBUG is defined

## MQTT Support

For MQTT integration, please use the separate [RYN4-MQTT-Adapter](https://github.com/your-username/RYN4-MQTT-Adapter) library which provides:
- MQTT message handling for relay control
- Topic-based relay management
- Status reporting via MQTT
- Zero impact on core relay functionality when not used

The adapter follows the same logging pattern and will use your configured logging system automatically.

## Project Structure

The RYN4 library is organized into 7 logical files for better maintainability:

- **RYN4.cpp** (362 lines) - Core class implementation, constructors, and resource management
- **RYN4Device.cpp** (339 lines) - IDeviceInstance interface implementation for device ecosystem integration
- **RYN4Modbus.cpp** (268 lines) - Modbus communication, response handling, and protocol implementation
- **RYN4State.cpp** (143 lines) - State queries, status methods, and cache management
- **RYN4Config.cpp** (270 lines) - Configuration methods, settings management, and parameter conversion
- **RYN4Control.cpp** (647 lines) - Relay control operations, batch operations, and state verification
- **RYN4Events.cpp** (71 lines) - FreeRTOS event group management and notification handling

This modular structure improves:
- **Compilation speed** - Only modified files need recompilation
- **Code navigation** - Easy to find specific functionality
- **Testing** - Individual components can be tested in isolation
- **Maintenance** - Clear separation of concerns

---
### Integrated Documentation: Using `mbpoll` with RYN404E and RYN408F Relay Devices

---

### General Device Information
- **Models**: RYN404E (4 channels) and RYN408F (8 channels).
- **Output Type**: Relay with Normally Open (NO), Normally Closed (NC), and COM terminals.

---

### Supported Function Codes
| Function Code | Address Range | Description                                  |
|---------------|---------------|----------------------------------------------|
| `0x01`        | `0x0000-0x001F` | Read digital output (DO) status (relay).    |
| `0x05`        | `0x0000-0x001F` | Write single DO status.                     |
| `0x0F`/`0x15` | `0x0000-0x001F` | Write multiple DO statuses.                 |
| `0x03`        | `0x0080-0x00FF` | Read special function registers.            |
| `0x06`        | `0x0080-0x00FF` | Write single special function register.     |
| `0x10`/`0x16` | `0x0080-0x00FF` | Write multiple special function registers.  |

---

### Register Mappings
1. **DO Digital Outputs**
   - Address: `0x0000-0x001F` (one register per channel).
   - Commands:
     - Open: `0x0100`
     - Close: `0x0200`
     - Self-locking (Toggle): `0x0300`
     - Interlock: `0x0400`
     - Momentary: `0x0500`
     - Delay: `0x06XX` (XX = time in seconds, `0x00-0xFF`).
     - Open All: `0x0700`
     - Close All: `0x0800`.

2. **Special Function Registers**
   | Address  | Description                  | Values/Notes                                 |
   |----------|------------------------------|---------------------------------------------|
   | `0x00FB` | Factory Reset                | Command: `FF 06 00 FB 00 00 ED E5`.         |
   | `0x00FC` | Command Return Time          | `0-25` (unit: 40 ms).                       |
   | `0x00FD` | RS485 Address (Slave ID)     | Read-only. Configured via DIP switches.     |
   | `0x00FE` | Baud Rate                    | `0`: 9600, `1`: 19200, `2`: 38400, `3`: 115200. |
   | `0x00FF` | Parity                       | `0`: None, `1`: Even, `2`: Odd.            |

---

### Examples of Modbus Commands
#### 1. Reading DO Status
   - Address: `0x0000-0x001F`.
   - Example:
     ```
     Send: 01 01 00 00 00 08 3D CC
     Response: 01 01 01 7C 50 69
     ```

#### 2. Writing DO Status
   - Single DO:
     - Address: `0x0000-0x0007`.
     - Example: Turn Channel 1 ON.
       ```
       Send: 01 05 00 00 FF 00 8C 3A
       Response: 01 05 00 00 FF 00 8C 3A
       ```
   - Multiple DO:
     - Address: `0x0000-0x0007`.
     - Example: Turn Channels 0-8 OFF.
       ```
       Send: 01 0F 00 00 00 08 01 00 FE 95
       Response: 01 0F 00 00 00 08 54 0D
       ```

#### 3. Special Register Commands
   - Setting Command Return Delay (200 ms):
     ```
     Send: 01 06 00 FC 00 05 89 F9
     Response: 01 06 00 FC 00 05 89 F9
     ```
   - Setting Even Parity:
     ```
     Send: 01 06 00 FF 00 01 78 3A
     Response: 01 06 00 FF 00 01 78 3A
     ```

---

### Using `mbpoll` for Device Interaction

#### **Controlling Relays**

##### **Set a Single Relay**
To toggle (self-lock) a single relay, write `0x0300` to its respective register:

```bash
mbpoll -q -m rtu -a 2 -r 1 -t 4:hex -b 9600 -P none /dev/ttyUSB485 0x0300
```

- Replace `-r 1` with the register address for the specific relay (e.g., `1` for the second relay).

---

##### **Toggle a Single Relay**
Use the `Self-locking` (Toggle) command (`0x0300`) to toggle a specific relay:

```bash
mbpoll -q -m rtu -a 2 -r 1 -t 4:hex -b 9600 -P none /dev/ttyUSB485 0x0300
```

- Replace `-r 1` with the register address of the relay (`0x0000-0x0007` for 8 relays).
- `-a 2`: Slave address (adjust based on your DIP switch settings).

---
##### **Set Multiple Relays at Once**
To toggle all 8 relays at once, write `0x0300` to all 8 registers sequentially:

```bash
mbpoll -q -m rtu -a 2 -r 1 -t 4:hex -b 9600 -P none /dev/ttyUSB485 0x0300 0x0300 0x0300 0x0300 0x0300 0x0300 0x0300 0x0300
```

- **Explanation**:
  - `-r 1`: Starting register address (Relay 2).
  - Each `0x0300` represents the toggle command for a specific relay.
  - This command writes to 8 consecutive registers (for 8 relays).

**Example Output**:
```
Written 8 references.
```

---

##### **Toggle All Relays with a for loop**
Iterate through all relays (`0x0000` to `0x0007`) to toggle them one by one:

```bash
for i in {1..8}; do
  mbpoll -q -m rtu -a 2 -r $i -t 4:hex -b 9600 -P none /dev/ttyUSB485 0x0300
  sleep 1
done
```

---

#### **Reading Relay Status**

##### **Read a Single Relay Status**
Read the status of a specific relay. The response indicates if the relay is `ON` (`0x0100`) or `OFF` (`0x0000`):

```bash
mbpoll -q -1 -m rtu -a 2 -r 1 -t 4:hex -b 9600 -P none /dev/ttyUSB485
```

- Replace `-r 1` with the relay's register address (`0x0000-0x0007`).

---

##### **Read All Relays at Once**
Retrieve the status of all relays in one command:

```bash
mbpoll -q -1 -m rtu -a 2 -r 1 -c 8 -t 4:hex -b 9600 -P none /dev/ttyUSB485
```

- `-r 1`: Starting register (first relay).
- `-c 8`: Number of registers to read (total relays).

---

#### **Special Commands**

##### **Factory Reset**
Reset the relay device to factory settings by writing to register `0x00FB`:

```bash
mbpoll -q -m rtu -a 2 -r 251 -t 4:hex -b 9600 -P none /dev/ttyUSB485 0x0000
```

##### **Set Return Delay**
Set the command return delay to `200 ms`:

```bash
mbpoll -q -m rtu -a 2 -r 252 -t 4:hex -b 9600 -P none /dev/ttyUSB485 0x0005
```

---

### **Example Responses**

**Read All Relays**:
```bash
mbpoll -q -1 -m rtu -a 2 -r 1 -c 8 -t 4:hex -b 9600 -P none /dev/ttyUSB485
```

Output:
```
[0]: 0x0100
[1]: 0x0000
[2]: 0x0100
[3]: 0x0000
[4]: 0x0000
[5]: 0x0000
[6]: 0x0100
[7]: 0x0000
```

- `[0]: 0x0100` indicates Relay 1 is ON.
- `[1]: 0x0000` indicates Relay 2 is OFF.

---

### Communication Settings
- **Default**: 9600 baud, 8 data bits, None parity, 1 stop bit.
- **Configurable**: Via DIP switches for RS485 address and baud rate.

---

### Troubleshooting

1. **Communication Timeout**
   - Increase timeout with:
     ```bash
     mbpoll -q -t 1000 -m rtu -a 2 -r 1 -t 4:hex -b 9600 -P none /dev/ttyUSB485
     ```

2. **Incorrect Slave Address**
   - Ensure DIP switches match the specified slave address.

3. **No Response**
   - Confirm baud rate, parity, and stop bits match your device configuration.
   - Try resetting the device to factory settings.

4. **CRC Errors**
   - Check the RS485 connection and ensure proper cable shielding.

---

## Suggested Improvements

The following improvements are recommended for future development:

### Performance Optimizations
1. **Batch Operation Optimization**: Implement optimized batch read/write operations to reduce Modbus overhead
2. **Cache Management**: Add configurable cache TTL and smart invalidation strategies
3. **Event Coalescing**: Implement event batching to reduce task switching overhead

### Advanced Features
1. **Relay Scheduling**: Add time-based relay control with cron-like syntax
2. **Energy Monitoring**: Integrate power consumption tracking per relay
3. **Web Interface**: Create embedded web server for relay control and monitoring
4. **Relay Groups**: Support logical grouping of relays for coordinated control
5. **Failsafe Modes**: Implement configurable failsafe states on communication loss

### Testing and Reliability
1. **Unit Tests**: Create comprehensive unit test suite with mocking
2. **Integration Tests**: Add FreeRTOS-specific integration tests
3. **Stress Testing**: Implement stress tests for concurrent access scenarios
4. **Memory Leak Detection**: Add automated memory leak detection

### Code Quality
1. **RAII Improvements**: Replace manual mutex operations with RAII lock guards
2. **Error Recovery**: Implement automatic retry mechanisms with exponential backoff
3. **Diagnostic Mode**: Add detailed diagnostic logging for troubleshooting
4. **API Documentation**: Generate Doxygen documentation automatically

### Hardware Support
1. **Auto-detection**: Implement automatic detection of RYN404E vs RYN408F
2. **Multi-slave Support**: Add support for controlling multiple RYN4 devices
3. **Hardware Abstraction**: Create HAL layer for easier porting

---
