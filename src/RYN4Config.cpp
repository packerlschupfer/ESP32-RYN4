/**
 * @file RYN4Config.cpp
 * @brief Configuration and settings implementation for RYN4 library
 * 
 * This file contains methods for reading and managing module configuration
 * including RS485 settings, relay mappings, and module initialization.
 */

#include "RYN4.h"
#include "ryn4/HardwareRegisters.h"
#include <MutexGuard.h>

using namespace ryn4;

bool RYN4::initializeModuleSettings() {
    // Starting module initialization
    unsigned long startTime = millis();
    unsigned long stepTime = startTime;
    RYN4_LOG_I("[TIMING] initializeModuleSettings() started");
    
    // Take the init mutex to ensure thread safety
    if (!initMutex) {
        RYN4_LOG_E("Init mutex is NULL");
        return false;
    }
    
    if (xSemaphoreTake(initMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        RYN4_LOG_E("Failed to acquire init mutex");
        return false;
    }
    
    RYN4_LOG_D("Module initialization starting (unified mapping architecture)");
    
    // Check if module is responsive before proceeding
    RYN4_LOG_I("[TIMING] Testing module responsiveness...");
    
    // Note: Watchdog feeding should be handled by the calling task,
    // not by the library itself
    
    if (!isModuleResponsive()) {
        RYN4_LOG_E("Module is unresponsive. Initialization aborted.");
        RYN4_LOG_I("[TIMING] Module not responsive after %lu ms", millis() - stepTime);
        statusFlags.moduleOffline = true;  // Mark module as offline
        xSemaphoreGive(initMutex);
        return false;
    }
    
    RYN4_LOG_D("Module is responsive, proceeding with initialization");
    RYN4_LOG_I("[TIMING] Module responsive check took: %lu ms", millis() - stepTime);
    stepTime = millis();
    
    // Feed watchdog before starting multiple register reads
    // Removed excessive taskYIELD() here - will yield after multiple operations instead
    
    // Read RS485 Address (DIP switch configured)
    RYN4_LOG_I("[TIMING] Reading module configuration registers...");
    auto addrResult = readHoldingRegisters(0x00FD, 1);
    RYN4_TRACK_MODBUS_RESULT(addrResult);
    if (addrResult.isOk() && !addrResult.value().empty()) {
        moduleSettings.rs485Address = static_cast<uint8_t>(addrResult.value()[0] & 0xFF);
        
        // Verify it matches our expected address
        if (moduleSettings.rs485Address != _slaveID) {
            RYN4_LOG_W("Module address mismatch! Expected: 0x%02X, Actual: 0x%02X",
                            _slaveID, moduleSettings.rs485Address);
            // Continue anyway - the module is responding to our commands
        }
    } else {
        RYN4_LOG_E("Failed to read RS485 address register");
        xSemaphoreGive(initMutex);
        return false;
    }
    
    // Read Baud Rate (DIP switch configured)
    auto baudResult = readHoldingRegisters(0x00FE, 1);
    RYN4_TRACK_MODBUS_RESULT(baudResult);
    if (baudResult.isOk() && !baudResult.value().empty()) {
        moduleSettings.baudRate = static_cast<uint8_t>(baudResult.value()[0] & 0xFF);
    } else {
        RYN4_LOG_E("Failed to read baud rate register");
        xSemaphoreGive(initMutex);
        return false;
    }
    
    // Read Parity
    auto parityResult = readHoldingRegisters(0x00FF, 1);
    RYN4_TRACK_MODBUS_RESULT(parityResult);
    if (parityResult.isOk() && !parityResult.value().empty()) {
        moduleSettings.parity = static_cast<uint8_t>(parityResult.value()[0] & 0xFF);
    } else {
        RYN4_LOG_E("Failed to read parity register");
        xSemaphoreGive(initMutex);
        return false;
    }
    
    // Read Return Delay
    auto delayResult = readHoldingRegisters(0x00FC, 1);
    RYN4_TRACK_MODBUS_RESULT(delayResult);
    // Yield once after all configuration reads
    taskYIELD();
    RYN4_LOG_I("[TIMING] Configuration registers read took: %lu ms", millis() - stepTime);
    stepTime = millis();
    
    if (delayResult.isOk() && !delayResult.value().empty()) {
        moduleSettings.returnDelay = static_cast<uint8_t>(delayResult.value()[0] & 0xFF);
    } else {
        RYN4_LOG_E("Failed to read return delay register");
        xSemaphoreGive(initMutex);
        return false;
    }
    
    // Log all module settings in one line
    RYN4_DEBUG_ONLY(
        const char* baudStr = "?";
        switch(moduleSettings.baudRate) {
            case 0: baudStr = "9600"; break;
            case 1: baudStr = "19200"; break;
            case 2: baudStr = "38400"; break;
            case 3: baudStr = "115200"; break;
        }
        const char* parityStr = "?";
        switch(moduleSettings.parity) {
            case 0: parityStr = "None"; break;
            case 1: parityStr = "Even"; break;
            case 2: parityStr = "Odd"; break;
        }
        RYN4_LOG_D("Module config: Addr=0x%02X, Baud=%s, Parity=%s, Delay=%dms",
                   moduleSettings.rs485Address, baudStr, parityStr, moduleSettings.returnDelay * 40);
    );
    
    // Reset all relays to OFF if configured (default behavior)
    if (initConfig.resetRelaysOnInit) {
        RYN4_LOG_D("Resetting all relays to OFF for safe startup...");
        unsigned long resetStart = millis();

        // Use DELAY 0 (0x0600) to all relays - this cancels any active DELAY timers!
        // NOTE: ALL_OFF (0x0800) does NOT work if DELAY timers are active from previous run
        std::vector<uint16_t> delayZeroData(NUM_RELAYS, hardware::CMD_DELAY_BASE);  // 0x0600 × 8
        auto resetResult = writeMultipleRegisters(0, delayZeroData);
        RYN4_TRACK_MODBUS_RESULT(resetResult);
        if (resetResult.isOk()) {
            RYN4_LOG_D("All relays reset to OFF (DELAY 0 × 8) successfully");

            // Set all relay states to OFF in our internal tracking
            for (int i = 0; i < NUM_RELAYS; i++) {
                relays[i].setOn(false);
                relays[i].setStateConfirmed(true);
                relays[i].lastUpdateTime = xTaskGetTickCount();
            }

            // Clear all relay status bits
            xEventGroupClearBits(xUpdateEventGroup, 0x00FFFFFF);  // Clear all 24 bits

            // Small delay for relay switching - reduced from 100ms to 20ms
            vTaskDelay(pdMS_TO_TICKS(20));
            RYN4_LOG_I("[TIMING] Relay reset took: %lu ms", millis() - resetStart);
        } else {
            RYN4_LOG_E("Failed to reset relays to OFF - continuing anyway");
        }
    } else {
        RYN4_LOG_D("Preserving existing relay states (resetRelaysOnInit = false)");
    }
    
    // Read all relay states to establish baseline (unless skipped)
    if (!initConfig.skipRelayStateRead) {
        RYN4_LOG_D("Reading initial relay states...");
        stepTime = millis();
        
        // Read all 8 relay states in one batch operation for efficiency
        auto result = readHoldingRegisters(0x0000, NUM_RELAYS);
        RYN4_TRACK_MODBUS_RESULT(result);
        RYN4_LOG_I("[TIMING] Batch relay read took: %lu ms", millis() - stepTime);

    if (result.isOk() && result.value().size() == NUM_RELAYS) {
        // Process all relay states at once
        char stateStr[NUM_RELAYS * 4 + 3]; // "[OFF,OFF,OFF,OFF,OFF,OFF,OFF,OFF]"
        int pos = snprintf(stateStr, sizeof(stateStr), "[");

        for (int i = 0; i < NUM_RELAYS; i++) {
            // Hardware returns 0x0001 for ON, 0x0000 for OFF (different from command values!)
            bool state = (result.value()[i] == 0x0001);
            relays[i].setOn(state);
            relays[i].setStateConfirmed(true);
            relays[i].lastUpdateTime = xTaskGetTickCount();
            
            if (state) {
                // Set the status bit for ON relays
                uint32_t statusBit = RELAY_STATUS_BITS[i];
                xEventGroupSetBits(xUpdateEventGroup, statusBit);
            }
            
            // Build state string
            if (i > 0) pos += snprintf(stateStr + pos, sizeof(stateStr) - pos, ",");
            pos += snprintf(stateStr + pos, sizeof(stateStr) - pos, "%s", state ? "ON" : "OFF");
        }
        
        snprintf(stateStr + pos, sizeof(stateStr) - pos, "]");
        RYN4_LOG_D("Initial relay states: %s", stateStr);
    } else {
        RYN4_LOG_E("Failed to read relay states in batch, falling back to individual reads");
        // Fallback to individual reads if batch read fails
        char stateStr[NUM_RELAYS * 4 + 3];
        int pos = snprintf(stateStr, sizeof(stateStr), "[");
        int successCount = 0;
        
        for (int i = 0; i < NUM_RELAYS; i++) {
            auto singleResult = readHoldingRegisters(i, 1);
            RYN4_TRACK_MODBUS_RESULT(singleResult);
            if (singleResult.isOk() && !singleResult.value().empty()) {
                bool state = (singleResult.value()[0] == 0x0001);
                relays[i].setOn(state);
                relays[i].setStateConfirmed(true);
                relays[i].lastUpdateTime = xTaskGetTickCount();
                
                if (state) {
                    uint32_t statusBit = RELAY_STATUS_BITS[i];
                    xEventGroupSetBits(xUpdateEventGroup, statusBit);
                }
                
                if (i > 0) pos += snprintf(stateStr + pos, sizeof(stateStr) - pos, ",");
                pos += snprintf(stateStr + pos, sizeof(stateStr) - pos, "%s", state ? "ON" : "OFF");
                successCount++;
            } else {
                RYN4_LOG_E("Failed to read relay %d state", i + 1);
                if (i > 0) pos += snprintf(stateStr + pos, sizeof(stateStr) - pos, ",");
                pos += snprintf(stateStr + pos, sizeof(stateStr) - pos, "ERR");
            }
        }
        
        snprintf(stateStr + pos, sizeof(stateStr) - pos, "]");
        if (successCount > 0) {
            RYN4_LOG_D("Initial relay states (%d/%d read): %s", successCount, NUM_RELAYS, stateStr);
        }
    }
    } else {
        RYN4_LOG_I("[TIMING] Skipping relay state read (skipRelayStateRead = true)");
        // Mark all relays as unconfirmed - we haven't verified actual hardware state
        for (int i = 0; i < NUM_RELAYS; i++) {
            relays[i].setStateConfirmed(false);
        }
    }
    
    // Set initialization bit to indicate module settings are loaded
    setInitializationBit(InitBits::DEVICE_RESPONSIVE);
    
    // Module settings are now logged in one line above
    
    RYN4_LOG_INIT_STEP("Module settings initialized successfully");
    RYN4_LOG_I("[TIMING] Total initializeModuleSettings() time: %lu ms", millis() - startTime);
    
    // Verify all relay states were read (only warn if we actually tried to read)
    bool allRelaysRead = true;
    for (int i = 0; i < NUM_RELAYS; i++) {
        if (!relays[i].isStateConfirmed()) {
            allRelaysRead = false;
            // Only warn if we tried to read but failed, not if we intentionally skipped
            if (!initConfig.skipRelayStateRead) {
                RYN4_LOG_W("Relay %d state not confirmed during init", i + 1);
            }
        }
    }

    if (allRelaysRead) {
        setInitializationBit(InitBits::RELAY_CONFIG);
        RYN4_LOG_INIT_STEP("All relay states verified successfully");
    } else if (initConfig.skipRelayStateRead) {
        RYN4_LOG_D("Relay states unconfirmed (read intentionally skipped)");
    }
    
    // Complete initialization - set all bits to indicate success
    statusFlags.initialized = true;
    xEventGroupSetBits(xInitEventGroup, InitBits::ALL_BITS);
    RYN4_LOG_INIT_COMPLETE();
    
    xSemaphoreGive(initMutex);
    return true;
}

// Configuration request methods
bool RYN4::reqReturnDelay() {
    // Request the Modbus register at address 0x00FC to respond with the return delay.
    return sendRequest(0x03, 0x00FC, 1) == ESP_OK;
}

bool RYN4::reqBaudRate() {
    return sendRequest(0x03, 0x00FE, 1) == ESP_OK;
}

bool RYN4::reqParity() {
    return sendRequest(0x03, 0x00FF, 1) == ESP_OK;
}

// Configuration value converters
BaudRate RYN4::getBaudRateEnum(uint8_t rawValue) {
    switch (rawValue) {
        case 0: return BaudRate::BAUD_9600;
        case 1: return BaudRate::BAUD_19200;
        case 2: return BaudRate::BAUD_38400;
        case 3: return BaudRate::BAUD_115200;
        default: return BaudRate::BAUD_9600;
    }
}

Parity RYN4::getParityEnum(uint8_t rawValue) {
    switch (rawValue) {
        case 0: return Parity::NONE;
        case 1: return Parity::EVEN;
        case 2: return Parity::ODD;
        default: return Parity::NONE;
    }
}

Parity RYN4::getStoredParity() {
    return getParityEnum(moduleSettings.parity);
}

// String conversion utilities
std::string RYN4::baudRateToString(BaudRate rate) {
    switch (rate) {
        case BaudRate::BAUD_9600: return "9600";
        case BaudRate::BAUD_19200: return "19200";
        case BaudRate::BAUD_38400: return "38400";
        case BaudRate::BAUD_115200: return "115200";
        default: return "Unknown";
    }
}

std::string RYN4::parityToString(Parity parity) {
    switch (parity) {
        case Parity::NONE: return "None";
        case Parity::EVEN: return "Even";
        case Parity::ODD: return "Odd";
        default: return "Unknown";
    }
}

// Function removed - module settings are now logged in one line during initialization

// ========== Unified Mapping API ==========

void RYN4::bindRelayPointers(const std::array<bool*, 8>& statePointers) {
    RYN4_LOG_D("Binding relay state pointers (unified mapping API)");

    // Copy the pointer array
    this->statePointers = statePointers;

    // Log which relays have bindings
    for (size_t i = 0; i < 8; i++) {
        if (statePointers[i] != nullptr) {
            RYN4_LOG_D("Relay %d bound to state pointer 0x%p", i + 1, statePointers[i]);
        } else {
            RYN4_LOG_D("Relay %d has no state binding (nullptr)", i + 1);
        }
    }
}

void RYN4::setHardwareConfig(const base::RelayHardwareConfig* config) {
    RYN4_LOG_D("Setting hardware configuration (unified mapping API)");

    if (config == nullptr) {
        RYN4_LOG_E("Hardware config pointer is null!");
        return;
    }

    // Store pointer to constexpr config array (lives in flash)
    this->hardwareConfig = config;

    RYN4_LOG_D("Hardware config set successfully (constexpr array in flash)");
}