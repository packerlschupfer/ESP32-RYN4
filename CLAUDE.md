# Claude Code Instructions for RYN4 Library

## Library Context
- RYN4 is an ESP32 library for controlling RYN404E/RYN408F relay modules via Modbus RTU
- Uses FreeRTOS for thread safety and event-driven architecture
- Thread safety is critical for all public methods
- Memory management must be explicit (no leaks)
- Stack space is LIMITED - tasks often have only 2048-4096 bytes
- Library is split into 7 files: Core, Device, Modbus, State, Config, Control, Events

## Code Standards
- Use RAII for resource management
- Follow existing naming conventions
- Add comprehensive error logging
- Use Logger::getInstance() singleton pattern
- Prefer snprintf over stringstream for embedded systems
- Use std::array<bool, 8> for relay states (compile-time size checking)

## Unified Hardware Mapping Architecture

The library uses a unified mapping system with pointer bindings for efficient relay control.

### Usage Example:
```cpp
// 1. Create RYN4 instance
RYN4 ryn4(1, "RYN4");

// 2. Set hardware config (constexpr array in flash - zero RAM)
ryn4.setHardwareConfig(ryn4::DEFAULT_HARDWARE_CONFIG.data());

// 3. Bind relay state pointers (library writes directly to your variables)
std::array<bool*, 8> relayPointers = {
    &myRelayStates.heatingPump,
    &myRelayStates.waterPump,
    &myRelayStates.burnerEnable,
    &myRelayStates.halfPower,
    &myRelayStates.waterMode,
    &myRelayStates.valve,
    nullptr,  // spare
    nullptr   // spare
};
ryn4.bindRelayPointers(relayPointers);

// 4. Initialize
ryn4.initialize();

// 5. Control relays
std::array<bool, 8> states = {true, false, true, false, false, false, false, false};
ryn4.setMultipleRelayStates(states);
```

### Architecture Benefits:
- Hardware config stored in flash (zero RAM cost)
- Direct pointer updates (no polling needed)
- Type-safe with std::array<bool, 8>
- Single configuration point for all relay mappings

## MQTT Support
- Core library is MQTT-agnostic
- MQTT support available via separate RYN4-MQTT-Adapter library
- Do not add MQTT dependencies to core library

## Testing Requirements
- Ensure code compiles without warnings
- Verify thread safety in concurrent scenarios
- Check memory allocation/deallocation pairs
- Test error paths and edge cases
- Run with example projects before committing

## Git Commit Standards
- Use conventional commits (fix:, feat:, docs:, refactor:, chore:)
- Make atomic commits (one logical change per commit)
- Include clear descriptions of what and why
- Reference issue numbers if applicable

## Development Workflow
1. Check for uncommitted changes first using `git status`
2. Read recent commit messages to understand context
3. Check CLAUDE.local.md for session-specific notes
4. Make changes following the standards above
5. Test thoroughly before committing

## Architecture Notes
- RYN4 inherits from QueuedModbusDevice
- RelayControlModule provides high-level interface
- Event groups used for relay state notifications
- Mutex protection for all shared resources
- Cached data with configurable validity period
- File organization:
  - RYN4.cpp (362 lines) - Core functionality
  - RYN4Device.cpp (339 lines) - IDeviceInstance interface
  - RYN4Modbus.cpp (268 lines) - Modbus communication
  - RYN4State.cpp (143 lines) - State queries
  - RYN4Config.cpp (270 lines) - Configuration
  - RYN4Control.cpp (647 lines) - Relay control
  - RYN4Events.cpp (71 lines) - Event management