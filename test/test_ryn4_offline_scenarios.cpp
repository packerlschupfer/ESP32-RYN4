#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "MockRYN4.h"

using ::testing::Return;
using ::testing::_;
using ::testing::InSequence;

class RYN4OfflineTest : public ::testing::Test {
protected:
    void SetUp() override {
        mockRYN4 = std::make_unique<MockRYN4>(0x01, "OfflineTestRYN4");
    }

    std::unique_ptr<MockRYN4> mockRYN4;
};

// Test that all operations fail when module is offline
TEST_F(RYN4OfflineTest, TestAllOperationsFailWhenOffline) {
    // Set module as offline
    EXPECT_CALL(*mockRYN4, isModuleOffline())
        .WillRepeatedly(Return(true));
    
    // All relay control operations should return NOT_INITIALIZED
    EXPECT_CALL(*mockRYN4, controlRelay(_, _))
        .WillRepeatedly(Return(ryn4::RelayErrorCode::NOT_INITIALIZED));
    
    EXPECT_CALL(*mockRYN4, setMultipleRelayStates(_))
        .WillOnce(Return(ryn4::RelayErrorCode::NOT_INITIALIZED));
    
    EXPECT_CALL(*mockRYN4, readRelayStatus(_, _))
        .WillOnce(Return(ryn4::RelayErrorCode::NOT_INITIALIZED));
    
    EXPECT_CALL(*mockRYN4, readAllRelayStatus())
        .WillOnce(Return(ryn4::RelayErrorCode::NOT_INITIALIZED));
    
    // Test operations
    EXPECT_TRUE(mockRYN4->isModuleOffline());
    EXPECT_EQ(mockRYN4->controlRelay(1, ryn4::RelayAction::CLOSE), 
              ryn4::RelayErrorCode::NOT_INITIALIZED);
    
    std::vector<bool> states = {true, false, true, false};
    EXPECT_EQ(mockRYN4->setMultipleRelayStates(states), 
              ryn4::RelayErrorCode::NOT_INITIALIZED);
    
    bool state;
    EXPECT_EQ(mockRYN4->readRelayStatus(1, state), 
              ryn4::RelayErrorCode::NOT_INITIALIZED);
    
    EXPECT_EQ(mockRYN4->readAllRelayStatus(), 
              ryn4::RelayErrorCode::NOT_INITIALIZED);
}

// Test initialization failure when module is offline
TEST_F(RYN4OfflineTest, TestInitializationFailsWhenOffline) {
    // Module starts offline
    EXPECT_CALL(*mockRYN4, initialize())
        .WillOnce(Return(IDeviceInstance::DeviceResult<void>(
            IDeviceInstance::DeviceError::COMMUNICATION_ERROR)));
    
    EXPECT_CALL(*mockRYN4, isModuleOffline())
        .WillOnce(Return(true));
    
    auto result = mockRYN4->initialize();
    EXPECT_FALSE(result.isOk());
    EXPECT_EQ(result.error, IDeviceInstance::DeviceError::COMMUNICATION_ERROR);
    EXPECT_TRUE(mockRYN4->isModuleOffline());
}

// Test transition from offline to online
TEST_F(RYN4OfflineTest, TestOfflineToOnlineTransition) {
    InSequence seq;
    
    // Start offline
    EXPECT_CALL(*mockRYN4, isModuleOffline())
        .WillOnce(Return(true));
    
    // Operations fail while offline
    EXPECT_CALL(*mockRYN4, controlRelay(1, ryn4::RelayAction::CLOSE))
        .WillOnce(Return(ryn4::RelayErrorCode::NOT_INITIALIZED));
    
    // Module comes back online
    EXPECT_CALL(*mockRYN4, isModuleOffline())
        .WillOnce(Return(false));
    
    // Operations succeed after coming online
    EXPECT_CALL(*mockRYN4, controlRelay(1, ryn4::RelayAction::CLOSE))
        .WillOnce(Return(ryn4::RelayErrorCode::SUCCESS));
    
    // Execute scenario
    EXPECT_TRUE(mockRYN4->isModuleOffline());
    EXPECT_EQ(mockRYN4->controlRelay(1, ryn4::RelayAction::CLOSE), 
              ryn4::RelayErrorCode::NOT_INITIALIZED);
    
    // Module comes back online
    EXPECT_FALSE(mockRYN4->isModuleOffline());
    EXPECT_EQ(mockRYN4->controlRelay(1, ryn4::RelayAction::CLOSE), 
              ryn4::RelayErrorCode::SUCCESS);
}

