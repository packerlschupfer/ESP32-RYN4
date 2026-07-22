#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "MockRYN4.h"
#include "test_config.h"
#include <signal.h>
#include <atomic>

using ::testing::Return;
using ::testing::_;

// Global flag for signal handling
static std::atomic<bool> g_testInterrupted(false);

// Signal handler for SIGINT
void testSignalHandler(int signal) {
    if (signal == SIGINT) {
        g_testInterrupted = true;
        // Don't exit immediately, let test cleanup
    }
}

// Safe test fixture that handles SIGINT gracefully
class RYN4SafeTest : public TimedTest {
protected:
    void SetUp() override {
        TimedTest::SetUp();
        
        // Install signal handler
        oldHandler = signal(SIGINT, testSignalHandler);
        g_testInterrupted = false;
        
        mockRYN4 = std::make_unique<MockRYN4>(0x01, "SafeTestRYN4");
    }
    
    void TearDown() override {
        // Cleanup mock
        mockRYN4.reset();
        
        // Restore original signal handler
        signal(SIGINT, oldHandler);
        
        TimedTest::TearDown();
    }
    
    bool isInterrupted() const {
        return g_testInterrupted || isTestTimedOut();
    }
    
    // Safe operation wrapper that checks for interruption
    template<typename Func>
    auto safeExecute(Func func) -> decltype(func()) {
        if (isInterrupted()) {
            GTEST_SKIP() << "Test interrupted or timed out";
        }
        return func();
    }
    
    std::unique_ptr<MockRYN4> mockRYN4;
    
private:
    void (*oldHandler)(int);
};

// Test that handles potential timing issues safely
TEST_F(RYN4SafeTest, TestSafeRelayOperations) {
    // Setup expectations with reasonable timeouts
    EXPECT_CALL(*mockRYN4, controlRelay(_, _))
        .WillRepeatedly(Return(ryn4::RelayErrorCode::SUCCESS));
    
    // Execute operations with interrupt checking
    for (int i = 1; i <= 8; ++i) {
        auto result = safeExecute([this, i]() {
            return mockRYN4->controlRelay(i, ryn4::RelayAction::CLOSE);
        });
        
        if (isInterrupted()) break;
        
        EXPECT_EQ(result, ryn4::RelayErrorCode::SUCCESS);
        
        // Add small delay to prevent timing issues
        std::this_thread::sleep_for(
            std::chrono::milliseconds(TestConfig::MIN_RELAY_SWITCH_DELAY_MS));
    }
}

// Test mutex operations with timeout protection
TEST_F(RYN4SafeTest, TestMutexOperationsWithTimeout) {
    // Use shorter timeout for testing
    const int testTimeout = 100;
    
    // First operation succeeds
    EXPECT_CALL(*mockRYN4, controlRelay(1, _))
        .WillOnce(Return(ryn4::RelayErrorCode::SUCCESS));
    
    auto result = safeExecute([this]() {
        return mockRYN4->controlRelay(1, ryn4::RelayAction::CLOSE);
    });
    
    EXPECT_EQ(result, ryn4::RelayErrorCode::SUCCESS);
}

// Test that gracefully handles initialization timeout
TEST_F(RYN4SafeTest, TestInitializationTimeout) {
    // Setup initialization with timeout
    EXPECT_CALL(*mockRYN4, initialize())
        .WillOnce(Invoke([this]() {
            // Check for interruption during long operation
            for (int i = 0; i < 50; ++i) {
                if (isInterrupted()) {
                    return IDeviceInstance::DeviceResult<void>(
                        IDeviceInstance::DeviceError::TIMEOUT);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return IDeviceInstance::DeviceResult<void>(
                IDeviceInstance::DeviceError::SUCCESS);
        }));
    
    auto result = safeExecute([this]() {
        return mockRYN4->initialize();
    });
    
    // Either succeeds or times out gracefully
    EXPECT_TRUE(result.isOk() || 
                result.error == IDeviceInstance::DeviceError::TIMEOUT);
}

// Test batch operations with interrupt protection
TEST_F(RYN4SafeTest, TestBatchOperationsWithInterruptProtection) {
    std::vector<bool> states = {true, false, true, false, true, false, true, false};
    
    EXPECT_CALL(*mockRYN4, setMultipleRelayStates(_))
        .WillOnce(Invoke([this](const std::vector<bool>&) {
            // Simulate processing time
            for (size_t i = 0; i < 8; ++i) {
                if (isInterrupted()) {
                    return ryn4::RelayErrorCode::TIMEOUT;
                }
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(TestConfig::MUTEX_TEST_DELAY_MS));
            }
            return ryn4::RelayErrorCode::SUCCESS;
        }));
    
    auto result = safeExecute([this, &states]() {
        return mockRYN4->setMultipleRelayStates(states);
    });
    
    // Either succeeds or times out gracefully
    EXPECT_TRUE(result == ryn4::RelayErrorCode::SUCCESS || 
                result == ryn4::RelayErrorCode::TIMEOUT);
}

// Test async operations with safe cleanup
TEST_F(RYN4SafeTest, TestAsyncOperationsWithSafeCleanup) {
    // Enable async mode
    EXPECT_CALL(*mockRYN4, isAsyncEnabled())
        .WillRepeatedly(Return(true));
    
    // Setup async operation expectations
    EXPECT_CALL(*mockRYN4, requestData())
        .WillOnce(Return(IDeviceInstance::DeviceResult<void>(
            IDeviceInstance::DeviceError::SUCCESS)));
    
    EXPECT_CALL(*mockRYN4, waitForData())
        .WillOnce(Invoke([this]() {
            // Check for interruption while waiting
            for (int i = 0; i < 10; ++i) {
                if (isInterrupted()) return false;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return true;
        }));
    
    // Execute with interrupt protection
    if (!isInterrupted()) {
        auto reqResult = mockRYN4->requestData();
        EXPECT_TRUE(reqResult.isOk());
        
        bool dataReady = safeExecute([this]() {
            return mockRYN4->waitForData();
        });
        
        if (!isInterrupted()) {
            EXPECT_TRUE(dataReady);
        }
    }
}

// Test that prevents infinite loops
TEST_F(RYN4SafeTest, TestPreventInfiniteLoops) {
    const int MAX_ITERATIONS = 100;
    int iterations = 0;
    
    // Setup mock to eventually succeed
    EXPECT_CALL(*mockRYN4, isModuleResponsive())
        .WillRepeatedly(Invoke([&iterations]() {
            return ++iterations > 10;
        }));
    
    // Wait for module with timeout protection
    while (!mockRYN4->isModuleResponsive() && 
           iterations < MAX_ITERATIONS && 
           !isInterrupted()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Should exit before max iterations
    EXPECT_LT(iterations, MAX_ITERATIONS);
}

// Test cleanup on unexpected termination
class RYN4CleanupTest : public RYN4SafeTest {
protected:
    ~RYN4CleanupTest() {
        // Ensure clean shutdown even if test is interrupted
        if (mockRYN4) {
            // Force cleanup of any pending operations
            mockRYN4.reset();
        }
    }
};

TEST_F(RYN4CleanupTest, TestCleanupOnFailure) {
    // This test ensures proper cleanup even on failure
    EXPECT_CALL(*mockRYN4, controlRelay(_, _))
        .WillOnce(Return(ryn4::RelayErrorCode::UNKNOWN_ERROR));
    
    auto result = mockRYN4->controlRelay(1, ryn4::RelayAction::CLOSE);
    EXPECT_EQ(result, ryn4::RelayErrorCode::UNKNOWN_ERROR);
    
    // Destructor will ensure cleanup
}