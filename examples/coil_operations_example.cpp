/**
 * @file coil_operations_example.cpp
 * @brief Example of using new coil operations in RYN4
 * 
 * This example shows how RYN4 could use the new coil operations
 * for digital I/O control and monitoring.
 */

#include "RYN4.h"

/**
 * Example 1: Using coils for auxiliary digital outputs
 * Some relay modules have additional digital outputs that can be
 * controlled via coils separately from the main relays.
 */
ryn4::RelayErrorCode RYN4::setAuxiliaryOutput(uint8_t outputIndex, bool state) {
    if (outputIndex >= 4) {  // Assuming 4 auxiliary outputs
        return RelayErrorCode::INVALID_INDEX;
    }
    
    // Auxiliary outputs might start at coil address 0x0010
    uint16_t coilAddress = 0x0010 + outputIndex;
    
    RYN4_LOG_D("Setting auxiliary output %d to %s", 
                      outputIndex + 1, state ? "ON" : "OFF");
    
    ModbusResult<void> result = writeCoilSync(coilAddress, state);
    
    if (result.isOk()) {
        RYN4_LOG_I("Auxiliary output %d set successfully", outputIndex + 1);
        return RelayErrorCode::SUCCESS;
    } else {
        RYN4_LOG_E("Failed to set auxiliary output %d: %s", 
                          outputIndex + 1, 
                          modbusErrorToString(result.error()));
        return RelayErrorCode::MODBUS_ERROR;
    }
}

/**
 * Example 2: Reading digital input states
 * Some relay modules include digital inputs for monitoring
 * external signals.
 */
ryn4::RelayResult<bool> RYN4::readDigitalInput(uint8_t inputIndex) {
    if (inputIndex >= 8) {  // Assuming 8 digital inputs
        return RelayResult<bool>(RelayErrorCode::INVALID_INDEX, false);
    }
    
    // Digital inputs might start at coil address 0x0000
    uint16_t coilAddress = 0x0000 + inputIndex;
    
    ModbusResult<bool> result = readCoilSync(coilAddress);
    
    if (result.isOk()) {
        RYN4_LOG_D("Digital input %d is %s", 
                          inputIndex + 1, 
                          result.value() ? "HIGH" : "LOW");
        return RelayResult<bool>(RelayErrorCode::SUCCESS, result.value());
    } else {
        RYN4_LOG_E("Failed to read digital input %d: %s", 
                          inputIndex + 1, 
                          modbusErrorToString(result.error()));
        return RelayResult<bool>(RelayErrorCode::MODBUS_ERROR, false);
    }
}

/**
 * Example 3: Bulk control of auxiliary outputs
 * Set multiple auxiliary outputs in one operation for efficiency.
 */
ryn4::RelayErrorCode RYN4::setAllAuxiliaryOutputs(uint8_t pattern) {
    // Convert byte pattern to bool array
    bool outputs[4];
    for (int i = 0; i < 4; i++) {
        outputs[i] = (pattern & (1 << i)) != 0;
    }
    
    RYN4_LOG_D("Setting auxiliary outputs to pattern: 0x%02X", pattern);
    
    // Write all 4 auxiliary outputs starting at address 0x0010
    ModbusResult<void> result = writeMultipleCoilsSync(0x0010, outputs, 4);
    
    if (result.isOk()) {
        RYN4_LOG_I("All auxiliary outputs set successfully");
        // Update internal state tracking if needed
        for (int i = 0; i < 4; i++) {
            auxOutputStates[i] = outputs[i];
        }
        return RelayErrorCode::SUCCESS;
    } else {
        RYN4_LOG_E("Failed to set auxiliary outputs: %s", 
                          modbusErrorToString(result.error()));
        return RelayErrorCode::MODBUS_ERROR;
    }
}

/**
 * Example 4: Emergency stop using coils
 * Some relay modules might have an emergency stop coil that
 * immediately opens all relays.
 */
ryn4::RelayErrorCode RYN4::activateEmergencyStop() {
    RYN4_LOG_W("EMERGENCY STOP ACTIVATED!");
    
    // Emergency stop might be at a special coil address
    const uint16_t EMERGENCY_STOP_COIL = 0x00FF;
    
    ModbusResult<void> result = writeCoilSync(EMERGENCY_STOP_COIL, true);
    
    if (result.isOk()) {
        // Update internal state - all relays are now OFF
        for (int i = 0; i < NUM_RELAYS; i++) {
            relays[i].isOn = false;
            relays[i].isStateConfirmed = true;
        }
        
        // Set all relay update bits to notify tasks
        setUpdateEventBits(relayAllUpdateBits);
        
        RYN4_LOG_E("Emergency stop executed - all relays OFF");
        return RelayErrorCode::SUCCESS;
    } else {
        RYN4_LOG_E("FAILED to activate emergency stop: %s", 
                          modbusErrorToString(result.error()));
        return RelayErrorCode::MODBUS_ERROR;
    }
}

/**
 * Example 5: Reading fault/alarm states
 * Monitor fault conditions that might be exposed as coils.
 */
ryn4::RelayResult<uint8_t> RYN4::readFaultStates() {
    uint8_t faultMask = 0;
    
    RYN4_LOG_D("Reading fault states...");
    
    // Read 8 fault coils starting at address 0x0020
    for (uint8_t i = 0; i < 8; i++) {
        ModbusResult<bool> result = readCoilSync(0x0020 + i);
        if (result.isOk() && result.value()) {
            faultMask |= (1 << i);
            RYN4_LOG_W("Fault detected on channel %d", i + 1);
        }
    }
    
    if (faultMask != 0) {
        // Set error event bits for faults
        setErrorEventBits(DEVICE_FAULT_BIT);
        return RelayResult<uint8_t>(RelayErrorCode::SUCCESS, faultMask);
    }
    
    return RelayResult<uint8_t>(RelayErrorCode::SUCCESS, 0);
}

/**
 * Example 6: Configuration enable/disable via coils
 * Some devices use coils to enable/disable configuration mode.
 */
ryn4::RelayErrorCode RYN4::setConfigurationMode(bool enable) {
    const uint16_t CONFIG_MODE_COIL = 0x00FE;
    
    RYN4_LOG_I("%s configuration mode", enable ? "Entering" : "Exiting");
    
    ModbusResult<void> result = writeCoilSync(CONFIG_MODE_COIL, enable);
    
    if (result.isOk()) {
        configModeActive = enable;
        if (enable) {
            RYN4_LOG_W("Device in configuration mode - relay control may be limited");
        }
        return RelayErrorCode::SUCCESS;
    } else {
        RYN4_LOG_E("Failed to %s configuration mode: %s", 
                          enable ? "enter" : "exit",
                          modbusErrorToString(result.error()));
        return RelayErrorCode::MODBUS_ERROR;
    }
}

/**
 * Helper function to convert ModbusError to string for logging
 */
const char* RYN4::modbusErrorToString(ModbusError error) {
    switch (error) {
        case ModbusError::SUCCESS:             return "Success";
        case ModbusError::TIMEOUT:             return "Timeout";
        case ModbusError::COMMUNICATION_ERROR: return "Communication error";
        case ModbusError::INVALID_RESPONSE:    return "Invalid response";
        case ModbusError::INVALID_PARAMETER:   return "Invalid parameter";
        case ModbusError::CRC_ERROR:          return "CRC error";
        case ModbusError::NOT_INITIALIZED:    return "Not initialized";
        case ModbusError::MUTEX_ERROR:        return "Mutex error";
        default:                              return "Unknown error";
    }
}