/**
 * @file RYN4Modbus.cpp
 * @brief Modbus communication implementation for RYN4 library
 * 
 * This file contains the Modbus response handling, packet validation,
 * and asynchronous communication methods.
 * 
 * IMPORTANT: RYN4 hardware uses different values for commands vs status reads:
 * - Commands: 0x0100 = ON, 0x0200 = OFF
 * - Status reads: 0x0001 = ON, 0x0000 = OFF
 */

#include "RYN4.h"
#include <MutexGuard.h>

using namespace ryn4;

bool RYN4::validatePacketLength(size_t receivedLength, size_t expectedLength, const char* context) {
    if (receivedLength != expectedLength) {
        RYN4_LOG_W("%s: Invalid packet length. Expected %d, received %d", 
                   context, expectedLength, receivedLength);
        return false;
    }
    return true;
}

void RYN4::onAsyncResponse(uint8_t functionCode, uint16_t address, const uint8_t* data, size_t length) {
    // Update passive responsiveness tracking
    lastResponseTime = xTaskGetTickCount();
    
    RYN4_LOG_D("onAsyncResponse: FC=0x%02X, Addr=0x%04X, Len=%d", 
               functionCode, address, length);
    
    // Handle based on function code
    switch (functionCode) {
        case 0x03: // Read Holding Registers
        case 0x04: // Read Input Registers
            handleReadResponse(address, data, length);
            break;
            
        case 0x06: // Write Single Register
            handleWriteSingleResponse(address, data, length);
            break;
            
        case 0x10: // Write Multiple Registers
            handleWriteMultipleResponse(address, data, length);
            break;
            
        default:
            RYN4_LOG_W("Unhandled function code: 0x%02X", functionCode);
            break;
    }
}

void RYN4::handleModbusResponse(uint8_t functionCode, uint16_t address, 
                              const uint8_t* data, size_t length) {
    RYN4_LOG_D("handleModbusResponse called with FC: 0x%02X, Addr: 0x%04X, Length: %d", 
               functionCode, address, length);
    
    // Process the response based on function code
    // This method is called by the base class when a response is received
}

void RYN4::handleModbusError(modbus::ModbusError error) {
    RYN4_LOG_E("Modbus error occurred: %d", static_cast<int>(error));
    // Additional error handling can be added here if needed
}

// Private helper methods for specific response types
void RYN4::handleReadResponse(uint16_t startAddress, const uint8_t* data, size_t length) {
    RYN4_LOG_D("handleReadResponse: startAddr=0x%04X, length=%d", startAddress, length);
    
    // Handle relay status reads (addresses 0x0000-0x0007)
    if (startAddress >= 0x0000 && startAddress <= 0x0007) {
        handleRelayStatusResponse(startAddress, data, length);
    }
    // Handle configuration register reads (0x00FC-0x00FF)
    else if (startAddress >= 0x00FC && startAddress <= 0x00FF) {
        handleConfigResponse(startAddress, data, length);
    }
    else {
        RYN4_LOG_W("Read response for unhandled address range: 0x%04X", startAddress);
    }
}

void RYN4::handleRelayStatusResponse(uint16_t startAddress, const uint8_t* data, size_t length) {
    // Calculate how many relays are in this response
    int relayCount = length / 2;  // Each relay status is 2 bytes
    
    RYN4_LOG_D("Processing relay status for %d relays starting at address 0x%04X", 
               relayCount, startAddress);
    
    MutexGuard lock(instanceMutex, mutexTimeout);
    if (!lock) {
        RYN4_LOG_E("Failed to acquire mutex in handleRelayStatusResponse");
        return;
    }
    
    for (int i = 0; i < relayCount && (startAddress + i) < NUM_RELAYS; i++) {
        int relayIndex = startAddress + i;
        
        // Extract 16-bit value (big-endian)
        uint16_t relayValue = (data[i * 2] << 8) | data[i * 2 + 1];
        // Hardware returns 0x0001 for ON, 0x0000 for OFF (different from command values!)
        bool newState = (relayValue == 0x0001);  // 0x0001 = ON, 0x0000 = OFF
        
        auto& relay = relays[relayIndex];
        bool previousState = relay.isOn();
        
        // Update relay state
        relay.setOn(newState);
        relay.setStateConfirmed(true);
        relay.lastUpdateTime = xTaskGetTickCount();
        
        // Set appropriate event bits
        if (previousState != newState) {
            setUpdateEventBits(RELAY_UPDATE_BITS[relayIndex]);
            RYN4_LOG_I("Relay %d state changed: %s -> %s", 
                       relayIndex + 1, 
                       previousState ? "ON" : "OFF",
                       newState ? "ON" : "OFF");
        }
    }
}

