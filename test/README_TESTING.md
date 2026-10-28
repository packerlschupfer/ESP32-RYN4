# RYN4 Testing Guide

This directory contains mock classes and test examples for the RYN4 library.

## Files

- `MockRYN4.h` - Complete mock class for RYN4 with all public methods
- `MockRYN4.cpp` - Mock implementation (minimal, as Google Mock handles most functionality)
- `test_ryn4_mock_example.cpp` - Comprehensive examples of using the mock
- `test_ryn4_offline_scenarios.cpp` - Specific tests for offline module behavior
- `test_ryn4_timing_protection.cpp` - Critical relay timing protection tests
- `test_sigint_safe.cpp` - SIGINT-safe test fixtures for stability
- `test_config.h` - Test configuration and timing constants

## Mock Features

The `MockRYN4` class provides:

### Core Mocking Capabilities
- All public methods from RYN4 are mocked
- Full control over return values and behavior
- Support for both RYN4-specific and IDeviceInstance methods

### Test Helper Methods
```cpp
// Control simulated relay states
void setSimulatedRelayState(uint8_t relayIndex, bool state);
bool getSimulatedRelayState(uint8_t relayIndex) const;

// Control module status
void setSimulatedModuleOffline(bool offline);
void setSimulatedInitialized(bool initialized);
void setSimulatedAsyncEnabled(bool enabled);

// Set relay modes
void setSimulatedRelayMode(uint8_t relayIndex, ryn4::RelayMode mode);

// Configure module settings
void setSimulatedModuleSettings(const ModuleSettings& settings);

// Error injection
void setNextOperationError(ryn4::RelayErrorCode error);
void clearNextOperationError();
```

## Basic Usage

### 1. Simple Test
```cpp
TEST(RYN4Test, BasicRelayControl) {
    MockRYN4 mock(0x01);
    
    EXPECT_CALL(mock, controlRelay(1, ryn4::RelayAction::OFF))
        .WillOnce(Return(ryn4::RelayErrorCode::SUCCESS));
    
    auto result = mock.controlRelay(1, ryn4::RelayAction::OFF);
    EXPECT_EQ(result, ryn4::RelayErrorCode::SUCCESS);
}
```

### 2. Testing Error Conditions
```cpp
TEST(RYN4Test, HandleErrors) {
    MockRYN4 mock(0x01);
    
    // Simulate timeout error
    EXPECT_CALL(mock, controlRelay(_, _))
        .WillOnce(Return(ryn4::RelayErrorCode::TIMEOUT));
    
    auto result = mock.controlRelay(1, ryn4::RelayAction::OFF);
    EXPECT_EQ(result, ryn4::RelayErrorCode::TIMEOUT);
}
```

### 3. Testing Offline Behavior
```cpp
TEST(RYN4Test, OfflineBehavior) {
    MockRYN4 mock(0x01);
    
    EXPECT_CALL(mock, isModuleOffline())
        .WillRepeatedly(Return(true));
    
    EXPECT_CALL(mock, controlRelay(_, _))
        .WillRepeatedly(Return(ryn4::RelayErrorCode::NOT_INITIALIZED));
    
    EXPECT_TRUE(mock.isModuleOffline());
    auto result = mock.controlRelay(1, ryn4::RelayAction::OFF);
    EXPECT_EQ(result, ryn4::RelayErrorCode::NOT_INITIALIZED);
}
```

### 4. Using Simulated States
```cpp
TEST(RYN4Test, SimulatedStates) {
    MockRYN4 mock(0x01);
    
    // Set up simulated states
    mock.setSimulatedRelayState(1, true);
    mock.setSimulatedRelayState(2, false);
    mock.setSimulatedModuleOffline(false);
    
    // Verify states
    EXPECT_TRUE(mock.getSimulatedRelayState(1));
    EXPECT_FALSE(mock.getSimulatedRelayState(2));
}
```

### 5. Complex Scenarios
```cpp
TEST(RYN4Test, CompleteScenario) {
    MockRYN4 mock(0x01);
    InSequence seq;  // Enforce order
    
    // Initialize
    EXPECT_CALL(mock, initialize())
        .WillOnce(Return(IDeviceInstance::DeviceResult<void>(
            IDeviceInstance::DeviceError::SUCCESS)));
    
    // Check status
    EXPECT_CALL(mock, isInitialized())
        .WillOnce(Return(true));
    
    // Control relays
    EXPECT_CALL(mock, setMultipleRelayStates(_))
        .WillOnce(Return(ryn4::RelayErrorCode::SUCCESS));
    
    // Execute
    ASSERT_TRUE(mock.initialize().isOk());
    ASSERT_TRUE(mock.isInitialized());
    
    std::vector<bool> states = {true, false, true, false, true, false, true, false};
    ASSERT_EQ(mock.setMultipleRelayStates(states), ryn4::RelayErrorCode::SUCCESS);
}
```

