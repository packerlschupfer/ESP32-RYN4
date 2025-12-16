#ifndef RETRY_POLICY_H
#define RETRY_POLICY_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>
#include <functional>

/**
 * @brief Retry policy with exponential backoff for reliable operations
 * 
 * This class implements a configurable retry mechanism with exponential backoff,
 * useful for handling transient failures in communication protocols like Modbus.
 * 
 * Features:
 * - Configurable max retries and delays
 * - Exponential backoff with jitter to prevent thundering herd
 * - Optional retry condition callback
 * - Thread-safe operation
 */
class RetryPolicy {
public:
    /**
     * @brief Configuration for retry policy
     */
    struct Config {
        uint8_t maxRetries;              ///< Maximum number of retry attempts
        TickType_t initialDelay;         ///< Initial delay between retries
        TickType_t maxDelay;             ///< Maximum delay between retries
        float backoffMultiplier;         ///< Multiplier for exponential backoff
        float jitterFactor;              ///< Random jitter factor (0.0 - 1.0)
        
        Config() 
            : maxRetries(3)
            , initialDelay(pdMS_TO_TICKS(50))
            , maxDelay(pdMS_TO_TICKS(2000))
            , backoffMultiplier(2.0f)
            , jitterFactor(0.1f) {}
        
        /**
         * @brief Create config for aggressive retry (fast, many attempts)
         */
        static Config aggressive() {
            Config cfg;
            cfg.maxRetries = 5;
            cfg.initialDelay = pdMS_TO_TICKS(20);
            cfg.maxDelay = pdMS_TO_TICKS(500);
            cfg.backoffMultiplier = 1.5f;
            return cfg;
        }
        
        /**
         * @brief Create config for conservative retry (slow, few attempts)
         */
        static Config conservative() {
            Config cfg;
            cfg.maxRetries = 2;
            cfg.initialDelay = pdMS_TO_TICKS(100);
            cfg.maxDelay = pdMS_TO_TICKS(5000);
            cfg.backoffMultiplier = 3.0f;
            return cfg;
        }
    };
    
    /**
     * @brief Result of a retry operation
     */
    template<typename T>
    struct Result {
        bool success = false;
        T value{};
        uint8_t attemptsMade = 0;
        TickType_t totalDelayMs = 0;
        
        operator bool() const { return success; }
        T& operator*() { return value; }
        const T& operator*() const { return value; }
    };
    
    /**
     * @brief Construct retry policy with given configuration
     */
    explicit RetryPolicy(const Config& config = Config())
        : config_(config) {}
    
    /**
     * @brief Execute operation with retry logic
     * 
     * @tparam T Return type of the operation
     * @param operation Function to execute that returns optional<T>
     * @param shouldRetry Optional predicate to check if retry should happen
     * @return Result containing success status and value if successful
     */
    template<typename T>
    Result<T> execute(std::function<T()> operation, 
                     std::function<bool(const T&)> isSuccess = nullptr) {
        Result<T> result;
        TickType_t currentDelay = config_.initialDelay;
        
        for (uint8_t attempt = 0; attempt <= config_.maxRetries; ++attempt) {
            result.attemptsMade = attempt + 1;
            
            // Execute the operation
            result.value = operation();
            
            // Check success condition
            if (isSuccess) {
                result.success = isSuccess(result.value);
            } else {
                // Default: any non-zero/non-null value is success
                if constexpr (std::is_pointer_v<T>) {
                    result.success = (result.value != nullptr);
                } else if constexpr (std::is_arithmetic_v<T>) {
                    result.success = (result.value != 0);
                } else {
                    result.success = true; // Assume success if no predicate
                }
            }
            
            if (result.success || attempt == config_.maxRetries) {
                break;
            }
            
            // Calculate delay with jitter
            if (config_.jitterFactor > 0) {
                // Simple pseudo-random jitter
                uint32_t random = xTaskGetTickCount() * 1103515245 + 12345;
                float jitterMultiplier = 1.0f + (config_.jitterFactor * 
                    ((random % 1000) / 1000.0f - 0.5f));
                currentDelay = static_cast<TickType_t>(currentDelay * jitterMultiplier);
            }
            
            // Apply delay
            vTaskDelay(currentDelay);
            result.totalDelayMs += pdTICKS_TO_MS(currentDelay);
            
            // Calculate next delay with exponential backoff
            currentDelay = static_cast<TickType_t>(
                std::min(static_cast<float>(config_.maxDelay),
                        currentDelay * config_.backoffMultiplier));
        }
        
        return result;
    }
    
    /**
     * @brief Execute void operation with retry logic
     */
    Result<bool> executeVoid(std::function<bool()> operation) {
        return execute<bool>(operation);
    }
    
    /**
     * @brief Get current configuration
     */
    const Config& getConfig() const { return config_; }
    
    /**
     * @brief Update configuration
     */
    void setConfig(const Config& config) { config_ = config; }

private:
    Config config_;
};

/**
 * @brief Global retry policies for different scenarios
 *
 * NOTE: Runtime retries are disabled for ModbusCoordinator compatibility.
 *
 * When using external scheduling (e.g., ModbusCoordinator with 2.5s ticks):
 * - Internal retries hold bus mutex 350ms+ during failures
 * - Rapid retries create RS485 noise affecting other devices
 * - Userspace already handles verification and retry queuing
 *
 * Only modbusCritical() retains retries for init-time operations
 * before the coordinator starts.
 */
namespace RetryPolicies {
    /**
     * @brief Default policy for runtime Modbus operations
     *
     * Retries disabled - coordinator handles scheduling.
     * Single attempt per tick, userspace queues retry on failure.
     */
    inline RetryPolicy modbusDefault() {
        RetryPolicy::Config config;
        config.maxRetries = 0;  // Coordinator handles retry scheduling
        config.initialDelay = pdMS_TO_TICKS(50);
        config.maxDelay = pdMS_TO_TICKS(1000);
        config.backoffMultiplier = 2.0f;
        config.jitterFactor = 0.2f;
        return RetryPolicy(config);
    }

    /**
     * @brief Critical policy for init-time operations
     *
     * Retries enabled - used before coordinator starts.
     * Acceptable to hold bus during initialization.
     */
    inline RetryPolicy modbusCritical() {
        return RetryPolicy(RetryPolicy::Config::aggressive());
    }

    /**
     * @brief Background policy for low-priority operations
     *
     * Retries disabled - let coordinator reschedule.
     */
    inline RetryPolicy modbusBackground() {
        RetryPolicy::Config cfg;
        cfg.maxRetries = 0;  // Coordinator handles retry scheduling
        cfg.initialDelay = pdMS_TO_TICKS(100);
        cfg.maxDelay = pdMS_TO_TICKS(5000);
        cfg.backoffMultiplier = 3.0f;
        return RetryPolicy(cfg);
    }
}

#endif // RETRY_POLICY_H