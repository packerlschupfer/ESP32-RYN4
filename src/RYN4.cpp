#include "RYN4.h"
#include <MutexGuard.h>  // External dependency
#include <RetryPolicy.h>
#include "map"
#include <unordered_map>
#include <ModbusDevice.h>  // For base class methods
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"

using namespace ryn4;

// Define allUpdateBits and allErrorBits
EventBits_t relayAllUpdateBits =    static_cast<EventBits_t>(RelayUpdateBits::RELAY1_UPDATE_BIT) |
                                    static_cast<EventBits_t>(RelayUpdateBits::RELAY2_UPDATE_BIT) |
                                    static_cast<EventBits_t>(RelayUpdateBits::RELAY3_UPDATE_BIT) |
                                    static_cast<EventBits_t>(RelayUpdateBits::RELAY4_UPDATE_BIT) |
                                    static_cast<EventBits_t>(RelayUpdateBits::RELAY5_UPDATE_BIT) |
                                    static_cast<EventBits_t>(RelayUpdateBits::RELAY6_UPDATE_BIT) |
                                    static_cast<EventBits_t>(RelayUpdateBits::RELAY7_UPDATE_BIT) |
                                    static_cast<EventBits_t>(RelayUpdateBits::RELAY8_UPDATE_BIT);

EventBits_t relayAllErrorBits =     static_cast<EventBits_t>(RelayErrorBits::RELAY1_ERROR_BIT) |
                                    static_cast<EventBits_t>(RelayErrorBits::RELAY2_ERROR_BIT) |
                                    static_cast<EventBits_t>(RelayErrorBits::RELAY3_ERROR_BIT) |
                                    static_cast<EventBits_t>(RelayErrorBits::RELAY4_ERROR_BIT) |
                                    static_cast<EventBits_t>(RelayErrorBits::RELAY5_ERROR_BIT) |
                                    static_cast<EventBits_t>(RelayErrorBits::RELAY6_ERROR_BIT) |
                                    static_cast<EventBits_t>(RelayErrorBits::RELAY7_ERROR_BIT) |
                                    static_cast<EventBits_t>(RelayErrorBits::RELAY8_ERROR_BIT);


RYN4::RYN4(uint8_t slaveID, const char* tag, uint8_t queueDepth)
    : modbus::QueuedModbusDevice(slaveID), _slaveID(slaveID), tag(tag), _queueDepth(queueDepth),
      hardwareConfig(nullptr), statePointers({}) {

    statusFlags.initialized = false; // Initialize the flag
    statusFlags.moduleOffline = false;
    statusFlags.customMappingsAvailable = false;

    // Initialize state pointers to nullptr
    for (auto& ptr : statePointers) {
        ptr = nullptr;
    }

    // Create FreeRTOS resources with error checking
    xUpdateEventGroup = xEventGroupCreate();
    xErrorEventGroup = xEventGroupCreate();
    xInitEventGroup = xEventGroupCreate();
    initMutex = xSemaphoreCreateMutex();
    instanceMutex = xSemaphoreCreateMutex();
    interfaceMutex = xSemaphoreCreateMutex();

    if (!xUpdateEventGroup || !xErrorEventGroup || !xInitEventGroup || !initMutex || 
        !instanceMutex || !interfaceMutex) {
        RYN4_LOG_E("Failed to create event groups or mutexes");
        // Clean up any successfully created resources
        if (xUpdateEventGroup) vEventGroupDelete(xUpdateEventGroup);
        if (xErrorEventGroup) vEventGroupDelete(xErrorEventGroup);
        if (xInitEventGroup) vEventGroupDelete(xInitEventGroup);
        if (initMutex) vSemaphoreDelete(initMutex);
        if (instanceMutex) vSemaphoreDelete(instanceMutex);
        if (interfaceMutex) vSemaphoreDelete(interfaceMutex);
        
        xUpdateEventGroup = nullptr;
        xErrorEventGroup = nullptr;
        xInitEventGroup = nullptr;
        initMutex = nullptr;
        instanceMutex = nullptr;
        interfaceMutex = nullptr;
        return;
    }

    // Clear all bits in event groups using the defined masks
    xEventGroupClearBits(xUpdateEventGroup, relayAllUpdateBits);
    xEventGroupClearBits(xErrorEventGroup, relayAllErrorBits);
    xEventGroupClearBits(xInitEventGroup, InitBits::ALL_BITS);

    // Initialize relay states
    for (int i = 0; i < NUM_RELAYS; ++i) {
        relays[i].setOn(false);
        relays[i].setLastCommandSuccess(true);  // Initialize to true
        relays[i].lastUpdateTime = 0;
        relays[i].setStateConfirmed(false);
    }

    // Callback registration removed - RYN4 uses QueuedModbusDevice's packet processing instead
}

