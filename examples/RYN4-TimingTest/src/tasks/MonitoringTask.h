// tasks/MonitoringTask.h
#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "config/ProjectConfig.h"
#include <EthernetManager.h>

/**
 * @brief System monitoring task for RYN4 relay controller
 * 
 * Periodically logs system health, network status, and relay module information
 */
class MonitoringTask {
public:
    /**
     * Initialize the monitoring task
     * @return true if initialization successful, false otherwise
     */
    static bool init();

    /**
     * Start the monitoring task
     * @return true if task started successfully, false otherwise
     */
    static bool start();

    /**
     * Stop the monitoring task
     */
    static void stop();

    /**
     * Check if the task is running
     * @return true if task is running, false otherwise
     */
    static bool isRunning();

    /**
     * Get the task handle
     * @return Task handle or nullptr if not running
     */
    static TaskHandle_t getTaskHandle();

private:
    /**
     * Main task function
     * @param pvParameters Task parameters (unused)
     */
    static void taskFunction(void* pvParameters);

    /**
     * Log system health information
     */
    static void logSystemHealth();

    /**
     * Log network status
     */
    static void logNetworkStatus();

    /**
     * Log RYN4 relay module status
     */
    static void logRelayModuleStatus();

    /**
     * Log relay operation statistics
     */
    static void logRelayStatistics();

    void trackMemoryAllocation(const char* location, size_t size);


    // Task configuration
    static constexpr const char* TASK_NAME = "MonitoringTask";
    static constexpr uint32_t STACK_SIZE = STACK_SIZE_MONITORING_TASK;
    static constexpr UBaseType_t TASK_PRIORITY = PRIORITY_MONITORING_TASK;
    static constexpr const char* TASK_TAG = LOG_TAG_MONITORING;
    static constexpr uint32_t MONITOR_INTERVAL_MS = MONITORING_TASK_INTERVAL_MS;

    // Task state
    static TaskHandle_t taskHandle;
};
