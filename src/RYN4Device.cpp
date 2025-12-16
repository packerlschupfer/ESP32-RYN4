/**
 * @file RYN4Device.cpp
 * @brief IDeviceInstance interface implementation for RYN4 library
 * 
 * This file contains the device interface methods for integration with
 * the broader device ecosystem.
 */

#include "RYN4.h"
#include <MutexGuard.h>
#include <string.h>

using namespace ryn4;


// IDeviceInstance interface implementation

IDeviceInstance::DeviceResult<void> RYN4::initialize() {
    // Use default configuration (resetRelaysOnInit = true)
    return initialize(InitConfig{});
}

IDeviceInstance::DeviceResult<void> RYN4::initialize(const InitConfig& config) {
    if (statusFlags.initialized) {
        return IDeviceInstance::DeviceResult<void>();  // Default constructor for success
    }
    
    // Store the configuration
    initConfig = config;
    
    RYN4_LOG_I("Starting RYN4 initialization for slave ID 0x%02X (resetRelaysOnInit=%s)", 
               _slaveID, config.resetRelaysOnInit ? "true" : "false");
    
    // Register with global device map for Modbus response routing
    modbus::ModbusError regError = registerDevice();
    if (regError != modbus::ModbusError::SUCCESS) {
        RYN4_LOG_E("Failed to register device: %d", static_cast<int>(regError));
        return IDeviceInstance::DeviceResult<void>(IDeviceInstance::DeviceError::COMMUNICATION_ERROR);
    }
    
    // Set initialization phase to CONFIGURING
    setInitPhase(InitPhase::CONFIGURING);
    RYN4_LOG_D("Set init phase to CONFIGURING");
    
    // Initialize the relay module
    if (!initializeModuleSettings()) {
        // Module is offline or initialization failed
        RYN4_LOG_E("RYN4 initialization failed - module is offline or unresponsive");
        
        // Set error phase and unregister
        setInitPhase(InitPhase::ERROR);
        unregisterDevice();
        
        return IDeviceInstance::DeviceResult<void>(IDeviceInstance::DeviceError::COMMUNICATION_ERROR);
    }
    
    // Set ready phase
    setInitPhase(InitPhase::READY);
    RYN4_LOG_D("Set init phase to READY");
    
    // Enable queued mode for normal operation after initialization
    if (!enableAsync(_queueDepth)) {  // Use configurable queue depth
        RYN4_LOG_E("Failed to enable async mode with queue depth %d", _queueDepth);
        setInitPhase(InitPhase::ERROR);
        return IDeviceInstance::DeviceResult<void>(IDeviceInstance::DeviceError::UNKNOWN_ERROR);
    }
    RYN4_LOG_D("Async mode enabled with depth %d", _queueDepth);
    
    statusFlags.initialized = true;
    RYN4_LOG_I("RYN4 initialized successfully");
    return IDeviceInstance::DeviceResult<void>();  // Default constructor for success
}

IDeviceInstance::DeviceResult<void> RYN4::requestData() {
    // Check if module is offline - prevent polling when device is unavailable
    if (statusFlags.moduleOffline) {
        RYN4_LOG_D("Module is offline - skipping requestData");
        return IDeviceInstance::DeviceResult<void>(IDeviceInstance::DeviceError::COMMUNICATION_ERROR);
    }
    
    RelayErrorCode result = readAllRelayStatus();
    if (result != RelayErrorCode::SUCCESS) {
        // Handle the error accordingly
        RYN4_LOG_E("Failed to request all relay statuses.");
        return IDeviceInstance::DeviceResult<void>(relayErrorToDeviceError(result));
    }
    return IDeviceInstance::DeviceResult<void>();  // Default constructor for success
}

IDeviceInstance::DeviceResult<void> RYN4::processData() {
    // Check if module is offline - prevent processing when device is unavailable
    if (statusFlags.moduleOffline) {
        RYN4_LOG_D("Module is offline - skipping processData");
        return IDeviceInstance::DeviceResult<void>(IDeviceInstance::DeviceError::COMMUNICATION_ERROR);
    }
    
    // Base class handles queue processing if async mode is enabled
    if (isAsyncEnabled()) {
        processQueue();
        return IDeviceInstance::DeviceResult<void>();  // Default constructor for success
    }
    
    // This method is now primarily used by QueuedModbusDevice to process queued packets
    // The actual relay state logging has been moved to debug level to prevent flooding
    static TickType_t lastLogTime = 0;
    TickType_t currentTime = xTaskGetTickCount();
    
    // Only log relay states once every 5 seconds to prevent flooding
    if ((currentTime - lastLogTime) >= pdMS_TO_TICKS(5000)) {
        MutexGuard lock(instanceMutex, pdMS_TO_TICKS(100));
        if (lock) {
            // Build relay states string - all on one line
            char stateStr[128] = "Relay states: ";
            int offset = strlen(stateStr);
            
            for (int i = 0; i < NUM_RELAYS; ++i) {
                const Relay& relay = relays[i];
                offset += snprintf(stateStr + offset, sizeof(stateStr) - offset, 
                                  "[%d:%s] ", i + 1, relay.isOn() ? "ON" : "OFF");
            }
            
            RYN4_LOG_D("%s", stateStr);
            lastLogTime = currentTime;
        }
    }
    
    return IDeviceInstance::DeviceResult<void>();  // Default constructor for success
}

