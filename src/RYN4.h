#ifndef RYN4_RELAY_H
#define RYN4_RELAY_H

#pragma once

/**
 * @file RYN4.h
 * @brief RYN4 Relay Control Module - Modbus RTU Interface
 * 
 * This library provides comprehensive control for RYN404E (4-channel) and RYN408F (8-channel)
 * relay modules via Modbus RTU protocol. Features include:
 * 
 * - Individual relay control (open/close/toggle/latch/momentary)
 * - Batch relay operations with atomic updates
 * - State verification and confirmation
 * - Event-driven architecture with FreeRTOS event groups
 * - Thread-safe operations with mutex protection
 * - Comprehensive error handling and logging
 * - Cached data access for performance
 * 
 * @version 1.1.0
 * @author Your Name
 * @license MIT
 */

#include "RYN4Logging.h"
#include <QueuedModbusDevice.h>
#include <IDeviceInstance.h>
#include <CommonModbusDefinitions.h>
#include "base/BaseRelayMapping.h"
#include "ryn4/RelayDefs.h"
#include "Result.h"  // common::Result from LibraryCommon
#include <cstdint>
#include <set>
#include <vector>
#include <sstream>
#include <iomanip>
#include <functional>
#include <memory>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"


extern EventBits_t relayAllUpdateBits;
extern EventBits_t relayAllErrorBits;

namespace ryn4 {
    /**
     * @brief Result type for relay operations using common::Result
     * @tparam T The type of the value on success
     *
     * Uses common::Result from LibraryCommon with RelayErrorCode as error type.
     */
    template<typename T>
    using RelayResult = common::Result<T, RelayErrorCode>;
}

/**
 * @class RYN4
 * @brief Main class for controlling RYN404E/RYN408F relay modules
 * 
 * The RYN4 class inherits from ModbusDevice and implements the IDeviceInstance
 * interface, providing a complete solution for relay control via Modbus RTU.
 * 
 * @note Thread-safe operations are ensured through mutex protection
 * @note Event groups are used for efficient task notification
 */
class RYN4 : public modbus::QueuedModbusDevice, public IDeviceInstance {
public:
    using RelayAction = ryn4::RelayAction;
    using RelayMode = ryn4::RelayMode;
    using RelayErrorCode = ryn4::RelayErrorCode;
    using Relay = ryn4::Relay;

    struct RelayActionInfo {
        ryn4::RelayAction action;
        ryn4::RelayErrorCode errorCode;
    };
    
    /**
     * @brief Configuration options for RYN4 initialization
     */
    struct InitConfig {
        bool resetRelaysOnInit = true;  // Default: reset all relays to OFF for safety
        bool skipRelayStateRead = false; // Default: read relay states during init
    };

    /**
     * @brief Construct a new RYN4 object
     * @param slaveID Modbus slave ID
     * @param tag Optional tag for logging
     * @param queueDepth Queue depth for async operations (default: 5, test/burst: 10)
     */
    explicit RYN4(uint8_t slaveID, const char* tag = "RYN4", uint8_t queueDepth = 5);
    ~RYN4() override; // Ensure correct overriding

    // Core initialization and status methods
    /**
     * @brief Check if the module has been initialized
     * @return true if initialization is complete
     */
    bool isInitialized() const noexcept;
    
    
    /**
     * @brief Wait for module initialization to complete (RYN4-specific)
     * @param timeout Maximum time to wait (default: 1000ms)
     * @return RelayResult<void> with error code if timeout or other error
     */
    ryn4::RelayResult<void> waitForModuleInitComplete(TickType_t timeout = pdMS_TO_TICKS(1000));
    
    /**
     * @brief Check if the module is responsive to Modbus commands
     * @return true if module responds to test command
     */
    bool isModuleResponsive();
    
    /**
     * @brief Check if the module is offline/unresponsive
     * @return true if module was detected as offline during initialization
     */
    bool isModuleOffline() const { return statusFlags.moduleOffline; }
    
    // IDeviceInstance interface - make these public for SystemInitializer
    IDeviceInstance::DeviceResult<void> initialize() override;
    IDeviceInstance::DeviceResult<void> initialize(const InitConfig& config);
    IDeviceInstance::DeviceResult<void> waitForInitializationComplete(TickType_t timeout = portMAX_DELAY) override;
    
