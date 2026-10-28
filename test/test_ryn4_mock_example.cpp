#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "MockRYN4.h"

using ::testing::Return;
using ::testing::_;
using ::testing::DoAll;
using ::testing::SetArgReferee;
using ::testing::Invoke;

class RYN4MockTest : public ::testing::Test {
protected:
    void SetUp() override {
        mockRYN4 = std::make_unique<MockRYN4>(0x01, "TestRYN4");
    }

    void TearDown() override {
        mockRYN4.reset();
    }

    std::unique_ptr<MockRYN4> mockRYN4;
};

// Test basic relay control
TEST_F(RYN4MockTest, TestBasicRelayControl) {
    // Setup expectations
    EXPECT_CALL(*mockRYN4, controlRelay(1, ryn4::RelayAction::CLOSE))
        .WillOnce(Return(ryn4::RelayErrorCode::SUCCESS));
    
    EXPECT_CALL(*mockRYN4, controlRelay(1, ryn4::RelayAction::OPEN))
        .WillOnce(Return(ryn4::RelayErrorCode::SUCCESS));
    
    // Execute
    auto result1 = mockRYN4->controlRelay(1, ryn4::RelayAction::CLOSE);
    auto result2 = mockRYN4->controlRelay(1, ryn4::RelayAction::OPEN);
    
    // Verify
    EXPECT_EQ(result1, ryn4::RelayErrorCode::SUCCESS);
    EXPECT_EQ(result2, ryn4::RelayErrorCode::SUCCESS);
}

// Test error handling
TEST_F(RYN4MockTest, TestErrorHandling) {
    // Setup expectation for invalid relay index
    EXPECT_CALL(*mockRYN4, controlRelay(9, _))
        .WillOnce(Return(ryn4::RelayErrorCode::INVALID_INDEX));
    
    // Execute
    auto result = mockRYN4->controlRelay(9, ryn4::RelayAction::CLOSE);
    
    // Verify
    EXPECT_EQ(result, ryn4::RelayErrorCode::INVALID_INDEX);
}

// Test reading relay status
TEST_F(RYN4MockTest, TestReadRelayStatus) {
    bool relayState = false;
    
    // Setup expectation to set the relay state to true
    EXPECT_CALL(*mockRYN4, readRelayStatus(1, _))
        .WillOnce(DoAll(
            SetArgReferee<1>(true),
            Return(ryn4::RelayErrorCode::SUCCESS)
        ));
    
    // Execute
    auto result = mockRYN4->readRelayStatus(1, relayState);
    
    // Verify
    EXPECT_EQ(result, ryn4::RelayErrorCode::SUCCESS);
    EXPECT_TRUE(relayState);
}

// Test multiple relay states
TEST_F(RYN4MockTest, TestSetMultipleRelayStates) {
    std::vector<bool> states = {true, false, true, false, true, false, true, false};
    
    EXPECT_CALL(*mockRYN4, setMultipleRelayStates(states))
        .WillOnce(Return(ryn4::RelayErrorCode::SUCCESS));
    
    auto result = mockRYN4->setMultipleRelayStates(states);
    EXPECT_EQ(result, ryn4::RelayErrorCode::SUCCESS);
}

// Test verified relay control
TEST_F(RYN4MockTest, TestVerifiedRelayControl) {
    EXPECT_CALL(*mockRYN4, controlRelayVerified(1, ryn4::RelayAction::CLOSE))
        .WillOnce(Return(ryn4::RelayErrorCode::SUCCESS));
    
    auto result = mockRYN4->controlRelayVerified(1, ryn4::RelayAction::CLOSE);
    EXPECT_EQ(result, ryn4::RelayErrorCode::SUCCESS);
}

// Test module offline status
TEST_F(RYN4MockTest, TestModuleOfflineStatus) {
    // Test when module is online
    EXPECT_CALL(*mockRYN4, isModuleOffline())
        .WillOnce(Return(false));
    
    EXPECT_FALSE(mockRYN4->isModuleOffline());
    
    // Test when module is offline
    EXPECT_CALL(*mockRYN4, isModuleOffline())
        .WillOnce(Return(true));
    
    EXPECT_TRUE(mockRYN4->isModuleOffline());
}

// Test initialization
TEST_F(RYN4MockTest, TestInitialization) {
    EXPECT_CALL(*mockRYN4, initialize())
        .WillOnce(Return(IDeviceInstance::DeviceResult<void>(
            IDeviceInstance::DeviceError::SUCCESS)));
    
    auto result = mockRYN4->initialize();
    EXPECT_TRUE(result.isOk());
}

// Test async mode
TEST_F(RYN4MockTest, TestAsyncMode) {
    EXPECT_CALL(*mockRYN4, isAsyncEnabled())
        .WillOnce(Return(false))
        .WillOnce(Return(true));
    
    EXPECT_FALSE(mockRYN4->isAsyncEnabled());
    EXPECT_TRUE(mockRYN4->isAsyncEnabled());
}

