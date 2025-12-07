/**
 * @file RYN4Control.cpp
 * @brief Relay control implementation for RYN4 library
 * 
 * This file contains the relay control methods including individual
 * and batch relay operations, state verification, and control logic.
 */

#include "RYN4.h"
#include <MutexGuard.h>
#include <RetryPolicy.h>

using namespace ryn4;

ryn4::RelayErrorCode RYN4::controlRelay(uint8_t relayIndex, RelayAction action) {
    RYN4_TIME_START();
    
    RYN4_LOG_I("controlRelay(%d, %d) called - CONTROL COMMAND!", relayIndex, static_cast<int>(action));
    
    // Check if module is offline
    if (statusFlags.moduleOffline) {
        RYN4_LOG_E("Module is offline - cannot control relays");
        return RelayErrorCode::MODBUS_ERROR;
    }
    
    // Check if resources are initialized
    if (!xUpdateEventGroup || !xErrorEventGroup) {
        RYN4_LOG_E("Resources not initialized");
        return RelayErrorCode::UNKNOWN_ERROR;
    }
    
    RYN4_LOG_D("ControlRelay called for Relay %d with Action %d", 
                      relayIndex, static_cast<int>(action));
    
    // Validate the relay index (1-8 for individual relays)
    if (relayIndex < 1 || relayIndex > NUM_RELAYS) {
        RYN4_LOG_E("Invalid relay index: %d (valid range: 1-%d)", relayIndex, NUM_RELAYS);
        return RelayErrorCode::INVALID_INDEX;
    }

    // Use updated mapping for register address
    uint16_t registerAddress = relayIndex - 1; // Use updated mapping for register address
    auto& relay = relays[relayIndex - 1];  // Get reference to relay state
    uint16_t commandValue = 0;

    // Map actions to command values
    switch (action) {
        case RelayAction::OPEN: commandValue = 0x0100; break;
        case RelayAction::CLOSE: commandValue = 0x0200; break;
        case RelayAction::TOGGLE: commandValue = 0x0300; break;
        case RelayAction::LATCH: commandValue = 0x0400; break;
        case RelayAction::MOMENTARY: commandValue = 0x0500; break;
        case RelayAction::DELAY: 
            commandValue = 0x0600; // Delay requires additional parameters
            RYN4_LOG_E("Delay action requires a delay time parameter. Not implemented here.");
            relay.setLastCommandSuccess(false);
            return RelayErrorCode::UNKNOWN_ERROR;
        case RelayAction::OPEN_ALL: commandValue = 0x0700; break;
        case RelayAction::CLOSE_ALL: commandValue = 0x0800; break;
        default:
            relay.setLastCommandSuccess(false);
            return RelayErrorCode::UNKNOWN_ERROR;
    }

    RYN4_LOG_D("Sending command value:", commandValue);

    // Attempt to send the command via Modbus with retry
    
    // Create retry policy for Modbus operations
    RetryPolicy retryPolicy = RetryPolicies::modbusDefault();
    
    RYN4_LOG_D("Sending relay %d command: addr=0x%04X, value=0x%04X", 
                     relayIndex, registerAddress, commandValue);
    // RYN4_LOG_D("MODBUS TX: Write Single Register - Addr: %d, Value: 0x%04X",
    //                  registerAddress, commandValue);
    
    // // Log the expected Modbus frame
    // uint8_t expectedFrame[8];
    // expectedFrame[0] = _slaveID;
    // expectedFrame[1] = 0x06;  // Write Single Register
    // expectedFrame[2] = (registerAddress >> 8) & 0xFF;
    // expectedFrame[3] = registerAddress & 0xFF;
    // expectedFrame[4] = (commandValue >> 8) & 0xFF;
    // expectedFrame[5] = commandValue & 0xFF;
    // // CRC would be calculated by ModbusRTU
    // RYN4_LOG_D("Expected Modbus frame: %02X %02X %02X %02X %02X %02X + CRC",
    //                  expectedFrame[0], expectedFrame[1], expectedFrame[2],
    //                  expectedFrame[3], expectedFrame[4], expectedFrame[5]);
    
    // Execute with retry
    auto result = retryPolicy.execute<bool>([&]() {
        // writeSingleRegister handles mutex internally
        auto writeResult = writeSingleRegister(registerAddress, commandValue);
        bool isOk = writeResult.isOk();
        if (!isOk) {
            RYN4_LOG_E("writeSingleRegister failed for relay %d", relayIndex);
        }
        return isOk;
    });
    
    if (result.attemptsMade > 1) {
        RYN4_LOG_I("Relay command succeeded after %d attempts (total delay: %lu ms)", 
                         result.attemptsMade, result.totalDelayMs);
    }
    
    bool success = result.success;
    
    if (!success) {
        RYN4_LOG_E("Failed to send command to relay %d after %d attempts", 
                         relayIndex, result.attemptsMade);
    }

    // Process the result of the Modbus command
    if (success) {
        RYN4_LOG_D("Command sent successfully to Relay", relayIndex);

        // Invalidate cache immediately after command
        invalidateCache();        

        // Update relay state tracking
        relay.setLastCommandSuccess(true);
        relay.lastUpdateTime = xTaskGetTickCount();
        relay.setStateConfirmed(false);  // State needs confirmation
        
        // Track state changes for operational visibility
        bool previousState = relay.isOn();
        
        // Update expected state based on action
        // Hardware mapping: OPEN (0x0100) = ON, CLOSE (0x0200) = OFF
        bool expectedState = relay.isOn();  // Current state
        
        switch(action) {
            case RelayAction::OPEN:
                expectedState = true;   // OPEN command turns relay ON
                break;
            case RelayAction::CLOSE:
                expectedState = false;  // CLOSE command turns relay OFF
                break;
            case RelayAction::TOGGLE:
                expectedState = !relay.isOn();
                break;
            // Other actions don't change expected state immediately
            default:
                break;
        }
        
        // Check if we're already in the desired state BEFORE updating
        if (relay.isOn() == expectedState && (action == RelayAction::OPEN || action == RelayAction::CLOSE)) {
            RYN4_LOG_D("Relay %d already in requested state: %s", 
                       relayIndex, expectedState ? "ON" : "OFF");
        }
        
        // Now update the relay state
        relay.setOn(expectedState);
        
        // Log state changes (visible in release mode)
        if (previousState != relay.isOn()) {
            RYN4_LOG_RELAY_CHANGE(relayIndex, previousState, relay.isOn());
        }

        // Small delay to allow the relay to process the command
        vTaskDelay(pdMS_TO_TICKS(20)); // Reduced from 50ms to 20ms

        // Clear error bits and set success update event bits for the specific relay
        if (relayIndex >= 1 && relayIndex <= 8) {
            // Clear any previous error bit for this relay using the lookup table
            clearErrorEventBits(ryn4::RELAY_ERROR_BITS[relayIndex - 1]);
            // Set update bit to indicate successful operation
            setUpdateEventBits(ryn4::RELAY_UPDATE_BITS[relayIndex - 1]);
        }

        RYN4_TIME_END("Relay control");
        return RelayErrorCode::SUCCESS;
    } else {
        RYN4_LOG_E("Modbus command failed for Relay %d", relayIndex);
        
        // Update state tracking for failure
        relay.setLastCommandSuccess(false);
        relay.setStateConfirmed(false);

        // Set error event bits for the specific relay
        if (relayIndex >= 1 && relayIndex <= 8) {
            // Set error event bit using the lookup table
            setErrorEventBits(RELAY_ERROR_BITS[relayIndex - 1]);
        }

        return RelayErrorCode::MODBUS_ERROR;
    }
}