## Advanced Features

### Error Injection
```cpp
TEST(RYN4Test, ErrorInjection) {
    MockRYN4 mock(0x01);
    
    // Inject an error for the next operation
    mock.setNextOperationError(ryn4::RelayErrorCode::MODBUS_ERROR);
    
    EXPECT_CALL(mock, controlRelay(_, _))
        .WillOnce(Return(ryn4::RelayErrorCode::MODBUS_ERROR));
    
    auto result = mock.controlRelay(1, ryn4::RelayAction::OFF);
    EXPECT_EQ(result, ryn4::RelayErrorCode::MODBUS_ERROR);
}
```

### Testing IDeviceInstance Interface
```cpp
TEST(RYN4Test, IDeviceInstanceMethods) {
    MockRYN4 mock(0x01);
    
    // Test getData
    std::vector<float> expectedData = {1.0f, 0.0f, 1.0f, 0.0f};
    EXPECT_CALL(mock, getData(IDeviceInstance::DeviceDataType::RELAY_STATUS))
        .WillOnce(Return(IDeviceInstance::DeviceResult<std::vector<float>>(
            IDeviceInstance::DeviceError::SUCCESS, expectedData)));
    
    auto result = mock.getData(IDeviceInstance::DeviceDataType::RELAY_STATUS);
    EXPECT_TRUE(result.isOk());
    EXPECT_EQ(result.value, expectedData);
}
```

## Building and Running Tests

### PlatformIO
```bash
# Run all tests
pio test

# Run specific test
pio test -f test_ryn4_mock_example.cpp

# Run with verbose output
pio test -v
```

### Native Build
```bash
# Compile
g++ -std=c++17 -I../src -I. test_ryn4_mock_example.cpp MockRYN4.cpp \
    -lgtest -lgmock -pthread -o test_ryn4

# Run
./test_ryn4
```

## Best Practices

1. **Use InSequence** when order matters:
   ```cpp
   InSequence seq;
   EXPECT_CALL(mock, method1());
   EXPECT_CALL(mock, method2());  // Must be called after method1
   ```

2. **Use WillRepeatedly** for persistent behavior:
   ```cpp
   EXPECT_CALL(mock, isModuleOffline())
       .WillRepeatedly(Return(false));
   ```

3. **Verify error paths** thoroughly:
   - Test timeout conditions
   - Test invalid parameters
   - Test offline module behavior
   - Test mutex errors

4. **Use ASSERT for critical checks**:
   ```cpp
   auto result = mock.initialize();
   ASSERT_TRUE(result.isOk()) << "Initialization must succeed";
   // Continue only if initialization succeeded
   ```

5. **Clean up expectations** between tests using `Mock::VerifyAndClearExpectations(&mock)`

## Integration Testing

For integration tests with real hardware, consider:
1. Using the mock for initial development
2. Creating hardware-in-the-loop tests
3. Using the mock to simulate error conditions that are hard to reproduce

## Timing Protection Tests

The `test_ryn4_timing_protection.cpp` file contains critical tests for:

1. **Rapid Switching Protection** - Prevents relay damage from too-fast switching
2. **Minimum Delay Enforcement** - Ensures proper delays between operations
3. **Concurrent Access Protection** - Tests mutex protection
4. **Momentary/Delay Actions** - Verifies timing-sensitive operations
5. **Verified Operations** - Tests timing with state confirmation

## SIGINT Safety

The `test_sigint_safe.cpp` file provides:

1. **Signal Handling** - Graceful handling of SIGINT
2. **Timeout Protection** - Prevents infinite loops
3. **Safe Cleanup** - Ensures resources are freed on interruption
4. **Interrupt Checking** - Regular checks for test interruption

Use `RYN4SafeTest` as base class for tests that might encounter timing issues:

```cpp
class MyTimingTest : public RYN4SafeTest {
protected:
    void runTimingSensitiveOperation() {
        auto result = safeExecute([this]() {
            // Your timing-sensitive code here
            return mockRYN4->controlRelay(1, ryn4::RelayAction::OFF);
        });
    }
};
```

## Troubleshooting

### Common Issues

1. **SIGINT during tests**
   - Use `RYN4SafeTest` base class
   - Add interrupt checks in long loops
   - Set reasonable timeouts in `test_config.h`

2. **Timing failures**
   - Use `EXPECT_TIMING_GE/LE` macros for tolerance
   - Adjust timing constants in `test_config.h`
   - Consider test environment load

3. **Uninteresting mock function call**
   - Add `EXPECT_CALL` for the method
   - Or use `NiceMock<MockRYN4>` to suppress warnings

4. **Unexpected function call**
   - Check your expectations match actual calls
   - Verify parameter values

5. **Leaked mock object**
   - Ensure proper cleanup in `TearDown()`
   - Use smart pointers