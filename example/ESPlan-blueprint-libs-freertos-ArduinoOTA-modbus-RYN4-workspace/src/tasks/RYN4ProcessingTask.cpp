// RYN4ProcessingTask.cpp
#include "LoggingMacros.h"
#include "RYN4ProcessingTask.h"
#include "config/ProjectConfig.h"
#include <TaskManager.h>

extern TaskManager taskManager;

#define TASK_TAG "RYN4Proc"

// Static member initialization
RYN4* RYN4ProcessingTask::ryn4Device = nullptr;
TaskHandle_t RYN4ProcessingTask::taskHandle = nullptr;
bool RYN4ProcessingTask::initialized = false;
bool RYN4ProcessingTask::running = false;
uint32_t RYN4ProcessingTask::packetsProcessed = 0;
uint32_t RYN4ProcessingTask::packetsDropped = 0;
TickType_t RYN4ProcessingTask::lastProcessTime = 0;

bool RYN4ProcessingTask::init(RYN4* device) {
    if (initialized) {
        LOG_WARN(TASK_TAG, "Task already initialized");
        return true;
    }
    
    if (!device) {
        LOG_ERROR(TASK_TAG, "Invalid device pointer");
        return false;
    }
    
    ryn4Device = device;
    packetsProcessed = 0;
    packetsDropped = 0;
    lastProcessTime = 0;
    initialized = true;
    
    LOG_INFO(TASK_TAG, "RYN4 processing task initialized");
    return true;
}

bool RYN4ProcessingTask::start() {
    if (!initialized) {
        LOG_ERROR(TASK_TAG, "Task not initialized");
        return false;
    }
    
    if (running) {
        LOG_WARN(TASK_TAG, "Task already running");
        return true;
    }
    
    // Create the processing task using TaskManager for consistency
    // Disable watchdog initially - will be enabled after initialization
    TaskManager::WatchdogConfig wdtConfig = TaskManager::WatchdogConfig::disabled();
    
    if (!taskManager.startTask(task, "RYN4Proc", 4096, nullptr, 2, wdtConfig)) {
        LOG_ERROR(TASK_TAG, "Failed to create task");
        return false;
    }
    
    // Retrieve the task handle after creation
    taskHandle = taskManager.getTaskHandleByName("RYN4Proc");
    
    running = true;
    LOG_INFO(TASK_TAG, "RYN4 processing task started");
    return true;
}

void RYN4ProcessingTask::stop() {
    if (!running || !taskHandle) {
        return;
    }
    
    running = false;
    
    // Give task time to exit gracefully
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Stop the task using TaskManager for proper cleanup
    if (taskHandle) {
        taskManager.stopTask(taskHandle);
        taskHandle = nullptr;
    }
    
    LOG_INFO(TASK_TAG, "RYN4 processing task stopped");
}

void RYN4ProcessingTask::resetStats() {
    packetsProcessed = 0;
    packetsDropped = 0;
}

void RYN4ProcessingTask::task(void* pvParameters) {
    LOG_INFO(TASK_TAG, "RYN4 processing task started, waiting for queued mode...");
    
    // Watchdog is already registered by TaskManager in start()
    // No need to register again
    
    // Wait for device to enable queued mode after initialization
    while (running && !ryn4Device->isAsyncEnabled()) {
        // Check if device is offline
        if (ryn4Device->isModuleOffline()) {
            LOG_WARN(TASK_TAG, "RYN4 device is offline, task entering idle mode");
            break;
        }
        taskManager.feedWatchdog();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    if (!running) {
        // Task is being stopped, exit cleanly
        return;
    }
    
    // If device is offline, enter minimal monitoring mode
    if (ryn4Device->isModuleOffline()) {
        LOG_INFO(TASK_TAG, "RYN4 device offline - monitoring mode");
        while (running) {
            // Just feed watchdog and wait
            taskManager.feedWatchdog();
            vTaskDelay(pdMS_TO_TICKS(1000));  // Check once per second
            
            // Periodically check if device came back online
            if (!ryn4Device->isModuleOffline() && ryn4Device->isAsyncEnabled()) {
                LOG_INFO(TASK_TAG, "RYN4 device back online, resuming normal operation");
                break;
            }
        }
    }
    
    LOG_INFO(TASK_TAG, "RYN4 queue mode active, starting event-driven packet processing");
    
    while (running) {
        // Wait for notification with timeout for watchdog feeding
        uint32_t notificationValue = 0;
        BaseType_t notified = xTaskNotifyWait(
            0,                          // Don't clear bits on entry
            0xFFFFFFFF,                 // Clear all bits on exit
            &notificationValue,         // Store notification value
            pdMS_TO_TICKS(5000)         // 5 second timeout for watchdog
        );
        
        // Check if device went offline
        if (ryn4Device->isModuleOffline()) {
            LOG_WARN(TASK_TAG, "RYN4 device went offline during operation");
            // Continue waiting for it to come back online
            taskManager.feedWatchdog();
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        
        // Only process when notified - no periodic housekeeping
        if (notified == pdTRUE) {
            // Only log every 10th notification to avoid flooding
            static uint32_t notificationCount = 0;
            notificationCount++;
            if (notificationCount % 10 == 1) {
                LOG_DEBUG(TASK_TAG, "Processing due to notification #%lu", notificationCount);
            }
            
            // Process any queued packets
            ryn4Device->processData();
            
            // Update statistics
            lastProcessTime = xTaskGetTickCount();
            packetsProcessed++;
        }
        
        // Feed watchdog
        taskManager.feedWatchdog();
        
        // No need for explicit yield when using blocking wait
    }
    
    LOG_INFO(TASK_TAG, "RYN4 processing task exiting");
    // TaskManager will handle task deletion
}