IDeviceInstance::DeviceResult<void> RYN4::waitForInitializationComplete(TickType_t timeout) {
    auto result = waitForModuleInitComplete(timeout);
    if (result.isOk()) {
        return IDeviceInstance::DeviceResult<void>();  // Default constructor for success
    }
    return IDeviceInstance::DeviceResult<void>(relayErrorToDeviceError(result.error()));
}

void RYN4::waitForInitialization() {
    // Legacy method - just wait with a default timeout
    waitForInitializationComplete(pdMS_TO_TICKS(5000));
}

IDeviceInstance::DeviceResult<void> RYN4::performAction(int actionId, int relayIndex) {
    // Performance tracking for action execution
    RYN4_TIME_START();
    
    RYN4_LOG_D("performAction called - Action ID: %d, Relay: %d", 
                      actionId, relayIndex);
    
    RelayActionInfo actionInfo = intToRelayAction(actionId);

    if (actionInfo.errorCode == RelayErrorCode::SUCCESS) {
        // Log the action being performed (visible in all modes for operational awareness)
        const char* actionName = [actionId]() {
            switch(actionId) {
                case 0: return "ON";
                case 1: return "OFF";
                case 2: return "TOGGLE";
                case 3: return "LATCH";
                case 4: return "MOMENTARY";
                case 5: return "DELAY";
                case 6: return "ALL_ON";
                case 7: return "ALL_OFF";
                default: return "UNKNOWN";
            }
        }();
        
        RYN4_LOG_I("Executing %s action on relay %d", actionName, relayIndex);
        
        RelayErrorCode result = controlRelay(relayIndex, actionInfo.action);
        
        if (result != RelayErrorCode::SUCCESS) {
            RYN4_LOG_E("Action %s failed for relay %d, error code: %d", 
                              actionName, relayIndex, static_cast<int>(result));
            RYN4_TIME_END("Action execution");
            return IDeviceInstance::DeviceResult<void>(relayErrorToDeviceError(result));
        }
    } else {
        // Handle error, e.g., logging or setting an error state
        RYN4_LOG_E("Invalid action ID: %d", actionId);
        RYN4_TIME_END("Action execution");
        return IDeviceInstance::DeviceResult<void>(IDeviceInstance::DeviceError::INVALID_PARAMETER);
    }
    
    RYN4_TIME_END("Action execution");
    return IDeviceInstance::DeviceResult<void>();  // Default constructor for success
}