    // Public access for RelayControlTask
    IDeviceInstance::DeviceResult<std::vector<float>> getData(IDeviceInstance::DeviceDataType dataType) override;
    SemaphoreHandle_t getMutexInterface() const noexcept override { return interfaceMutex; }
    EventGroupHandle_t getUpdateEventGroup() const;
    ryn4::RelayErrorCode setMultipleRelayStates(const std::array<bool, 8>& states);

    /**
     * @brief Command specification for multi-command operations
     *
     * Specifies the action and optional delay for a single relay.
     */
    struct RelayCommandSpec {
        ryn4::RelayAction action;  ///< Command to execute (OPEN, CLOSE, TOGGLE, LATCH, MOMENTARY, DELAY)
        uint8_t delaySeconds;      ///< Delay duration (used only for DELAY action, 0-255 seconds)

        // Constructors for convenience
        RelayCommandSpec() : action(ryn4::RelayAction::CLOSE), delaySeconds(0) {}
        RelayCommandSpec(ryn4::RelayAction act) : action(act), delaySeconds(0) {}
        RelayCommandSpec(ryn4::RelayAction act, uint8_t delay) : action(act), delaySeconds(delay) {}
    };

    /**
     * @brief Set multiple relays with different commands in single atomic operation
     *
     * **HARDWARE TESTED (2025-12-07)**: All command types work in FC 0x10!
     *
     * This method uses FC 0x10 (Write Multiple Registers) to send different commands
     * to multiple relays simultaneously. Supported commands:
     * - OPEN: Relay stays ON
     * - CLOSE: Relay turns OFF
     * - TOGGLE: Toggles current state
     * - LATCH: Inter-locking (last LATCH command wins)
     * - MOMENTARY: 1-second pulse
     * - DELAY: ON for specified seconds, then OFF (independent timing per relay)
     *
     * @param commands Array of 8 command specifications (one per relay)
     * @return RelayErrorCode SUCCESS if all commands sent successfully
     *
     * **Performance**: 6-8x faster than sequential commands (atomic execution)
     *
     * **Example - Complex automation sequence:**
     * @code
     * std::array<RYN4::RelayCommandSpec, 8> commands = {
     *     {RelayAction::OPEN, 0},       // Relay 1: Pump ON permanently
     *     {RelayAction::MOMENTARY, 0},  // Relay 2: Valve pulse (1 sec)
     *     {RelayAction::DELAY, 10},     // Relay 3: Burner ON for 10 sec
     *     {RelayAction::TOGGLE, 0},     // Relay 4: Toggle fan
     *     {RelayAction::CLOSE, 0},      // Relay 5-8: OFF
     *     {RelayAction::CLOSE, 0},
     *     {RelayAction::CLOSE, 0},
     *     {RelayAction::CLOSE, 0}
     * };
     * auto result = ryn4.setMultipleRelayCommands(commands);
     * @endcode
     *
     * **Example - Staggered motor start:**
     * @code
     * std::array<RYN4::RelayCommandSpec, 8> staggered = {
     *     {RelayAction::DELAY, 5},   // Motor 1: Start after 5s
     *     {RelayAction::DELAY, 10},  // Motor 2: Start after 10s
     *     {RelayAction::DELAY, 15},  // Motor 3: Start after 15s
     *     {RelayAction::DELAY, 20},  // Motor 4: Start after 20s
     *     {RelayAction::CLOSE, 0},   // Others: OFF
     *     {RelayAction::CLOSE, 0},
     *     {RelayAction::CLOSE, 0},
     *     {RelayAction::CLOSE, 0}
     * };
     * ryn4.setMultipleRelayCommands(staggered);
     * // Prevents power supply overload from simultaneous start
     * @endcode
     *
     * **Example - Simultaneous pulses:**
     * @code
     * std::array<RYN4::RelayCommandSpec, 8> pulseAll = {
     *     {RelayAction::MOMENTARY, 0}, {RelayAction::MOMENTARY, 0},
     *     {RelayAction::MOMENTARY, 0}, {RelayAction::MOMENTARY, 0},
     *     {RelayAction::MOMENTARY, 0}, {RelayAction::MOMENTARY, 0},
     *     {RelayAction::MOMENTARY, 0}, {RelayAction::MOMENTARY, 0}
     * };
     * ryn4.setMultipleRelayCommands(pulseAll);
     * // All relays pulse simultaneously - great for testing!
     * @endcode
     */
    ryn4::RelayErrorCode setMultipleRelayCommands(const std::array<RelayCommandSpec, 8>& commands);

