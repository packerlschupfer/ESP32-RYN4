# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.0] - 2026-10-28

### Added - Initial Public Release
- RYN404E/RYN408F relay module driver via Modbus RTU
- 8-channel relay control with thread-safe operations
- Verified relay control methods (automatic state verification)
- Batch relay operations for multiple relays
- Relay reset on initialization (configurable, default: all OFF)
- Thread-safe operations with mutex protection
- Event group integration for relay state notifications
- QueuedModbusDevice base class for async operations
- IDeviceInstance interface implementation
- Cached relay states with configurable validity
- Passive responsiveness monitoring
- Configurable async queue depth
- Result<T> based error handling

### Added - DELAY-Safe Convenience API
The RYN4 hardware implements DELAY commands as hardware watchdog timers with
absolute priority — a plain `OFF` command is ignored while a DELAY timer is
active. The DELAY-safe methods below all first send `DELAY 0` to cancel any
timer, then issue the requested command, so they always work:

- `turnOnRelay(idx)` - Cancels DELAY + turns ON permanently
- `turnOffRelay(idx)` - Sends DELAY 0, guaranteed OFF
- `toggleRelay(idx)` - Reads state + force opposite (1 read + 1-2 writes)
- `turnOnRelayTimed(idx, seconds)` - ON with hardware watchdog timer
- `turnOnAllTimed(seconds)` - Batch watchdog refresh for safety systems
- `momentaryRelay(idx)` - 1-second pulse (also cancels DELAY)
- `forceOnRelay(idx)`, `forceOffRelay(idx)`, `emergencyStopAll()` - Force control

### Added - DIRECT Methods (fast, bypass DELAY handling)
Use when the caller knows no DELAY timer is active and wants minimal latency.
These will be silently ignored by the hardware if a DELAY timer is running —
prefer the DELAY-safe variants above unless you have measured a need:

- `turnOnRelayDirect(idx)` - Simple ON (0x0100), timer not cancelled
- `turnOffRelayDirect(idx)` - Simple OFF (0x0200), ignored if DELAY active
- `toggleRelayDirect(idx)` - Simple TOGGLE (0x0300), ignored if DELAY active

### Added - Multi-Command API (hardware tested 2025-12-07)
- `setMultipleRelayCommands()` - Send different commands to multiple relays
  atomically via FC 0x10
- All command types work in FC 0x10: ON, OFF, TOGGLE, LATCH, MOMENTARY, DELAY
- Independent DELAY timing: each relay maintains its own hardware timer
- Mixed commands: combine any command types in a single operation
- Performance: 6-8x faster than sequential per-relay commands
- Atomic execution: all relays respond simultaneously

### Added - Zero-Allocation Partial Range API
- `setRelayCommandRange(startRelay, count, spec)` - Write the same command to a
  contiguous range of relays via FC 0x10. Variant of
  `setMultipleRelayCommands()` for the "same action across N relays" case
  (watchdog renewal, staggered off, etc.):
  - **0-based `startRelay`** (0 = Relay 1), matching `writeMultipleRegisters()`
    and the `std::array<RelayCommandSpec, 8>` variant.
  - **Zero heap allocation on the hot path**: builds the register payload on a
    fixed-size stack buffer and calls the base class's non-allocating
    primitives directly (`acquireBusMutex` / `ensureSyncReady` /
    `sendRequestInternal` / `waitForResponse`), bypassing the
    `std::vector`-based `writeMultipleRegisters()` overload. Safe at high
    cadence from consumers compiled with `-fno-exceptions`.
  - Lower bus traffic than always writing 8 registers (`2 * count` bytes).
  - Rejects `RelayAction::ALL_ON` / `ALL_OFF` with `INVALID_INDEX` (broadcast
    actions are addr 0x0000 only).
  - Requires ESP32-ModbusDevice v0.1.0 with `ensureSyncReady()` exposed as
    `protected`.

### Added - Runtime Link-Health Tracking
- `isModuleOffline()` now reflects runtime link health, not just the init-time
  probe. `handleModbusError()` increments a consecutive-failure counter and
  latches `statusFlags.moduleOffline` once it hits `OFFLINE_FAILURE_THRESHOLD`
  (3). `handleModbusResponse()` and `onAsyncResponse()` clear both the counter
  and the flag on the next successful response. Covers mid-life module death
  (bus cut, power loss, transceiver fault) where the init probe is no longer
  protective — previously, every post-init write to a dead module was
  reported successful while nothing reached the hardware.
