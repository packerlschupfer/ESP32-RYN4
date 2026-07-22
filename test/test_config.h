#ifndef TEST_CONFIG_H
#define TEST_CONFIG_H

// Test timing configuration to prevent SIGINT issues
namespace TestConfig {
    // Relay timing constraints (milliseconds)
    constexpr int MIN_RELAY_SWITCH_DELAY_MS = 50;
    constexpr int RELAY_MOMENTARY_DURATION_MS = 500;
    constexpr int RELAY_DELAY_ACTION_MS = 1000;
    constexpr int RELAY_VERIFY_TIMEOUT_MS = 100;
    
    // Mutex timeouts for testing
    constexpr int MUTEX_TIMEOUT_MS = 1000;
    constexpr int MUTEX_TEST_DELAY_MS = 10;
    
    // Module communication timeouts
    constexpr int MODBUS_RESPONSE_TIMEOUT_MS = 200;
    constexpr int MODULE_INIT_TIMEOUT_MS = 5000;
    
    // Test environment settings
    constexpr bool ENABLE_TIMING_CHECKS = true;
    constexpr bool ENABLE_MUTEX_CHECKS = true;
    constexpr bool ENABLE_VERBOSE_LOGGING = false;
    
    // SIGINT prevention settings
    constexpr int MAX_TEST_DURATION_MS = 30000;  // 30 seconds max per test
    constexpr int WATCHDOG_TIMEOUT_MS = 5000;    // 5 second watchdog
}

// Helper macros for timing tests
#define EXPECT_TIMING_GE(actual, expected) \
    EXPECT_GE(actual, expected - 10) << "Timing constraint violated"

#define EXPECT_TIMING_LE(actual, expected) \
    EXPECT_LE(actual, expected + 10) << "Timing constraint violated"

#define MEASURE_TIME(code) \
    [&]() { \
        auto start = std::chrono::steady_clock::now(); \
        code; \
        auto end = std::chrono::steady_clock::now(); \
        return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count(); \
    }()

// Test fixture with timeout protection
class TimedTest : public ::testing::Test {
protected:
    void SetUp() override {
        testStart = std::chrono::steady_clock::now();
    }
    
    void TearDown() override {
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - testStart).count();
            
        if (duration > TestConfig::MAX_TEST_DURATION_MS) {
            FAIL() << "Test exceeded maximum duration: " << duration << "ms";
        }
    }
    
    bool isTestTimedOut() const {
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - testStart).count();
        return duration > TestConfig::MAX_TEST_DURATION_MS;
    }
    
private:
    std::chrono::steady_clock::time_point testStart;
};

#endif // TEST_CONFIG_H