IDeviceInstance::DeviceResult<std::vector<float>> RYN4::getData(IDeviceInstance::DeviceDataType dataType) {
    RYN4_TIME_START();

    // Debug mode: log with task info (but throttled)
    #if defined(RYN4_DEBUG_FULL)
        static uint32_t debugCallCount = 0;
        if (++debugCallCount % 100 == 0) {  // Only log every 100th call
            TaskHandle_t currentTask = xTaskGetCurrentTaskHandle();
            char taskName[16];
            vTaskGetName(currentTask, taskName, sizeof(taskName));
            RYN4_LOG_D("getData() called from task: %s, type: %d", 
                              taskName, static_cast<int>(dataType));
        }
    #endif

    IDeviceInstance::DeviceError error = IDeviceInstance::DeviceError::UNKNOWN_ERROR;
    std::vector<float> values;

    switch (dataType) {
        case DeviceDataType::TEMPERATURE:
            // Remove excessive logging
            RYN4_LOG_D("TEMPERATURE data type not applicable for relay controller");
            break;

        case DeviceDataType::RELAY_STATE: {
            // Remove frequent logging here - it's causing performance issues
            // RYN4_LOG_D("Fetching RELAY_STATE data...");

            // Critical section timing
            RYN4_TIME_START();

            // Lock the mutex if needed to safely access shared resources
            if (xSemaphoreTake(getMutexInstance(), pdMS_TO_TICKS(100))) {
                RYN4_TIME_END("Mutex acquisition");

                RYN4_LOG_CRITICAL_ENTRY("relay state access");

                // Pre-allocate vector for efficiency
                values.clear(); // Ensure the vector is clear before adding new values
                values.reserve(NUM_RELAYS); // Optimize vector allocation

                 // Use single loop without string building in release mode
                #if defined(RYN4_DEBUG_FULL)
                    static char stateBuffer[64];
                    int pos = 0;
                    pos += snprintf(stateBuffer, sizeof(stateBuffer), "States: ");
                #endif

                for (int i = 0; i < NUM_RELAYS; ++i) {
                    // Apply inverse logic if configured for this relay
                    bool logicalState = relays[i].isOn();
                    if (hardwareConfig != nullptr && hardwareConfig[i].inverseLogic) {
                        logicalState = !logicalState;  // Invert the state
                    }
                    values.push_back(logicalState ? 1.0f : 0.0f);
                    
                    #if defined(RYN4_DEBUG_FULL)
                        if (pos < sizeof(stateBuffer) - 10) {
                            pos += snprintf(stateBuffer + pos, sizeof(stateBuffer) - pos,
                                          "%d:%s ", i + 1, logicalState ? "ON" : "OFF");
                        }
                    #endif
                }

                error = DeviceError::SUCCESS; // Indicate success if relay states are successfully added
                
                // Only log errors or in full debug mode with throttling
                #if defined(RYN4_DEBUG_FULL)
                    // Only log states periodically, not every call
                    static uint32_t callCount = 0;
                    if (++callCount % 10 == 0) {
                        RYN4_LOG_D("Retrieved relay states - %s", stateBuffer);
                    }
                #endif

                RYN4_LOG_CRITICAL_EXIT("relay state access");
                xSemaphoreGive(getMutexInstance());

                // Remove success logging - it's too frequent
                // RYN4_LOG_D("Successfully retrieved %d relay states", NUM_RELAYS);

            } else {
                RYN4_LOG_E("Timeout acquiring mutex for relay state access");
                
                // In release mode, return cached values without mutex
                #ifdef RYN4_RELEASE_MODE
                    values.reserve(NUM_RELAYS);
                    for (int i = 0; i < NUM_RELAYS; ++i) {
                        // Apply inverse logic if configured for this relay
                        bool logicalState = relays[i].isOn();
                        if (hardwareConfig != nullptr && hardwareConfig[i].inverseLogic) {
                            logicalState = !logicalState;  // Invert the state
                        }
                        values.push_back(logicalState ? 1.0f : 0.0f);
                    }
                    error = DeviceError::SUCCESS;
                    RYN4_LOG_W("Using cached relay states due to mutex timeout");
                #endif
            }
            break;
        }

        default:
            RYN4_LOG_E("Unsupported data type requested: %d", static_cast<int>(dataType));
            error = IDeviceInstance::DeviceError::INVALID_PARAMETER;
            break;
    }

    RYN4_TIME_END("Total getData time");
    
    // Cache successful results
    if (error == IDeviceInstance::DeviceError::SUCCESS) {
        cachedRelayResult = IDeviceInstance::DeviceResult<std::vector<float>>::ok(values);
        cacheTimestamp = xTaskGetTickCount();
    }

    if (error == IDeviceInstance::DeviceError::SUCCESS) {
        return IDeviceInstance::DeviceResult<std::vector<float>>::ok(values);
    }
    return IDeviceInstance::DeviceResult<std::vector<float>>::error(error);
}

bool RYN4::waitForData() {
    EventBits_t updateBits = xEventGroupWaitBits(
        xUpdateEventGroup,           // The event group being tested for updates.
        relayAllUpdateBits,          // The bits within the event group to wait for updates.
        pdTRUE,                      // Clear allUpdateBits on exit.
        pdFALSE,                     // Wait for any bit, not all.
        pdMS_TO_TICKS(10000)         // Wait for a maximum of 10 seconds for data to become available.
    );

    EventBits_t errorBits = xEventGroupWaitBits(
        xErrorEventGroup,            // The event group being tested for errors.
        relayAllErrorBits,           // The bits within the event group to wait for errors.
        pdTRUE,                      // Clear allErrorBits on exit.
        pdFALSE,                     // Wait for any bit, not all.
        0                            // No delay, just check the current status.
    );

    if ((updateBits & relayAllUpdateBits) == 0 && (errorBits & relayAllErrorBits) == 0) {
        // No update or error bits were set - handle timeout or error
        RYN4_LOG_E("Timeout or no data/error update from relay controller.");
        return false;
    }

    return (updateBits & relayAllUpdateBits) != 0;
}