# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### ⚠️ CRITICAL BEHAVIOR CHANGE - DELAY-Safe API

**All convenience methods are now DELAY-safe by default!**

The RYN4 hardware implements DELAY commands as hardware watchdog timers with absolute priority.
Previously, `turnOffRelay()` would **fail silently** if a DELAY timer was active!

**Before (v0.1.x - BROKEN):**
```cpp
ryn4.turnOffRelay(1);  // ❌ IGNORED if DELAY active! Relay stays ON!
```

**After (v0.2.0 - FIXED):**
```cpp
ryn4.turnOffRelay(1);  // ✅ Always works - cancels DELAY + turns OFF
```

### Added - SAFE Methods (DELAY-aware, always work)
- `turnOnRelay(idx)` - Cancels DELAY + turns ON permanently
- `turnOffRelay(idx)` - Sends DELAY 0, guaranteed OFF
- `toggleRelay(idx)` - Reads state + force opposite (1 read + 1-2 writes)
- `turnOnRelayTimed(idx, seconds)` - ON with hardware watchdog timer
- `turnOnAllTimed(seconds)` - Batch watchdog refresh for safety systems
- `momentaryRelay(idx)` - 1-second pulse (also cancels DELAY)
- `forceOnRelay(idx)`, `forceOffRelay(idx)`, `emergencyStopAll()` - Force control

### Added - DIRECT Methods (fast, may fail with active DELAY)
- `turnOnRelayDirect(idx)` - Simple ON (0x0100), timer not cancelled
- `turnOffRelayDirect(idx)` - Simple OFF (0x0200), **IGNORED if DELAY active!**
- `toggleRelayDirect(idx)` - Simple TOGGLE (0x0300), **IGNORED if DELAY active!**

### Changed
- **BREAKING**: `turnOnRelay()`, `turnOffRelay()`, `toggleRelay()` now use force methods
- **API Rename** - All OPEN/CLOSE terminology changed to ON/OFF for clarity:
  - `RelayAction::OPEN` → `RelayAction::ON`
  - `RelayAction::CLOSE` → `RelayAction::OFF`
  - `RelayAction::OPEN_ALL` → `RelayAction::ALL_ON`
  - `RelayAction::CLOSE_ALL` → `RelayAction::ALL_OFF`

### Documentation
- Added comprehensive DELAY command behavior documentation
- DELAY has absolute priority - only DELAY 0 or MOMENTARY can cancel
- Hardware watchdog use case documented for safety-critical applications

## [0.1.0] - 2025-12-04

### Added - Initial Public Release
- RYN404E/RYN408F relay module driver via Modbus RTU
- 8-channel relay control with thread-safe operations
- Verified relay control methods (automatic state verification)
- Batch relay operations for multiple relays
- Relay reset on initialization (configurable, default: all OFF)
- Offline device detection and protection
- Thread-safe operations with mutex protection
- Event group integration for relay state notifications
- QueuedModbusDevice base class for async operations
- IDeviceInstance interface implementation
- Cached relay states with configurable validity
- Passive responsiveness monitoring
- Configurable async queue depth
- Result<T> based error handling

### Added - Multi-Command API ⭐ **MAJOR FEATURE** (hardware tested 2025-12-07)
- `setMultipleRelayCommands()` - Send different commands to multiple relays atomically
- **ALL command types work in FC 0x10**: ON, OFF, TOGGLE, LATCH, MOMENTARY, DELAY
- **Independent DELAY timing**: Each relay maintains separate hardware timer
- **Mixed commands**: Combine any command types in single operation
- **Performance**: 6-8x faster than sequential commands
- **Atomic execution**: All relays respond simultaneously

### Added - Advanced Configuration API (based on official manufacturer documentation)
- `readDeviceInfo()` - Read device type, firmware version, and configuration
- `verifyHardwareConfig()` - Verify DIP/jumper settings match software
- `readBitmapStatus()` - Efficient batch status read via bitmap register (0x0080)
- `factoryReset()` - Software factory reset command
- `setParity()` - Configure parity (requires power cycle)
- `getReplyDelay()` / `setReplyDelay()` - Configure module response delay

### Added - Hardware Documentation
- Complete HARDWARE.md with manufacturer specs (800+ lines)
- Hardware register constants in HardwareRegisters.h (400+ lines)
- Configuration registers (0x00F0-0x00FF) fully documented
- Status bitmap register (0x0080-0x0081) documented
- Extended baud rate support (9600/19200/38400/115200)
- Function code support matrix (FC 01/03/05/06/0F/10)
- DIP switch and jumper pad configuration guide
- Wiring diagrams and physical specifications
- HARDWARE_TEST_RESULTS.md - FC 0x10 verification testing

### Added - Examples
- examples/hardware_setup/README.md - Complete setup guide with checklist
- examples/hardware_setup/test_communication.ino - Basic validation sketch
- examples/hardware_setup/test_advanced_config.ino - Advanced features demo
- examples/multi_command_example/ - Real-world automation scenarios

### Added - Documentation Updates
- README.md hardware configuration section
- CLAUDE.md hardware protocol reference
- DOCUMENTATION_UPDATES.md tracking all changes
- RELEASE_NOTES_v0.1.1.md feature highlights (internal doc)
- NEW_FEATURES_v0.1.1.md usage guide (internal doc)

### Changed
- **Baud Rate Support** - Updated to 9600/19200/38400/115200 (official manual)
- **Reply Delay Units** - Corrected to 40ms per unit (official manual)
- **Function Code Documentation** - Added FC 01/05/0F support

### Fixed
- **Parity Configuration** - Documented as R/W (requires power cycle)
- **Hardware Register Constants** - Added missing configuration registers (0x00F0-0x00FF)
- **Documentation Accuracy** - All specs match official manufacturer manuals

### Documentation Sources
- Official manufacturer manuals (RYN404E RYN408F Manual.pdf, MODBUS RTU Command.pdf)
- Product page: https://485io.com/rs485-relays
- Hardware testing on RYN408F module (2025-12-07)

### Notes
- Production-tested controlling 8 relays in industrial boiler system
- Verified relay operations ensure state consistency
- Hardware-tested multi-command operations (FC 0x10 with all command types)
- Comprehensive documentation based on official manufacturer manuals
- All features verified on RYN408F hardware (2025-12-07)

Platform: ESP32 (Arduino/ESP-IDF)
Hardware: RYN404E/RYN408F 4/8-channel relay modules (Modbus RTU)
License: GPL-3
Dependencies: ESP32-ModbusDevice, ESP32-IDeviceInstance, MutexGuard, LibraryCommon
