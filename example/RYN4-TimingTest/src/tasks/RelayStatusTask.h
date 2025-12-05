// tasks/RelayStatusTask.h
#pragma once

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "config/ProjectConfig.h"
#include "RYN4.h"

/**
 * @brief Task responsible for monitoring relay status and handling status updates
 * 
 * This task uses an adaptive event-driven architecture to efficiently monitor relay states.
 * It operates in two modes:
 * - Active Mode: When receiving notifications, polls every 30 seconds
 * - Idle Mode: When no activity detected, gradually increases poll interval up to 5 minutes
 * 
 * The task primarily waits for notifications from the RYN4 device when new data is available,
 * with periodic polling as a safety net for communication verification.
 */
class RelayStatusTask {
public:
    /**
     * @brief Initialize the relay status task
     * @param device Pointer to the RYN4 device instance
     * @return true if initialization successful
     */
    static bool init(RYN4* device);
    
    /**
     * @brief Start the relay status task
     * @return true if task started successfully
     */
    static bool start();
    
    /**
     * @brief Stop the relay status task
     */
    static void stop();
    
    /**
     * @brief Get the task handle for external notification
     * @return TaskHandle_t of the status task
     */
    static TaskHandle_t getTaskHandle();
    
    /**
     * @brief Check if the task is running
     * @return true if task is active
     */
    static bool isRunning();
    
    /**
     * @brief Get the last update timestamp
     * @return TickType_t of last successful update
     */
    static TickType_t getLastUpdateTime();
    
    /**
     * @brief Force an immediate status update
     * Sends a SENSOR_UPDATE_BIT notification to the task
     */
    static void requestImmediateUpdate();

private:
    // Task function
    static void taskFunction(void* pvParameters);
    
    // Task configuration
    static constexpr const char* TASK_NAME = "RelayStatusTask";
    static constexpr uint32_t STACK_SIZE = STACK_SIZE_RELAY_STATUS_TASK;
    static constexpr UBaseType_t TASK_PRIORITY = PRIORITY_RELAY_STATUS_TASK;
    static constexpr const char* TASK_TAG = LOG_TAG_RELAY_STATUS;
    static constexpr uint32_t STATUS_INTERVAL_MS = RELAY_STATUS_TASK_INTERVAL_MS;
    
    // Task state
    static RYN4* ryn4Device;
    static TaskHandle_t taskHandle;
    static SemaphoreHandle_t taskMutex;
    static bool initialized;
    static bool running;
    static TickType_t lastUpdateTime;
    static uint32_t updateCount;
    static uint32_t errorCount;
    
    // Status tracking
    static bool lastRelayStates[8];
    static bool statesInitialized;
    
    // Private methods
    static void processStatusUpdate();
    static void processNotificationData(const IDeviceInstance::DeviceResult<std::vector<float>>* existingResult = nullptr);
    static void checkForStateChanges(const IDeviceInstance::DeviceResult<std::vector<float>>& result);
    static void logRelayStates();
    static void logRelayStatesFromResult(const IDeviceInstance::DeviceResult<std::vector<float>>& result);
    static void handleStatusError();
};