void RYN4::handleWriteSingleResponse(uint16_t address, const uint8_t* data, size_t length) {
    RYN4_LOG_D("Write single register response: addr=0x%04X", address);
    
    // For relay control (addresses 0x0000-0x0007)
    if (address <= 0x0007) {
        int relayIndex = address;
        
        if (length >= 2) {
            uint16_t echoValue = (data[0] << 8) | data[1];
            RYN4_LOG_D("Relay %d command acknowledged with value 0x%04X", 
                       relayIndex + 1, echoValue);
            
            MutexGuard lock(instanceMutex, mutexTimeout);
            if (lock) {
                auto& relay = relays[relayIndex];
                
                // Determine expected state from command
                bool expectedState = relay.isOn();
                if (echoValue == 0x0100) expectedState = true;   // ON
                else if (echoValue == 0x0200) expectedState = false; // OFF
                else if (echoValue == 0x0300) expectedState = !relay.isOn(); // TOGGLE
                
                // Check if already in expected state
                if (relay.isOn() == expectedState) {
                    relay.setStateConfirmed(true);
                    RYN4_LOG_D("Relay %d confirmed in expected state: %s", 
                               relayIndex + 1, expectedState ? "ON" : "OFF");
                } else {
                    relay.setStateConfirmed(false);
                    RYN4_LOG_W("Relay %d state mismatch - expected: %s, actual: %s", 
                               relayIndex + 1,
                               expectedState ? "ON" : "OFF",
                               relay.isOn() ? "ON" : "OFF");
                }
            }
        }
    }
}

void RYN4::handleWriteMultipleResponse(uint16_t startAddress, const uint8_t* data, size_t length) {
    RYN4_LOG_D("Write multiple registers response: startAddr=0x%04X, count=%d", 
               startAddress, length);
    
    // For batch relay operations
    if (startAddress == 0x0000 && length <= NUM_RELAYS * 2) {
        RYN4_LOG_I("Batch relay operation acknowledged for %d relays", length / 2);
        
        // Mark affected relays as needing confirmation
        MutexGuard lock(instanceMutex, mutexTimeout);
        if (lock) {
            for (int i = 0; i < length / 2 && i < NUM_RELAYS; i++) {
                relays[i].setStateConfirmed(false);
            }
        }
    }
}

void RYN4::handleConfigResponse(uint16_t address, const uint8_t* data, size_t length) {
    if (length < 2) {
        RYN4_LOG_W("Config response too short: %d bytes", length);
        return;
    }
    
    uint16_t value = (data[0] << 8) | data[1];
    
    switch (address) {
        case 0x00FC: // Return delay
            moduleSettings.returnDelay = value & 0xFF;
            RYN4_LOG_D("Return delay updated: %d units", moduleSettings.returnDelay);
            break;
            
        case 0x00FD: // RS485 address
            moduleSettings.rs485Address = value & 0xFF;
            RYN4_LOG_D("RS485 address read: 0x%02X", moduleSettings.rs485Address);
            break;
            
        case 0x00FE: // Baud rate
            moduleSettings.baudRate = value & 0xFF;
            RYN4_LOG_D("Baud rate updated: %d", moduleSettings.baudRate);
            break;
            
        case 0x00FF: // Parity
            moduleSettings.parity = value & 0xFF;
            RYN4_LOG_D("Parity updated: %d", moduleSettings.parity);
            break;
            
        default:
            RYN4_LOG_W("Config response for unknown register: 0x%04X", address);
            break;
    }
}