    ryn4::RelayErrorCode controlRelay(uint8_t relayIndex, ryn4::RelayAction action);
    ryn4::RelayErrorCode readRelayStatus(uint8_t relayIndex, bool& state);
    ryn4::RelayErrorCode readAllRelayStatus();
    bool waitForData() override;
    
    // Relay configuration - Verified (with state confirmation)
    /**
     * @brief Control a single relay with state verification
     * 
     * Sends the control command and then reads back the relay state to verify
     * it reached the commanded state. Sets isStateConfirmed flag.
     * 
     * @param relayIndex Relay number (1-8)
     * @param action Action to perform (OPEN, CLOSE, TOGGLE, etc.)
     * @return RelayErrorCode SUCCESS if command sent and state verified
     */
    ryn4::RelayErrorCode controlRelayVerified(uint8_t relayIndex, ryn4::RelayAction action);
    
    /**
     * @brief Set multiple relay states with verification
     *
     * Sends multi-register write command and then reads back all relay states
     * to verify they reached the commanded states. Sets isStateConfirmed flags.
     *
     * @param states Array of desired states (true=ON, false=OFF) for all 8 relays
     * @return RelayErrorCode SUCCESS if all relays reached commanded states
     */
    ryn4::RelayErrorCode setMultipleRelayStatesVerified(const std::array<bool, 8>& states);
    
    /**
     * @brief Convenience function to set and verify a single relay state
     * 
     * @param relayIndex Relay number (1-8)
     * @param state Desired state (true=ON, false=OFF)
     * @return RelayErrorCode SUCCESS if relay reached commanded state
     */
    ryn4::RelayErrorCode setRelayStateVerified(uint8_t relayIndex, bool state);
    
    /**
     * @brief Set all relays to the same state with verification
     * 
     * @param state Desired state for all relays (true=ON, false=OFF)
     * @return RelayErrorCode SUCCESS if all relays reached commanded state
     */
    ryn4::RelayErrorCode setAllRelaysVerified(bool state);
    
    // Relay state getter methods
    /**
     * @brief Get the current state of a relay
     * 
     * Returns the internally cached state of the relay. This is thread-safe
     * and reflects the last known state from either control commands or
     * status reads.
     * 
     * @param relayIndex Relay number (1-8)
     * @return RelayResult<bool> SUCCESS with true=ON, false=OFF
     */
    ryn4::RelayResult<bool> getRelayState(uint8_t relayIndex) const;
    
    /**
     * @brief Get the current state of all relays
     *
     * Returns an array containing the states of all 8 relays in order.
     * This is more efficient than calling getRelayState multiple times.
     *
     * @return RelayResult<std::array<bool, 8>> SUCCESS with array of relay states
     */
    ryn4::RelayResult<std::array<bool, 8>> getAllRelayStates() const;
    
    // Configuration request methods
    bool reqReturnDelay();
    bool reqAddress();
    bool reqBaudRate();
    bool reqParity();

    // Settings access and conversion
    static BaudRate getBaudRateEnum(uint8_t rawValue);
    BaudRate getStoredBaudRate();
    static Parity getParityEnum(uint8_t rawValue);
    Parity getStoredParity();

    // ========== New Configuration API (from official manual) ==========

    /**
     * @brief Device information structure
     *
     * Contains hardware identification and current configuration read from
     * the module's configuration registers (0x00F0-0x00FF).
     */
    struct DeviceInfo {
        uint16_t deviceType;          ///< Hardware model ID (from 0x00F0)
        uint8_t firmwareMajor;         ///< Firmware major version (from 0x00F1)
        uint8_t firmwareMinor;         ///< Firmware minor version (from 0x00F2)
        uint8_t configuredAddress;     ///< DIP switch slave ID (from 0x00FD)
        uint32_t configuredBaudRate;   ///< DIP switch baud rate (from 0x00FE)
        uint8_t configuredParity;      ///< Parity setting (from 0x00FF)
        uint16_t replyDelayMs;         ///< Response delay in ms (from 0x00FC)
    };

