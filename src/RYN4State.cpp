/**
 * @file RYN4State.cpp
 * @brief State management and query implementation for RYN4 library
 * 
 * This file contains methods for querying relay states, module status,
 * and cache management.
 */

#include "RYN4.h"
#include <MutexGuard.h>

using namespace ryn4;

// Static member initialization
IDeviceInstance::DeviceResult<std::vector<float>> RYN4::cachedRelayResult(IDeviceInstance::DeviceError::NOT_INITIALIZED);
TickType_t RYN4::cacheTimestamp = 0;

bool RYN4::isInitialized() const noexcept {
    return statusFlags.initialized;
}

bool RYN4::isModuleResponsive() {
    // First check passive responsiveness - if we've received any response recently
    TickType_t currentTime = xTaskGetTickCount();
    if (lastResponseTime != 0 && (currentTime - lastResponseTime) < RESPONSIVE_TIMEOUT) {
        // Module is responsive based on recent activity - no need to log every time
        return true;
    }
    
    // If no recent passive response, do active check
    RYN4_LOG_D("No recent response, performing active responsiveness check");
    
    // Attempt to read the module's return delay setting (simple register read)
    unsigned long checkStart = millis();
    auto result = readHoldingRegisters(0x00FC, 1);  // Return delay register
    RYN4_TRACK_MODBUS_RESULT(result);
    RYN4_LOG_I("[TIMING] isModuleResponsive() register read took: %lu ms", millis() - checkStart);

    if (result.isOk() && !result.value().empty()) {
        RYN4_LOG_D("Module responsive - return delay: %d units", result.value()[0]);
        // Update last response time for passive monitoring
        lastResponseTime = xTaskGetTickCount();
        return true;
    } else {
        RYN4_LOG_W("Module not responsive - failed to read return delay register");
        return false;
    }
}

// State query methods
ryn4::RelayResult<bool> RYN4::getRelayState(uint8_t relayIndex) const {
    if (relayIndex < 1 || relayIndex > NUM_RELAYS) {
        RYN4_LOG_E("Invalid relay index: %d (valid range: 1-%d)", relayIndex, NUM_RELAYS);
        return ryn4::RelayResult<bool>::error(ryn4::RelayErrorCode::INVALID_INDEX);
    }

    MutexGuard lock(instanceMutex, mutexTimeout);
    if (!lock) {
        RYN4_LOG_E("Failed to acquire mutex in getRelayState");
        return ryn4::RelayResult<bool>::error(ryn4::RelayErrorCode::MUTEX_ERROR);
    }

    bool state = relays[relayIndex - 1].isOn();
    return ryn4::RelayResult<bool>::ok(state);
}

RelayMode RYN4::getRelayMode(uint8_t relayIndex) const {
    if (relayIndex < 1 || relayIndex > NUM_RELAYS) {
        return RelayMode::NORMAL;
    }
    MutexGuard lock(instanceMutex, mutexTimeout);
    if (!lock) {
        RYN4_LOG_E("Failed to acquire mutex in getRelayMode");
        return RelayMode::NORMAL;
    }
    return relays[relayIndex - 1].getMode();
}

bool RYN4::wasLastCommandSuccessful(uint8_t relayIndex) const {
    if (relayIndex < 1 || relayIndex > NUM_RELAYS) {
        return false;
    }
    MutexGuard lock(instanceMutex, mutexTimeout);
    if (!lock) {
        RYN4_LOG_E("Failed to acquire mutex in wasLastCommandSuccessful");
        return false;
    }
    bool success = relays[relayIndex - 1].lastCommandSuccess();
    RYN4_LOG_D("Relay %d last command success: %s", relayIndex, success ? "YES" : "NO");
    return success;
}

TickType_t RYN4::getLastUpdateTime(uint8_t relayIndex) const {
    if (relayIndex < 1 || relayIndex > NUM_RELAYS) {
        return 0;
    }
    MutexGuard lock(instanceMutex, mutexTimeout);
    if (!lock) {
        RYN4_LOG_E("Failed to acquire mutex in getLastUpdateTime");
        return 0;
    }
    return relays[relayIndex - 1].lastUpdateTime;
}

bool RYN4::isRelayStateConfirmed(uint8_t relayIndex) const {
    if (relayIndex < 1 || relayIndex > NUM_RELAYS) {
        return false;
    }
    MutexGuard lock(instanceMutex, mutexTimeout);
    if (!lock) {
        RYN4_LOG_E("Failed to acquire mutex in isRelayStateConfirmed");
        return false;
    }
    bool confirmed = relays[relayIndex - 1].isStateConfirmed();
    RYN4_LOG_D("Relay %d state confirmed: %s", relayIndex, confirmed ? "YES" : "NO");
    return confirmed;
}

ryn4::RelayResult<std::array<bool, 8>> RYN4::getAllRelayStates() const {
    MutexGuard lock(instanceMutex, mutexTimeout);
    if (!lock) {
        RYN4_LOG_E("Failed to acquire mutex in getAllRelayStates");
        return ryn4::RelayResult<std::array<bool, 8>>::error(ryn4::RelayErrorCode::MUTEX_ERROR);
    }

    std::array<bool, 8> states;

    for (int i = 0; i < NUM_RELAYS; i++) {
        states[i] = relays[i].isOn();
    }

    return ryn4::RelayResult<std::array<bool, 8>>::ok(states);
}

void RYN4::printRelayStatus() {
    MutexGuard lock(instanceMutex, mutexTimeout);
    if (!lock) {
        RYN4_LOG_E("Failed to acquire mutex in printRelayStatus");
        return;
    }
    
    RYN4_LOG_I("=== Current Relay Status ===");
    for (int i = 0; i < NUM_RELAYS; i++) {
        const char* statusStr = relays[i].isOn() ? "ON" : "OFF";
        RYN4_LOG_I("Relay %d: %s (confirmed: %s)", 
                   i + 1, 
                   statusStr, 
                   relays[i].isStateConfirmed() ? "YES" : "NO");
    }
    
    // Count active relays
    int activeCount = 0;
    for (int i = 0; i < NUM_RELAYS; i++) {
        if (relays[i].isOn()) activeCount++;
    }
    RYN4_LOG_I("Active relays: %d/%d", activeCount, NUM_RELAYS);
}

void RYN4::invalidateCache() {
    MutexGuard lock(instanceMutex, mutexTimeout);
    if (lock) {
        cacheTimestamp = 0;  // Force cache to be stale
    }
}