// Status reading methods
ryn4::RelayErrorCode RYN4::readRelayStatus(uint8_t relayIndex, bool& state) {
    // Check if module is offline - prevent communication when device is unavailable
    if (statusFlags.moduleOffline) {
        RYN4_LOG_D("Module is offline - cannot read relay status");
        return RelayErrorCode::MODBUS_ERROR;
    }
    
    if (relayIndex < 1 || relayIndex > NUM_RELAYS) {
        return RelayErrorCode::INVALID_INDEX;
    }
    
    uint16_t registerAddr = relayIndex - 1;  // Relay registers are 0-indexed
    
    // Use STATUS priority for status reads - lowest priority to avoid blocking sensor reads
    auto result = readHoldingRegistersWithPriority(registerAddr, 1, esp32Modbus::STATUS);
    RYN4_TRACK_MODBUS_RESULT(result);
    if (result.isOk() && !result.value().empty()) {
        // Hardware returns 0x0001 for ON, 0x0000 for OFF (different from command values!)
        state = (result.value()[0] == 0x0001);  // 0x0001 = ON, 0x0000 = OFF
        
        // Update internal state
        MutexGuard lock(instanceMutex, mutexTimeout);
        if (lock) {
            relays[relayIndex - 1].setOn(state);
            relays[relayIndex - 1].setStateConfirmed(true);
        }

        // Signal that this relay status has been read successfully
        // This is needed for controlRelayVerified() verification
        setUpdateEventBits(RELAY_UPDATE_BITS[relayIndex - 1]);

        return RelayErrorCode::SUCCESS;
    }

    return RelayErrorCode::MODBUS_ERROR;
}

ryn4::RelayErrorCode RYN4::readAllRelayStatus() {
    // Check if module is offline - prevent communication when device is unavailable
    if (statusFlags.moduleOffline) {
        RYN4_LOG_D("Module is offline - cannot read all relay status");
        return RelayErrorCode::MODBUS_ERROR;
    }
    
    RYN4_LOG_D("Reading all relay status...");

    // Read all 8 relay status registers in one request
    // Use STATUS priority for status reads - lowest priority to avoid blocking sensor reads
    auto result = readHoldingRegistersWithPriority(0x0000, NUM_RELAYS, esp32Modbus::STATUS);
    RYN4_TRACK_MODBUS_RESULT(result);
    if (result.isOk() && result.value().size() == NUM_RELAYS) {
        MutexGuard lock(instanceMutex, mutexTimeout);
        if (!lock) {
            return RelayErrorCode::MUTEX_ERROR;
        }

        for (int i = 0; i < NUM_RELAYS; i++) {
            // Hardware returns 0x0001 for ON, 0x0000 for OFF (different from command values!)
            bool state = (result.value()[i] == 0x0001);  // 0x0001 = ON, 0x0000 = OFF
            bool previousState = relays[i].isOn();
            
            relays[i].setOn(state);
            relays[i].setStateConfirmed(true);
            relays[i].lastUpdateTime = xTaskGetTickCount();
            
            if (previousState != state) {
                setUpdateEventBits(RELAY_UPDATE_BITS[i]);
                RYN4_LOG_I("Relay %d state: %s", i + 1, state ? "ON" : "OFF");
            }
        }

        // Signal that relay config/status has been read successfully
        // This is needed for setMultipleRelayStatesVerified() verification
        setInitializationBit(InitBits::RELAY_CONFIG);

        return RelayErrorCode::SUCCESS;
    }
    
    return RelayErrorCode::MODBUS_ERROR;
}