    /**
     * @brief Read complete device identification and configuration
     *
     * Reads all configuration registers from the module to provide
     * hardware identification and current settings. Useful for:
     * - Verifying hardware configuration matches software
     * - Auto-detecting module type and firmware version
     * - Diagnostics and troubleshooting
     *
     * @return RelayResult<DeviceInfo> with device information on success
     *
     * @code
     * auto info = ryn4.readDeviceInfo();
     * if (info.isOk()) {
     *     Serial.printf("Device Type: 0x%04X\n", info.value.deviceType);
     *     Serial.printf("Firmware: v%d.%d\n", info.value.firmwareMajor, info.value.firmwareMinor);
     *     Serial.printf("Slave ID: %d\n", info.value.configuredAddress);
     *     Serial.printf("Baud Rate: %lu\n", info.value.configuredBaudRate);
     * }
     * @endcode
     */
    ryn4::RelayResult<DeviceInfo> readDeviceInfo();

    /**
     * @brief Verify hardware configuration matches software expectations
     *
     * Reads hardware configuration from DIP switches/jumpers and compares
     * to the slave ID and baud rate used to create this RYN4 instance.
     *
     * @return RelayResult<bool> with true if configuration matches, error if mismatch
     *
     * @code
     * auto result = ryn4.verifyHardwareConfig();
     * if (result.isError()) {
     *     Serial.println("WARNING: Hardware DIP switches don't match software config!");
     * }
     * @endcode
     */
    ryn4::RelayResult<bool> verifyHardwareConfig();

    /**
     * @brief Read all relay states as a bitmap
     *
     * Reads register 0x0080 which contains all 8 relay states as bits.
     * More efficient than reading individual relay registers (2 bytes vs 16 bytes).
     *
     * @param updateCache If true, updates internal relay state cache and event bits
     * @return RelayResult<uint16_t> with bitmap (bit 0 = relay 1, bit 7 = relay 8)
     *
     * @code
     * auto bitmap = ryn4.readBitmapStatus();
     * if (bitmap.isOk()) {
     *     for (int i = 0; i < 8; i++) {
     *         bool relayOn = (bitmap.value() >> i) & 0x01;
     *         Serial.printf("Relay %d: %s\n", i+1, relayOn ? "ON" : "OFF");
     *     }
     * }
     * // Or with cache update for verification:
     * auto bitmap = ryn4.readBitmapStatus(true);
     * @endcode
     */
    ryn4::RelayResult<uint16_t> readBitmapStatus(bool updateCache = false);

    /**
     * @brief Perform software factory reset
     *
     * Sends factory reset command to register 0x00FB (broadcast address).
     * Resets software-configurable parameters to defaults:
     * - Reply delay → 0ms
     * - Parity → None (8N1)
     *
     * Does NOT reset hardware-configured parameters (DIP switches):
     * - Slave ID (configured via A0-A5)
     * - Baud rate (configured via M1/M2)
     *
     * @return RelayResult<void> SUCCESS if reset command sent
     *
     * @note Alternative: Short RES jumper on board for 5 seconds, then power cycle
     *
     * @code
     * auto result = ryn4.factoryReset();
     * if (result.isOk()) {
     *     Serial.println("Factory reset successful - power cycle module");
     * }
     * @endcode
     */
    ryn4::RelayResult<void> factoryReset();

    /**
     * @brief Set parity configuration
     *
     * Writes to register 0x00FF to configure parity.
     * **IMPORTANT:** Requires power cycle to take effect!
     *
     * @param parity Parity setting (0=None, 1=Even, 2=Odd)
     * @return RelayResult<void> SUCCESS if write successful
     *
     * @note Values >2 will reset to 0 (None) on next power-up
     * @note This changes the Modbus framing - ensure your code matches after reboot
     *
     * @code
     * // Set to Even parity
     * auto result = ryn4.setParity(1);
     * if (result.isOk()) {
     *     Serial.println("Parity set to Even - power cycle module to activate");
     * }
     * @endcode
     */
    ryn4::RelayResult<void> setParity(uint8_t parity);