- Recovery: the latched flag short-circuits the public control/read methods
  with `MODBUS_ERROR`, so an application driving recovery must bypass the
  gate — call `probeModule()` (or re-run `initialize()`). `probeModule()`
  always hits the wire and clears the latch on a successful reply via the
  normal response path. Prefer it over `isModuleResponsive()` for recovery:
  `isModuleResponsive()` may take the passive shortcut on a reply observed
  *before* the latch, which is stale evidence relative to current link
  state.
- `getConsecutiveFailures()` - current failure count (0..threshold), for
  callers that want finer-grained signal than the latched flag — e.g. to
  distinguish "the relays disagree with what I commanded" (counter stays 0,
  link is fine) from "the module is not talking" (counter climbing).
- `getLastResponseTime()` - FreeRTOS tick of the last successful response
  from either the sync or async receive path. Combined with
  `xTaskGetTickCount()` this gives callers a "time since last reply"
  liveness signal independent of the latch. Internal `lastResponseTime` is
  now atomic and stamped from both paths (previously only the async path
  updated it).
- `probeModule()` - always-active primitive: issues a bare register read via
  the base class, bypassing both the moduleOffline-latch gate on the
  RYN4-level control/read methods and the passive shortcut inside
  `isModuleResponsive()`. On a successful reply the latch clears via the
  normal response path. This is the recommended primitive when driving
  recovery from a latched `isModuleOffline()` — "is it responsive" (passive,
  cached) and "ask it right now" (active, probing) are different questions,
  and latch recovery wants the second. `isModuleResponsive()` now
  delegates to `probeModule()` for its active check so both paths share
  one implementation.

### Added - DELAY Watchdog Default + Renewal Helper
Convenience for safety-critical consumers that renew the per-command DELAY
watchdog on a cadence:

- `setWatchdogSeconds(uint8_t)` / `getWatchdogSeconds()` - Library-side
  default for the per-command DELAY-watchdog seconds. **Library-side state
  only** — nothing is written to the module. Lets consumers stop hardcoding
  the renewal duration at every callsite. Default 10s. Not the same as
  `setDelayTime()` / `setReplyDelay()`, which configure the module's Modbus
  Return-Delay register (0x00FC).
- `renewWatchdog(uint8_t relayMask = 0xFF)` - Issues `DELAY N` to the relays
  selected by mask using the stored seconds. Single FC 0x10 for the all-8
  case and for any contiguous subrange (via `setRelayCommandRange()`); falls
  back to one FC 0x10 per set bit for non-contiguous masks. Allocation-free
  throughout, so safe at watchdog cadence from `-fno-exceptions` consumers.

### Added - Advanced Configuration API
Based on the official RYN404E / RYN408F manufacturer manuals:

- `readDeviceInfo()` - Read device type, firmware version, and configuration
- `verifyHardwareConfig()` - Verify DIP / jumper settings match software
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
- `examples/hardware_setup/` - Complete setup guide with validation sketches
- `examples/multi_command_example/` - Real-world automation scenarios

### Design Notes
- RelayAction naming uses ON/OFF (and ALL_ON/ALL_OFF for broadcast), not
  OPEN/CLOSE, to match the hardware behavior and avoid relay-polarity
  confusion.
- DELAY has absolute priority on the hardware: only DELAY 0 or MOMENTARY can
  cancel an active DELAY timer. The convenience API treats this as the
  default behavior so callers do not have to track timer state.
- Internal `encodeRelayCommand()` helper is the single source of truth for
  the FC 0x10 wire format; both `setMultipleRelayCommands()` and
  `setRelayCommandRange()` go through it.
- Runtime offline tracking (`isModuleOffline()`) is latched by a
  consecutive-failure counter rather than a per-transaction flag so a single
  transient CRC error does not disable the device; `OFFLINE_FAILURE_THRESHOLD`
  gives three retries before latching.

Platform: ESP32 (Arduino / ESP-IDF, pioarduino / Arduino core 3.3.x)
Hardware: RYN404E / RYN408F 4/8-channel relay modules (Modbus RTU)
License: GPL-3.0-or-later
Dependencies: ESP32-ModbusDevice, ESP32-IDeviceInstance, MutexGuard, LibraryCommon