ryn4::RelayErrorCode RYN4::setMultipleRelayStates(const std::array<bool, 8>& states) {
    RYN4_TIME_START();

    RYN4_LOG_D("setMultipleRelayStates called with 8 relay states");

    // Check if module is offline
    if (statusFlags.moduleOffline) {
        RYN4_LOG_E("Module is offline - cannot set multiple relay states");
        return RelayErrorCode::MODBUS_ERROR;
    }

    // Use vector for writeMultipleRegisters API
    std::vector<uint16_t> data(NUM_RELAYS);  // 8 relays (2 bytes each)
    
    RYN4_DEBUG_ONLY(
        RYN4_LOG_D("Preparing relay states for multi-write:");
        for (size_t i = 0; i < NUM_RELAYS; i++) {
            // Hardware uses inverted logic: ON=OPEN(0x0100), OFF=CLOSE(0x0200)
            data[i] = states[i] ? 0x0100 : 0x0200;
            RYN4_LOG_D("  Relay %d -> %s (0x%04X)", 
                             i + 1, states[i] ? "ON (OPEN)" : "OFF (CLOSE)", data[i]);
        }
    );

    // In release mode, just fill the data array without logging
    RYN4_RELEASE_ONLY(
        for (size_t i = 0; i < NUM_RELAYS; i++) {
            // Hardware uses inverted logic: ON=OPEN(0x0100), OFF=CLOSE(0x0200)
            data[i] = states[i] ? 0x0100 : 0x0200;
        }
    );

    
    // Create retry policy for batch operations
    RetryPolicy retryPolicy = RetryPolicies::modbusDefault();
    
    // Execute with retry
    auto result = retryPolicy.execute<bool>([&]() {
        RYN4_LOG_D("Sending multi-register write command");
        // writeMultipleRegisters handles mutex internally
        auto writeResult = writeMultipleRegisters(
            0,          // Starting register address (relays start at register 0)
            data        // Data vector
        );
        if (!writeResult.isOk()) {
            RYN4_LOG_D("Write failed with error code: %d", static_cast<int>(writeResult.error()));
        }
        return writeResult.isOk();
    });
    
    if (result.attemptsMade > 1) {
        RYN4_LOG_I("Batch relay command succeeded after %d attempts (total delay: %lu ms)", 
                         result.attemptsMade, result.totalDelayMs);
    }
    
    bool success = result.success;
    
    if (success) {
            RYN4_LOG_D("Multi-register write command sent successfully");

#ifdef RYN4_ENABLE_OPTIMISTIC_UPDATES
            // OPTIMISTIC UPDATE: Update internal relay states immediately
            // This provides instant feedback to the UI while the hardware processes the command
            if (xSemaphoreTake(instanceMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                EventBits_t updateBits = 0;

                for (size_t i = 0; i < NUM_RELAYS; i++) {
                    bool previousState = relays[i].isOn();
                    bool newState = states[i];

                    // Update internal state
                    relays[i].setOn(newState);
                    relays[i].lastUpdateTime = xTaskGetTickCount();
                    relays[i].setStateConfirmed(false);  // Will be confirmed when hardware responds

                    // Log state changes
                    if (previousState != newState) {
                        RYN4_LOG_RELAY_CHANGE(i + 1, previousState, newState);

                        // Set update bit for this relay
                        updateBits |= RELAY_UPDATE_BITS[i];
                    }
                }

                xSemaphoreGive(instanceMutex);

                // Set update bits if any relays changed
                if (updateBits) {
                    setUpdateEventBits(updateBits);

                    // Notify data receiver of the changes
                    xEventGroupSetBits(xUpdateEventGroup, SENSOR_UPDATE_BIT);
                    notifyDataReceiver();

                    RYN4_LOG_I("Optimistic update: relay states updated immediately");
                }
            } else {
                RYN4_LOG_W("Failed to acquire mutex for optimistic update");
            }
#endif // RYN4_ENABLE_OPTIMISTIC_UPDATES

            // Invalidate cache after multiple relay changes
            invalidateCache();
    } else {
        RYN4_LOG_E("Failed to send multi-register write command");
    }
    
    RYN4_TIME_END("Multiple relay state setting");
    return success ? RelayErrorCode::SUCCESS : RelayErrorCode::MODBUS_ERROR;
}

// Single relay control with verification
ryn4::RelayErrorCode RYN4::controlRelayVerified(uint8_t relayIndex, RelayAction action) {
    RYN4_TIME_START();
    
    RYN4_LOG_D("controlRelayVerified called for Relay %d with Action %d", 
                      relayIndex, static_cast<int>(action));
    
    // First, send the command (optimistic update if RYN4_ENABLE_OPTIMISTIC_UPDATES defined)
    RelayErrorCode result = controlRelay(relayIndex, action);
    
    if (result != RelayErrorCode::SUCCESS) {
        RYN4_TIME_END("Relay control verified (failed to send)");
        return result;
    }
    
    // Only verify for actions that change state
    if (action != RelayAction::OPEN && action != RelayAction::CLOSE && 
        action != RelayAction::TOGGLE) {
        RYN4_LOG_D("Action %d does not require verification", static_cast<int>(action));
        RYN4_TIME_END("Relay control verified (no verify needed)");
        return RelayErrorCode::SUCCESS;
    }
    
    // Small delay for relay to physically change state
    vTaskDelay(pdMS_TO_TICKS(30)); // Reduced from 100ms to 30ms
    
    // Read back the specific relay status
    bool actualState;
    if (readRelayStatus(relayIndex, actualState) != RelayErrorCode::SUCCESS) {
        RYN4_LOG_E("Failed to read relay %d status for verification", relayIndex);
        
        if (xSemaphoreTake(instanceMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            relays[relayIndex - 1].setStateConfirmed(false);
            xSemaphoreGive(instanceMutex);
        }
        
        RYN4_TIME_END("Relay control verified (read failed)");
        return RelayErrorCode::MODBUS_ERROR;
    }
    
    // Wait for the read response to be processed
    EventBits_t bits = xEventGroupWaitBits(
        xUpdateEventGroup,
        RELAY_UPDATE_BITS[relayIndex - 1],
        pdTRUE,     // Clear the bit on exit
        pdTRUE,     // Wait for all bits
        pdMS_TO_TICKS(500)  // 500ms timeout
    );
    
    if ((bits & RELAY_UPDATE_BITS[relayIndex - 1]) == 0) {
        RYN4_LOG_W("Timeout waiting for relay %d status response", relayIndex);
        
        if (xSemaphoreTake(instanceMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            relays[relayIndex - 1].setStateConfirmed(false);
            xSemaphoreGive(instanceMutex);
        }
        
        RYN4_TIME_END("Relay control verified (timeout)");
        return RelayErrorCode::TIMEOUT;
    }
    
    // Verify the state matches what we commanded
    if (xSemaphoreTake(instanceMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        auto& relay = relays[relayIndex - 1];
        bool expectedState;
        
        switch(action) {
            case RelayAction::OPEN:
                expectedState = true;
                break;
            case RelayAction::CLOSE:
                expectedState = false;
                break;
            case RelayAction::TOGGLE:
                // For toggle, we can't predict the final state without knowing the initial state
                // So we just confirm the state was read
                relay.setStateConfirmed(true);
                xSemaphoreGive(instanceMutex);
                RYN4_LOG_D("Relay %d toggle verified, now: %s", 
                                 relayIndex, relay.isOn() ? "ON" : "OFF");
                RYN4_TIME_END("Relay control verified (toggle)");
                return RelayErrorCode::SUCCESS;
            default:
                expectedState = relay.isOn(); // Should not reach here
                break;
        }
        
        if (relay.isOn() == expectedState) {
            relay.setStateConfirmed(true);
            RYN4_LOG_D("Relay %d state verified: %s", 
                             relayIndex, relay.isOn() ? "ON" : "OFF");
            result = RelayErrorCode::SUCCESS;
        } else {
            relay.setStateConfirmed(false);
            RYN4_LOG_E("Relay %d verification failed: commanded %s, actual %s",
                             relayIndex, expectedState ? "ON" : "OFF", 
                             relay.isOn() ? "ON" : "OFF");
            
            // Set error bit for this relay
            setErrorEventBits(RELAY_ERROR_BITS[relayIndex - 1]);
            result = RelayErrorCode::UNKNOWN_ERROR;
        }
        
        xSemaphoreGive(instanceMutex);
    }
    
    RYN4_TIME_END("Relay control verified");
    return result;
}

// Multiple relay control with verification
ryn4::RelayErrorCode RYN4::setMultipleRelayStatesVerified(const std::array<bool, 8>& states) {
    RYN4_TIME_START();

    RYN4_LOG_D("setMultipleRelayStatesVerified called with 8 relay states");
    
    // First, send the command (optimistic update if RYN4_ENABLE_OPTIMISTIC_UPDATES defined)
    RelayErrorCode result = setMultipleRelayStates(states);
    
    if (result != RelayErrorCode::SUCCESS) {
        RYN4_TIME_END("Multi relay set verified (failed to send)");
        return result;
    }
    
    // Small delay for relays to physically change state
    vTaskDelay(pdMS_TO_TICKS(30)); // Reduced from 100ms to 30ms

    // Read back all relay states using bitmap (faster: 2 bytes vs 16 bytes)
    // updateCache=true updates internal state and sets RELAY_CONFIG bit
    auto bitmapResult = readBitmapStatus(true);

    if (bitmapResult.isError()) {
        RYN4_LOG_E("Failed to read relay status bitmap for verification");

        // Mark all states as unconfirmed
        if (xSemaphoreTake(instanceMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            for (size_t i = 0; i < NUM_RELAYS; i++) {
                relays[i].setStateConfirmed(false);
            }
            xSemaphoreGive(instanceMutex);
        }

        RYN4_TIME_END("Multi relay set verified (read failed)");
        return RelayErrorCode::MODBUS_ERROR;
    }

    uint16_t bitmap = bitmapResult.value();

    // Verify all states match what we commanded
    // Note: readBitmapStatus(true) already updated the cache, but we verify against bitmap directly
    bool allMatch = true;
    int mismatchCount = 0;
    EventBits_t errorBits = 0;

    RYN4_LOG_D("Verifying relay states (bitmap: 0x%04X):", bitmap);

    for (size_t i = 0; i < NUM_RELAYS; i++) {
        bool expectedState = states[i];
        bool actualState = (bitmap >> i) & 0x01;

        if (actualState == expectedState) {
            RYN4_LOG_D("  Relay %d: verified %s ✓",
                             i + 1, actualState ? "ON" : "OFF");
        } else {
            allMatch = false;
            mismatchCount++;
            errorBits |= RELAY_ERROR_BITS[i];

            // Mark this relay as unconfirmed
            if (xSemaphoreTake(instanceMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                relays[i].setStateConfirmed(false);
                xSemaphoreGive(instanceMutex);
            }

            RYN4_LOG_E("  Relay %d: verification FAILED - commanded %s, actual %s ✗",
                             i + 1, expectedState ? "ON" : "OFF",
                             actualState ? "ON" : "OFF");
        }
    }

    if (!allMatch) {
        // Set error bits for mismatched relays
        setErrorEventBits(errorBits);

        RYN4_LOG_E("Relay verification failed: %d of %d relays did not reach expected state",
                         mismatchCount, NUM_RELAYS);
        result = RelayErrorCode::UNKNOWN_ERROR;
    } else {
        RYN4_LOG_I("All %d relays verified successfully", NUM_RELAYS);
        result = RelayErrorCode::SUCCESS;
    }
    
    RYN4_TIME_END("Multi relay set verified");
    return result;
}

// Convenience function to set and verify a single relay state
ryn4::RelayErrorCode RYN4::setRelayStateVerified(uint8_t relayIndex, bool state) {
    RelayAction action = state ? RelayAction::OPEN : RelayAction::CLOSE;
    return controlRelayVerified(relayIndex, action);
}

// Convenience function to set and verify all relays to the same state
ryn4::RelayErrorCode RYN4::setAllRelaysVerified(bool state) {
    std::array<bool, 8> states;
    states.fill(state);
    return setMultipleRelayStatesVerified(states);
}

void RYN4::setDataReceiverTask(TaskHandle_t taskHandle) {
    dataReceiverTask = taskHandle;
    RYN4_LOG_I("Data receiver task set: %p", taskHandle);
}

void RYN4::setProcessingTask(TaskHandle_t taskHandle) {
    processingTask = taskHandle;
    RYN4_LOG_I("Processing task set: %p", taskHandle);
}

void RYN4::notifyDataReceiver() {
    // Notify the data receiver task (RelayStatusTask) if set
    if (dataReceiverTask != nullptr) {
        xTaskNotifyGive(dataReceiverTask);
        RYN4_LOG_D("Notified data receiver task");
    }
    
    // Notify the processing task (RYN4ProcessingTask) if set
    if (processingTask != nullptr) {
        xTaskNotifyGive(processingTask);
        RYN4_LOG_D("Notified processing task");
    }
}

RYN4::RelayActionInfo RYN4::intToRelayAction(int actionId) {
    RelayActionInfo result;

    if (actionId >= 0 && actionId < static_cast<int>(RelayAction::NUM_ACTIONS)) {
        result.action = static_cast<RelayAction>(actionId);
        result.errorCode = RelayErrorCode::SUCCESS;
        
        RYN4_LOG_D("Converted action ID %d to RelayAction::%d", 
                          actionId, static_cast<int>(result.action));
    } else {
        result.action = RelayAction(); // Default action or a specific invalid action
        result.errorCode = RelayErrorCode::UNKNOWN_ERROR;
        
        RYN4_LOG_W("Invalid action ID: %d (valid range: 0-%d)", 
                         actionId, static_cast<int>(RelayAction::NUM_ACTIONS) - 1);
    }

    return result;
}


// Unified mapping API - processes relay state using array index
void RYN4::processRelayState(uint8_t relayIndex, bool state) {
    // relayIndex is 1-based (physical relay number)
    if (relayIndex < 1 || relayIndex > NUM_RELAYS) {
        RYN4_LOG_E("Invalid relay index: %d", relayIndex);
        return;
    }

    size_t arrayIndex = relayIndex - 1;
    EventBits_t updateBit = 0;
    bool inverseLogic = false;

    // Get inverse logic from hardware config if available
    if (hardwareConfig != nullptr) {
        inverseLogic = hardwareConfig[arrayIndex].inverseLogic;
    }

    // Lock mutex before accessing relay array
    {
        MutexGuard lock(instanceMutex, pdMS_TO_TICKS(100));
        if (lock) {
            auto& relay = relays[arrayIndex];
            bool previousState = relay.isOn();

            // Update relay state and tracking info
            relay.setOn(state);
            relay.lastUpdateTime = xTaskGetTickCount();
            relay.setStateConfirmed(true);

            if (previousState != state) {
                RYN4_LOG_I("Relay %d state changed: %s -> %s%s",
                                relayIndex,
                                previousState ? "ON" : "OFF",
                                state ? "ON" : "OFF",
                                inverseLogic ? " (INVERSE)" : "");
            } else {
                RYN4_LOG_D("Relay %d state confirmed: %s",
                                 relayIndex,
                                 state ? "ON" : "OFF");
            }

            // Update the device state pointer with inverse logic applied
            if (statePointers[arrayIndex] != nullptr) {
                bool* deviceState = statePointers[arrayIndex];
                // Apply inverse logic: if inverseLogic is true, device is ON when relay is OFF
                *deviceState = inverseLogic ? !state : state;

                RYN4_LOG_D("Relay %d: relay=%s, device=%s %s",
                                 relayIndex,
                                 state ? "ON" : "OFF",
                                 *deviceState ? "ON" : "OFF",
                                 inverseLogic ? "(inverted)" : "(normal)");
            }

            // Store update bit to set after mutex is released
            updateBit = RELAY_UPDATE_BITS[arrayIndex];
        } else {
            RYN4_LOG_E("Failed to acquire mutex for relay state update");
            return;
        }
    } // MutexGuard automatically releases mutex here

    // Set update bit after mutex is released
    if (updateBit != 0) {
        setUpdateEventBits(updateBit);
    }
}