    /**
     * @brief Get current reply delay setting
     *
     * Reads register 0x00FC and converts to milliseconds.
     * Reply delay adds wait time before module responds to commands.
     *
     * @return RelayResult<uint16_t> with delay in milliseconds (0-1000ms)
     *
     * @code
     * auto delay = ryn4.getReplyDelay();
     * if (delay.isOk()) {
     *     Serial.printf("Current reply delay: %dms\n", delay.value);
     * }
     * @endcode
     */
    ryn4::RelayResult<uint16_t> getReplyDelay();

    /**
     * @brief Set reply delay
     *
     * Writes to register 0x00FC to configure response delay.
     * Delay is in 40ms units, range 0-1000ms (register value 0-25).
     *
     * @param delayMs Delay in milliseconds (0-1000, will be rounded to 40ms increments)
     * @return RelayResult<void> SUCCESS if write successful
     *
     * @note Setting value >1000ms resets to 0ms on next power-up
     * @note Useful for RS485 bus timing optimization
     *
     * @code
     * // Set 200ms reply delay
     * auto result = ryn4.setReplyDelay(200);
     * if (result.isOk()) {
     *     Serial.println("Reply delay set to 200ms");
     * }
     * @endcode
     */
    ryn4::RelayResult<void> setReplyDelay(uint16_t delayMs);
    
    // Public methods needed by main.cpp and tasks

    /**
     * @brief Bind relay state pointers (unified mapping API)
     *
     * This method accepts an array of pointers to bool variables in the application.
     * When relay states change, the library will update these variables directly.
     * This replaces the old BaseRelayMapping.additionalData mechanism.
     *
     * @param statePointers Array of 8 pointers to bool (one per relay)
     *
     * Example usage:
     * @code
     * std::array<bool*, 8> pointers = {
     *     &relayStates.heatingPump,
     *     &relayStates.waterPump,
     *     nullptr,  // unused relay
     *     ...
     * };
     * ryn4->bindRelayPointers(pointers);
     * @endcode
     */
    void bindRelayPointers(const std::array<bool*, 8>& statePointers);

    /**
     * @brief Set hardware configuration (unified mapping API)
     *
     * This method accepts a pointer to a constexpr hardware config array.
     * The config defines the event bits and physical relay numbers.
     *
     * @param config Pointer to array of 8 RelayHardwareConfig structs
     *
     * Example usage:
     * @code
     * ryn4->setHardwareConfig(ryn4::DEFAULT_HARDWARE_CONFIG.data());
     * @endcode
     */
    void setHardwareConfig(const base::RelayHardwareConfig* config);

    void printRelayStatus();
    void setDataReceiverTask(TaskHandle_t taskHandle);
    void setProcessingTask(TaskHandle_t taskHandle);
    EventGroupHandle_t getInitEventGroup() const { return xInitEventGroup; }
    const ModuleSettings& getModuleSettings() const { return moduleSettings; }
    static std::string baudRateToString(BaudRate rate);
    static std::string parityToString(Parity parity);
    
    // IDeviceInstance interface implementations
    IDeviceInstance::DeviceResult<void> requestData() override;
    IDeviceInstance::DeviceResult<void> processData() override;
    IDeviceInstance::DeviceResult<void> performAction(int actionId, int relayIndex) override;
    void waitForInitialization() override;
    EventGroupHandle_t getEventGroup() const noexcept override { return xUpdateEventGroup; }
    EventGroupHandle_t getErrorEventGroup() const;
    
    // Callback methods - not used by RYN4 (uses event-driven architecture) but required by interface
    virtual IDeviceInstance::DeviceResult<void> registerCallback(IDeviceInstance::EventCallback callback) override {
        (void)callback;
        return IDeviceInstance::DeviceResult<void>(IDeviceInstance::DeviceError::UNKNOWN_ERROR);
    }

    virtual IDeviceInstance::DeviceResult<void> unregisterCallbacks() override {
        return IDeviceInstance::DeviceResult<void>(IDeviceInstance::DeviceError::UNKNOWN_ERROR);
    }

