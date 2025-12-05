#ifndef MOCK_RYN4_RELAY_H
#define MOCK_RYN4_RELAY_H

#include "RYN4.h"
#include <gmock/gmock.h>
#include <vector>
#include <map>

class MockRYN4 : public RYN4 {
public:
    MockRYN4(uint8_t slaveID, const char* tag = "MockRYN4")
        : RYN4(slaveID, tag) {
        // Initialize simulated states
        for (int i = 0; i < 8; ++i) {
            simulatedRelayStates[i] = false;
            simulatedRelayModes[i] = ryn4::RelayMode::NORMAL;
        }
        simulatedModuleOffline = false;
        simulatedInitialized = false;
        simulatedAsyncEnabled = false;
    }

    // Core control methods
    MOCK_METHOD(ryn4::RelayErrorCode, controlRelay, (uint8_t relayIndex, ryn4::RelayAction action), (override));
    MOCK_METHOD(ryn4::RelayErrorCode, setMultipleRelayStates, (const std::vector<bool>& states), (override));
    MOCK_METHOD(ryn4::RelayErrorCode, readRelayStatus, (uint8_t relayIndex, bool& state), (override));
    MOCK_METHOD(ryn4::RelayErrorCode, readAllRelayStatus, (), (override));
    
    // Verified control methods
    MOCK_METHOD(ryn4::RelayErrorCode, controlRelayVerified, (uint8_t relayIndex, ryn4::RelayAction action), (override));
    MOCK_METHOD(ryn4::RelayErrorCode, setMultipleRelayStatesVerified, (const std::vector<bool>& states), (override));
    MOCK_METHOD(ryn4::RelayErrorCode, setRelayStateVerified, (uint8_t relayIndex, bool state), (override));
    MOCK_METHOD(ryn4::RelayErrorCode, setAllRelaysVerified, (bool state), (override));
    
    // IDeviceInstance methods
    MOCK_METHOD(IDeviceInstance::DeviceResult<void>, initialize, (), (override));
    MOCK_METHOD(IDeviceInstance::DeviceResult<void>, requestData, (), (override));
    MOCK_METHOD(IDeviceInstance::DeviceResult<void>, processData, (), (override));
    MOCK_METHOD(IDeviceInstance::DeviceResult<void>, performAction, (int actionId, int relayIndex), (override));
    MOCK_METHOD(IDeviceInstance::DeviceResult<std::vector<float>>, getData, (IDeviceInstance::DeviceDataType dataType), (override));
    MOCK_METHOD(bool, waitForData, (), (override));
    
    // Status methods
    MOCK_METHOD(bool, isInitialized, (), (const, noexcept, override));
    MOCK_METHOD(bool, isModuleOffline, (), (const, override));
    MOCK_METHOD(bool, isModuleResponsive, (), (override));
    MOCK_METHOD(bool, isAsyncEnabled, (), (const, override));
    
    // Configuration methods
    MOCK_METHOD(bool, reqReturnDelay, (), (override));
    MOCK_METHOD(bool, reqAddress, (), (override));
    MOCK_METHOD(bool, reqBaudRate, (), (override));
    MOCK_METHOD(bool, reqParity, (), (override));
    
    // Test helper methods
    void setSimulatedRelayState(uint8_t relayIndex, bool state) {
        if (relayIndex >= 1 && relayIndex <= 8) {
            simulatedRelayStates[relayIndex - 1] = state;
        }
    }
    
    bool getSimulatedRelayState(uint8_t relayIndex) const {
        if (relayIndex >= 1 && relayIndex <= 8) {
            return simulatedRelayStates[relayIndex - 1];
        }
        return false;
    }
    
    void setSimulatedModuleOffline(bool offline) {
        simulatedModuleOffline = offline;
    }
    
    void setSimulatedInitialized(bool initialized) {
        simulatedInitialized = initialized;
    }
    
    void setSimulatedAsyncEnabled(bool enabled) {
        simulatedAsyncEnabled = enabled;
    }
    
    void setSimulatedRelayMode(uint8_t relayIndex, ryn4::RelayMode mode) {
        if (relayIndex >= 1 && relayIndex <= 8) {
            simulatedRelayModes[relayIndex - 1] = mode;
        }
    }
    
    // Simulate module settings
    void setSimulatedModuleSettings(const ModuleSettings& settings) {
        simulatedSettings = settings;
    }
    
    // Error injection for testing
    void setNextOperationError(ryn4::RelayErrorCode error) {
        nextError = error;
        hasNextError = true;
    }
    
    void clearNextOperationError() {
        hasNextError = false;
    }

private:
    bool simulatedRelayStates[8];
    ryn4::RelayMode simulatedRelayModes[8];
    bool simulatedModuleOffline;
    bool simulatedInitialized;
    bool simulatedAsyncEnabled;
    ModuleSettings simulatedSettings;
    
    // Error injection
    ryn4::RelayErrorCode nextError = ryn4::RelayErrorCode::SUCCESS;
    bool hasNextError = false;
};

#endif // MOCK_RYN4_RELAY_H