RYN4::~RYN4() {
    // Unregister from global device map using new architecture
    unregisterDevice();
    
    // Clean up all FreeRTOS resources
    if (xUpdateEventGroup != nullptr) {
        vEventGroupDelete(xUpdateEventGroup);
        xUpdateEventGroup = nullptr;
    }

    if (xErrorEventGroup != nullptr) {
        vEventGroupDelete(xErrorEventGroup);
        xErrorEventGroup = nullptr;
    }
    
    if (xInitEventGroup != nullptr) {
        vEventGroupDelete(xInitEventGroup);
        xInitEventGroup = nullptr;
    }
    
    if (initMutex != nullptr) {
        vSemaphoreDelete(initMutex);
        initMutex = nullptr;
    }
    
    // Clean up IDeviceInstance mutexes
    if (instanceMutex != nullptr) {
        vSemaphoreDelete(instanceMutex);
        instanceMutex = nullptr;
    }
    
    if (interfaceMutex != nullptr) {
        vSemaphoreDelete(interfaceMutex);
        interfaceMutex = nullptr;
    }
    
    // Clear any cached data
    cacheTimestamp = 0;

    RYN4_LOG_D("RYN4 destructor completed for slave ID: %d", _slaveID);
}

bool RYN4::setDelayTime(u_int8_t delayTimeValue) {
    uint16_t dataToWrite = static_cast<uint16_t>(delayTimeValue);
    uint16_t _startingAddress = 0x00FC;

    // Use base class writeSingleRegister which handles mutex internally
    auto result = writeSingleRegister(_startingAddress, dataToWrite);
    return result.isOk();
}

bool RYN4::setAddress(u_int8_t addressValue) {
    uint16_t dataToWrite = static_cast<uint16_t>(addressValue);
    
    // Use base class writeSingleRegister which handles mutex internally
    auto result = writeSingleRegister(0x00FD, dataToWrite);
    return result.isOk();
}

bool RYN4::setBaudRate(u_int8_t baudRateValue) {
    uint16_t dataToWrite = static_cast<uint16_t>(baudRateValue);
    
    // Use base class writeSingleRegister which handles mutex internally
    auto result = writeSingleRegister(0x00FE, dataToWrite);
    return result.isOk();
}

// setParity moved to RYN4AdvancedConfig.cpp (new RelayResult-based API)
// Removed callback registration methods - using QueuedModbusDevice packet processing

// Removed unused method invokeModbusResponseCallback

EventGroupHandle_t RYN4::getUpdateEventGroup() const {
    if (!xUpdateEventGroup) {
        RYN4_LOG_E("xUpdateEventGroup is NULL.");
        return nullptr;
    }
    return xUpdateEventGroup;
}

bool RYN4::waitForInitStep(EventBits_t stepBit, const char* stepName, TickType_t timeout) {
    EventBits_t bits = xEventGroupWaitBits(
        xInitEventGroup,
        stepBit,
        pdFALSE,    // Don't clear on exit
        pdTRUE,     // Wait for all bits
        timeout
    );

    bool success = (bits & stepBit) == stepBit;
    if (!success) {
        RYN4_LOG_E("Timeout waiting for initialization step: %s", stepName);
    } else {
        RYN4_LOG_D("Initialization step completed: %s", stepName);
    }
    return success;
}

ryn4::RelayResult<void> RYN4::waitForModuleInitComplete(TickType_t timeout) {
    if (!xInitEventGroup) {
        RYN4_LOG_E("Initialization event group not created");
        return ryn4::RelayResult<void>::error(ryn4::RelayErrorCode::NOT_INITIALIZED);
    }

    // First immediate check
    EventBits_t bits = xEventGroupGetBits(xInitEventGroup);
    if ((bits & InitBits::ALL_BITS) == InitBits::ALL_BITS) {
        RYN4_LOG_I("Initialization completed successfully (immediate check)");
        return ryn4::RelayResult<void>::ok();
    }

    // If not complete, wait for the event with timeout
    bits = xEventGroupWaitBits(
        xInitEventGroup,
        InitBits::ALL_BITS,
        pdFALSE,    // Don't clear bits
        pdTRUE,     // Wait for all bits
        timeout
    );

    if ((bits & InitBits::ALL_BITS) == InitBits::ALL_BITS) {
        RYN4_LOG_I("Initialization completed successfully");
        return ryn4::RelayResult<void>::ok();
    }

    // Timeout occurred - only log periodically to avoid spam
    static TickType_t lastErrorLog = 0;
    TickType_t now = xTaskGetTickCount();

    if (now - lastErrorLog > pdMS_TO_TICKS(10000)) {  // Log every 10 seconds max
        EventBits_t missingBits __attribute__((unused)) = InitBits::ALL_BITS & ~bits;
        #ifdef RYN4_DEBUG
            RYN4_LOG_E("Initialization incomplete. Missing bits: 0x%lx", missingBits);
        #else
            RYN4_LOG_W("RYN4 initialization pending...");
        #endif
        lastErrorLog = now;
    }

    return ryn4::RelayResult<void>::error(ryn4::RelayErrorCode::TIMEOUT);
}
bool RYN4::checkAllInitBitsSet() const {
    if (!xInitEventGroup) {
        RYN4_LOG_E("Init event group is null");
        return false;
    }
    
    EventBits_t bits = xEventGroupGetBits(xInitEventGroup);
    bool allSet = (bits & InitBits::ALL_BITS) == InitBits::ALL_BITS;
    
    if (!allSet) {
        EventBits_t missingBits = InitBits::ALL_BITS & ~bits;
        #ifdef RYN4_DEBUG
            RYN4_LOG_E("Missing initialization bits: 0x%lx", missingBits);
            
            // Log specific missing components
            if (!(bits & InitBits::RELAY_CONFIG))
                RYN4_LOG_E("Missing: Relay Configuration");
        #else
            (void)missingBits;  // Suppress warning in release mode
        #endif
    } else {
        RYN4_LOG_D("All initialization bits are set");
    }
    
    return allSet;
}
const char* RYN4::getTag() const {
    return tag;
}