    virtual IDeviceInstance::DeviceResult<void> setEventNotification(IDeviceInstance::EventType eventType, bool enable) override {
        (void)eventType;
        (void)enable;
        return IDeviceInstance::DeviceResult<void>(IDeviceInstance::DeviceError::UNKNOWN_ERROR);
    }

protected:
    // QueuedModbusDevice interface - handle async responses
    void onAsyncResponse(uint8_t functionCode, uint16_t address,
                        const uint8_t* data, size_t length) override;
    
    // ModbusDevice overrides
    void handleModbusResponse(uint8_t functionCode, uint16_t address, 
                            const uint8_t* data, size_t length) override;
    void handleModbusError(modbus::ModbusError error) override;

    // Mutex access
    SemaphoreHandle_t getMutexInstance() const noexcept override { return instanceMutex; }
    
    // Helper to convert internal RelayResult to DeviceResult
    static IDeviceInstance::DeviceError relayErrorToDeviceError(ryn4::RelayErrorCode relayError) {
        switch (relayError) {
            case ryn4::RelayErrorCode::SUCCESS: return IDeviceInstance::DeviceError::SUCCESS;
            case ryn4::RelayErrorCode::INVALID_INDEX: return IDeviceInstance::DeviceError::INVALID_PARAMETER;
            case ryn4::RelayErrorCode::MODBUS_ERROR: return IDeviceInstance::DeviceError::COMMUNICATION_ERROR;
            case ryn4::RelayErrorCode::TIMEOUT: return IDeviceInstance::DeviceError::TIMEOUT;
            case ryn4::RelayErrorCode::MUTEX_ERROR: return IDeviceInstance::DeviceError::MUTEX_ERROR;
            case ryn4::RelayErrorCode::NOT_INITIALIZED: return IDeviceInstance::DeviceError::NOT_INITIALIZED;
            case ryn4::RelayErrorCode::UNKNOWN_ERROR:
            default: return IDeviceInstance::DeviceError::UNKNOWN_ERROR;
        }
    }

    // Event-driven task notification support
    
    // Relay configuration
    /**
     * @brief Convert integer action ID to RelayAction enum
     * @param actionId Integer action identifier
     * @return RelayActionInfo containing action and potential error code
     */
    RYN4::RelayActionInfo intToRelayAction(int actionId);

    // State processing
    void processRelayState(uint8_t relayIndex, bool state);

    void invalidateCache();    

    // Callback registration and management
    // Callback system removed - using QueuedModbusDevice packet processing instead

    bool initializeModuleSettings();

    // Device configuration methods
    bool setFactoryReset();
    bool setDelayTime(uint8_t delayTimeValue);
    bool setAddress(uint8_t addressValue);
    bool setBaudRate(uint8_t baudRateValue);
    // setParity already declared above at line 418

    // Event bit handling methods (no longer overrides)
    void setUpdateEventBits(uint32_t bitsToSet);
    void clearUpdateEventBits(uint32_t bitsToClear);
    void setErrorEventBits(uint32_t bitsToSet);
    void clearErrorEventBits(uint32_t bitsToClear);
    void updateSensorEventBits(uint8_t sensorIndex, bool isValid, bool hasError);

    // Utility methods
    const char* getTag() const;
    void setTag(const char* newTag);
    // Module settings are now logged in one line during initialization

    // Clear all relay update bits
    void clearAllUpdateBits() {
        xEventGroupClearBits(xUpdateEventGroup, relayAllUpdateBits);
    }
    
    // Clear all relay error bits
    void clearAllErrorBits() {
        xEventGroupClearBits(xErrorEventGroup, relayAllErrorBits);
    }
    
    // Check if any relay has an update pending
    bool hasAnyUpdatePending() const {
        EventBits_t bits = xEventGroupGetBits(xUpdateEventGroup);
        return (bits & relayAllUpdateBits) != 0;
    }
    
    // Check if any relay has an error
    bool hasAnyError() const {
        EventBits_t bits = xEventGroupGetBits(xErrorEventGroup);
        return (bits & relayAllErrorBits) != 0;
    }
    
    // Get update bits for specific relays (by index mask)
    EventBits_t getUpdateBitsForRelays(uint8_t relayMask) const {
        EventBits_t bits = 0;
        for (int i = 0; i < 8; i++) {
            if (relayMask & (1 << i)) {
                bits |= ryn4::RELAY_UPDATE_BITS[i];
            }
        }
        return bits;
    }