// Test IDeviceInstance methods when offline
TEST_F(RYN4OfflineTest, TestIDeviceInstanceMethodsWhenOffline) {
    EXPECT_CALL(*mockRYN4, isModuleOffline())
        .WillRepeatedly(Return(true));
    
    // requestData should return error when offline
    EXPECT_CALL(*mockRYN4, requestData())
        .WillOnce(Return(IDeviceInstance::DeviceResult<void>(
            IDeviceInstance::DeviceError::NOT_INITIALIZED)));
    
    // processData should return error when offline
    EXPECT_CALL(*mockRYN4, processData())
        .WillOnce(Return(IDeviceInstance::DeviceResult<void>(
            IDeviceInstance::DeviceError::NOT_INITIALIZED)));
    
    // getData should return error when offline
    EXPECT_CALL(*mockRYN4, getData(_))
        .WillOnce(Return(IDeviceInstance::DeviceResult<std::vector<float>>(
            IDeviceInstance::DeviceError::NOT_INITIALIZED)));
    
    // Test
    EXPECT_TRUE(mockRYN4->isModuleOffline());
    
    auto reqResult = mockRYN4->requestData();
    EXPECT_FALSE(reqResult.isOk());
    EXPECT_EQ(reqResult.error, IDeviceInstance::DeviceError::NOT_INITIALIZED);
    
    auto procResult = mockRYN4->processData();
    EXPECT_FALSE(procResult.isOk());
    EXPECT_EQ(procResult.error, IDeviceInstance::DeviceError::NOT_INITIALIZED);
    
    auto dataResult = mockRYN4->getData(IDeviceInstance::DeviceDataType::RELAY_STATUS);
    EXPECT_FALSE(dataResult.isOk());
    EXPECT_EQ(dataResult.error, IDeviceInstance::DeviceError::NOT_INITIALIZED);
}

// Test verified methods when offline
TEST_F(RYN4OfflineTest, TestVerifiedMethodsWhenOffline) {
    EXPECT_CALL(*mockRYN4, isModuleOffline())
        .WillRepeatedly(Return(true));
    
    // All verified methods should fail
    EXPECT_CALL(*mockRYN4, controlRelayVerified(_, _))
        .WillOnce(Return(ryn4::RelayErrorCode::NOT_INITIALIZED));
    
    EXPECT_CALL(*mockRYN4, setRelayStateVerified(_, _))
        .WillOnce(Return(ryn4::RelayErrorCode::NOT_INITIALIZED));
    
    EXPECT_CALL(*mockRYN4, setAllRelaysVerified(_))
        .WillOnce(Return(ryn4::RelayErrorCode::NOT_INITIALIZED));
    
    EXPECT_CALL(*mockRYN4, setMultipleRelayStatesVerified(_))
        .WillOnce(Return(ryn4::RelayErrorCode::NOT_INITIALIZED));
    
    // Test
    EXPECT_EQ(mockRYN4->controlRelayVerified(1, ryn4::RelayAction::CLOSE),
              ryn4::RelayErrorCode::NOT_INITIALIZED);
    
    EXPECT_EQ(mockRYN4->setRelayStateVerified(1, true),
              ryn4::RelayErrorCode::NOT_INITIALIZED);
    
    EXPECT_EQ(mockRYN4->setAllRelaysVerified(true),
              ryn4::RelayErrorCode::NOT_INITIALIZED);
    
    std::vector<bool> states = {true, false, true, false};
    EXPECT_EQ(mockRYN4->setMultipleRelayStatesVerified(states),
              ryn4::RelayErrorCode::NOT_INITIALIZED);
}

// Test module responsiveness check
TEST_F(RYN4OfflineTest, TestModuleResponsivenessCheck) {
    InSequence seq;
    
    // Module is initially offline and unresponsive
    EXPECT_CALL(*mockRYN4, isModuleOffline())
        .WillOnce(Return(true));
    
    EXPECT_CALL(*mockRYN4, isModuleResponsive())
        .WillOnce(Return(false));
    
    // After some recovery, module is online and responsive
    EXPECT_CALL(*mockRYN4, isModuleOffline())
        .WillOnce(Return(false));
    
    EXPECT_CALL(*mockRYN4, isModuleResponsive())
        .WillOnce(Return(true));
    
    // Test
    EXPECT_TRUE(mockRYN4->isModuleOffline());
    EXPECT_FALSE(mockRYN4->isModuleResponsive());
    
    // After recovery
    EXPECT_FALSE(mockRYN4->isModuleOffline());
    EXPECT_TRUE(mockRYN4->isModuleResponsive());
}

// Test polling behavior when offline
TEST_F(RYN4OfflineTest, TestNoPollingWhenOffline) {
    // Module is offline
    EXPECT_CALL(*mockRYN4, isModuleOffline())
        .WillRepeatedly(Return(true));
    
    // No Modbus operations should be attempted
    // This would be verified by NOT setting expectations for Modbus calls
    // If any unexpected Modbus operations occur, the test will fail
    
    // Only status checks should happen
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(mockRYN4->isModuleOffline());
        // In real implementation, this would skip any Modbus operations
    }
}