void RYN4::setTag(const char* newTag) {
    if (newTag) {
        tag = newTag;
    }
}
// Test function to demonstrate verified operations
void RYN4::testVerifiedRelayControl() {
    RYN4_LOG_I("=== Testing Verified Relay Control ===");
    
    // Test 1: Single relay verified control
    RYN4_LOG_I("Test 1: Single relay verified control");
    
    RelayErrorCode result = setRelayStateVerified(1, true);
    RYN4_LOG_I("Set relay 1 ON (verified): %s", 
                     result == RelayErrorCode::SUCCESS ? "SUCCESS" : "FAILED");
    
    vTaskDelay(pdMS_TO_TICKS(500));
    
    result = setRelayStateVerified(1, false);
    RYN4_LOG_I("Set relay 1 OFF (verified): %s", 
                     result == RelayErrorCode::SUCCESS ? "SUCCESS" : "FAILED");
    
    // Test 2: Multi-relay verified control
    RYN4_LOG_I("\nTest 2: Multi-relay verified control");
    
    std::array<bool, 8> pattern = {true, false, true, false, true, false, true, true};
    result = setMultipleRelayStatesVerified(pattern);
    RYN4_LOG_I("Set pattern [ON,OFF,ON,OFF,ON,OFF,ON,ON] (verified): %s", 
                     result == RelayErrorCode::SUCCESS ? "SUCCESS" : "FAILED");
    
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Test 3: All relays OFF verified
    RYN4_LOG_I("\nTest 3: All relays OFF verified");
    
    result = setAllRelaysVerified(false);
    RYN4_LOG_I("Set all relays OFF (verified): %s", 
                     result == RelayErrorCode::SUCCESS ? "SUCCESS" : "FAILED");
    
    // Check confirmation status
    if (xSemaphoreTake(instanceMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        RYN4_LOG_I("\nRelay confirmation status:");
        for (int i = 0; i < NUM_RELAYS; i++) {
            RYN4_LOG_I("  Relay %d: %s, confirmed: %s", 
                             i + 1,
                             relays[i].isOn() ? "ON" : "OFF",
                             relays[i].isStateConfirmed() ? "YES" : "NO");
        }
        xSemaphoreGive(instanceMutex);
    }
    
    RYN4_LOG_I("=== Verified Control Test Complete ===");
}

// Test function to demonstrate relay state getter methods
void RYN4::testRelayStateGetters() {
    RYN4_LOG_I("=== Testing Relay State Getter Methods ===");
    
    // Test 1: Get individual relay states
    RYN4_LOG_I("Test 1: Individual relay state getters");
    
    for (uint8_t i = 1; i <= NUM_RELAYS; i++) {
        auto result = getRelayState(i);
        if (result.isOk()) {
            RYN4_LOG_I("  Relay %d state: %s", i, result.value() ? "ON" : "OFF");
        } else {
            RYN4_LOG_E("  Failed to get relay %d state", i);
        }
    }
    
    // Test 2: Get all relay states at once
    RYN4_LOG_I("\nTest 2: Get all relay states");
    
    auto allStates = getAllRelayStates();
    if (allStates.isOk()) {
        RYN4_LOG_I("  Successfully retrieved all relay states:");
        for (size_t i = 0; i < NUM_RELAYS; i++) {
            RYN4_LOG_I("    Relay %d: %s", i + 1, allStates.value()[i] ? "ON" : "OFF");
        }
    } else {
        RYN4_LOG_E("  Failed to get all relay states");
    }
    
    // Test 3: Error handling - invalid index
    RYN4_LOG_I("\nTest 3: Error handling");
    
    auto invalidResult = getRelayState(0);  // Invalid index (0)
    RYN4_LOG_I("  Get state for relay 0: %s", 
               invalidResult.isOk() ? "UNEXPECTED SUCCESS" : "FAILED (expected)");
    
    invalidResult = getRelayState(9);  // Invalid index (9)
    RYN4_LOG_I("  Get state for relay 9: %s", 
               invalidResult.isOk() ? "UNEXPECTED SUCCESS" : "FAILED (expected)");
    
    RYN4_LOG_I("=== Relay State Getter Test Complete ===");
}