    // Test functions (can be removed in production)
    void testVerifiedRelayControl();
    void testRelay8StatusFix();
    void testRelayStateGetters();

    static constexpr TickType_t mutexTimeout = pdMS_TO_TICKS(1000);

    // ========== Advanced Features Integration ==========
    
    /**
     * @brief Enable state machine for relay control
     * @param enable Enable/disable state machines
     * @return true if state machines initialized successfully
     */
    bool enableStateMachines(bool enable = true);
    

private:
    uint8_t _slaveID;
    const char* tag; // Tag for logging
    uint8_t _queueDepth; // Queue depth for async operations
    
    // Consolidated status flags using bit fields (saves memory)
    struct {
        uint8_t initialized : 1;
        uint8_t moduleOffline : 1;
        uint8_t customMappingsAvailable : 1;
        uint8_t reserved : 5;
    } statusFlags;
    
    // Initialization configuration
    InitConfig initConfig;

    static constexpr int NUM_RELAYS = 8; // Number of relays supported
    Relay relays[NUM_RELAYS];        // State of relays

    // Unified mapping architecture
    const base::RelayHardwareConfig* hardwareConfig; // Pointer to constexpr hardware config (flash)
    std::array<bool*, 8> statePointers;              // Runtime state pointers (RAM)

    std::set<uint8_t> pendingRelayChanges; // Track relays with pending state changes

    static IDeviceInstance::DeviceResult<std::vector<float>> cachedRelayResult;
    static TickType_t cacheTimestamp;
    static constexpr TickType_t CACHE_VALIDITY = pdMS_TO_TICKS(100);

    // Event-driven notification support
    TaskHandle_t dataReceiverTask = nullptr;  // Task to notify when data is ready (RelayStatusTask)
    TaskHandle_t processingTask = nullptr;    // Task to notify when packets need processing (RYN4ProcessingTask)
    
    /**
     * @brief Notify the data receiver task if set
     * 
     * This method sends a direct notification to the registered data receiver
     * task, allowing for efficient event-driven operation without polling.
     */
    void notifyDataReceiver();

    // Helper methods
    bool validatePacketLength(size_t receivedLength, size_t expectedLength, const char* context);

    // State access methods
    RelayMode getRelayMode(uint8_t relayIndex) const;
    bool wasLastCommandSuccessful(uint8_t relayIndex) const;
    TickType_t getLastUpdateTime(uint8_t relayIndex) const;
    bool isRelayStateConfirmed(uint8_t relayIndex) const;

    // Private Modbus response handlers
    void handleReadResponse(uint16_t startAddress, const uint8_t* data, size_t length);
    void handleRelayStatusResponse(uint16_t startAddress, const uint8_t* data, size_t length);
    void handleWriteSingleResponse(uint16_t address, const uint8_t* data, size_t length);
    void handleWriteMultipleResponse(uint16_t startAddress, const uint8_t* data, size_t length);
    void handleConfigResponse(uint16_t address, const uint8_t* data, size_t length);

    ModuleSettings moduleSettings;

    // Event handling
    EventGroupHandle_t xInitEventGroup;
    SemaphoreHandle_t initMutex;
    EventGroupHandle_t xUpdateEventGroup;
    EventGroupHandle_t xErrorEventGroup;
    
    // IDeviceInstance mutexes
    SemaphoreHandle_t instanceMutex;
    SemaphoreHandle_t interfaceMutex;

    struct InitBits {
        static constexpr uint32_t DEVICE_RESPONSIVE = (1 << 0);
        static constexpr uint32_t RELAY_CONFIG = (1 << 1);
        static constexpr uint32_t ALL_BITS = DEVICE_RESPONSIVE | RELAY_CONFIG;
    };
    
    // Track last response time for improved responsiveness detection
    TickType_t lastResponseTime = 0;
    static constexpr TickType_t RESPONSIVE_TIMEOUT = pdMS_TO_TICKS(30000); // 30 seconds

    // Initialization helpers
    bool waitForInitStep(EventBits_t stepBit, const char* stepName, TickType_t timeout = pdMS_TO_TICKS(5000));
    void setInitializationBit(EventBits_t bit);
    bool checkAllInitBitsSet() const;
};

#endif  // RYN4_RELAY_H
