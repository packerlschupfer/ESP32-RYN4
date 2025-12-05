# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.0] - 2025-12-04

### Added
- Initial public release
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

Platform: ESP32 (Arduino/ESP-IDF)
Hardware: RYN404E/RYN408F 4/8-channel relay modules (Modbus RTU)
License: MIT
Dependencies: ESP32-ModbusDevice, ESP32-IDeviceInstance, MutexGuard, LibraryCommon

### Notes
- Production-tested controlling 8 relays in industrial boiler system
- Verified relay operations ensure state consistency
- Previous internal versions (v1.x) not publicly released
- Reset to v0.1.0 for clean public release start