// Test IDeviceInstance getData
TEST_F(RYN4MockTest, TestGetData) {
    std::vector<float> expectedData = {1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f};
    
    EXPECT_CALL(*mockRYN4, getData(IDeviceInstance::DeviceDataType::RELAY_STATUS))
        .WillOnce(Return(IDeviceInstance::DeviceResult<std::vector<float>>(
            IDeviceInstance::DeviceError::SUCCESS, expectedData)));
    
    auto result = mockRYN4->getData(IDeviceInstance::DeviceDataType::RELAY_STATUS);
    EXPECT_TRUE(result.isOk());
    EXPECT_EQ(result.value, expectedData);
}

// Test error injection
TEST_F(RYN4MockTest, TestErrorInjection) {
    // Use the mock's error injection feature
    mockRYN4->setNextOperationError(ryn4::RelayErrorCode::TIMEOUT);
    
    // Setup mock to use the injected error
    EXPECT_CALL(*mockRYN4, controlRelay(_, _))
        .WillOnce(Invoke([this](uint8_t, ryn4::RelayAction) {
            return ryn4::RelayErrorCode::TIMEOUT;
        }));
    
    auto result = mockRYN4->controlRelay(1, ryn4::RelayAction::CLOSE);
    EXPECT_EQ(result, ryn4::RelayErrorCode::TIMEOUT);
}

// Test configuration methods
TEST_F(RYN4MockTest, TestConfigurationMethods) {
    EXPECT_CALL(*mockRYN4, reqBaudRate())
        .WillOnce(Return(true));
    
    EXPECT_CALL(*mockRYN4, reqParity())
        .WillOnce(Return(true));
    
    EXPECT_CALL(*mockRYN4, reqAddress())
        .WillOnce(Return(true));
    
    EXPECT_CALL(*mockRYN4, reqReturnDelay())
        .WillOnce(Return(true));
    
    EXPECT_TRUE(mockRYN4->reqBaudRate());
    EXPECT_TRUE(mockRYN4->reqParity());
    EXPECT_TRUE(mockRYN4->reqAddress());
    EXPECT_TRUE(mockRYN4->reqReturnDelay());
}

// Test complex scenario with multiple expectations
TEST_F(RYN4MockTest, TestComplexScenario) {
    // Scenario: Initialize, check status, control relays, read status
    
    // 1. Initialize
    EXPECT_CALL(*mockRYN4, initialize())
        .WillOnce(Return(IDeviceInstance::DeviceResult<void>(
            IDeviceInstance::DeviceError::SUCCESS)));
    
    // 2. Check if initialized
    EXPECT_CALL(*mockRYN4, isInitialized())
        .WillRepeatedly(Return(true));
    
    // 3. Check if module is online
    EXPECT_CALL(*mockRYN4, isModuleOffline())
        .WillRepeatedly(Return(false));
    
    // 4. Control multiple relays
    EXPECT_CALL(*mockRYN4, controlRelay(_, _))
        .Times(4)
        .WillRepeatedly(Return(ryn4::RelayErrorCode::SUCCESS));
    
    // 5. Read all relay status
    EXPECT_CALL(*mockRYN4, readAllRelayStatus())
        .WillOnce(Return(ryn4::RelayErrorCode::SUCCESS));
    
    // Execute scenario
    ASSERT_TRUE(mockRYN4->initialize().isOk());
    ASSERT_TRUE(mockRYN4->isInitialized());
    ASSERT_FALSE(mockRYN4->isModuleOffline());
    
    for (int i = 1; i <= 4; ++i) {
        ASSERT_EQ(mockRYN4->controlRelay(i, ryn4::RelayAction::CLOSE), 
                  ryn4::RelayErrorCode::SUCCESS);
    }
    
    ASSERT_EQ(mockRYN4->readAllRelayStatus(), ryn4::RelayErrorCode::SUCCESS);
}

// Test with simulated states
TEST_F(RYN4MockTest, TestSimulatedStates) {
    // Set up some simulated states
    mockRYN4->setSimulatedRelayState(1, true);
    mockRYN4->setSimulatedRelayState(2, false);
    mockRYN4->setSimulatedModuleOffline(false);
    mockRYN4->setSimulatedInitialized(true);
    
    // Verify the simulated states
    EXPECT_TRUE(mockRYN4->getSimulatedRelayState(1));
    EXPECT_FALSE(mockRYN4->getSimulatedRelayState(2));
    
    // Set up mock to use simulated states
    EXPECT_CALL(*mockRYN4, isInitialized())
        .WillOnce(Invoke([this]() { return true; }));
    
    EXPECT_CALL(*mockRYN4, isModuleOffline())
        .WillOnce(Invoke([this]() { return false; }));
    
    EXPECT_TRUE(mockRYN4->isInitialized());
    EXPECT_FALSE(mockRYN4->isModuleOffline());
}

// Main function for running tests
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}