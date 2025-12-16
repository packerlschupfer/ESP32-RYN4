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
    
    LOG_INFO(TASK_TAG, "Attempting to create task with TaskManager...");
    
    // Create the processing task using TaskManager like Boiler Controller
    // Configure watchdog with 30 second timeout
    TaskManager::WatchdogConfig wdtConfig = TaskManager::WatchdogConfig::enabled(
        false,  // not critical
        30000   // 30 second timeout
    );
    
    // Pass the RYN4 device as parameter to the task
    if (!taskManager.startTaskPinned(
        task,           // Task function
        "RYN4Proc",     // Task name
        4096,           // Stack size
        ryn4Device,     // Pass device as parameter
        2,              // Priority
        1,              // Pin to core 1
        wdtConfig)) {   // Watchdog configuration
        LOG_ERROR(TASK_TAG, "Failed to create task with startTaskPinned");
        return false;
    }
    
    LOG_INFO(TASK_TAG, "TaskManager.startTaskPinned returned success");
    
    // Retrieve the task handle after creation
    taskHandle = taskManager.getTaskHandleByName("RYN4Proc");
    
    if (taskHandle == nullptr) {
        LOG_ERROR(TASK_TAG, "Failed to retrieve task handle after creation");
        return false;
    }
    
    LOG_INFO(TASK_TAG, "Retrieved task handle: %p", taskHandle);
    
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
    // Get the RYN4 device from parameters
    RYN4* device = static_cast<RYN4*>(pvParameters);
    
    if (!device) {
        LOG_ERROR(TASK_TAG, "Started with null RYN4 instance");
        vTaskDelete(NULL);
        return;
    }
    
    LOG_INFO(TASK_TAG, "Started C%d Stk:%d", xPortGetCoreID(), uxTaskGetStackHighWaterMark(NULL) * 4);
    
    // Register with watchdog
    TaskManager::WatchdogConfig wdtConfig = TaskManager::WatchdogConfig::enabled(
        false,  // not critical
        30000   // 30 second timeout
    );
    
    if (!taskManager.registerCurrentTaskWithWatchdog("RYN4Proc", wdtConfig)) {
        LOG_WARN(TASK_TAG, "Failed to register with watchdog");
    }
    
    // RYN4 handles queue internally
    LOG_INFO(TASK_TAG, "Waiting for device initialization...");
    vTaskDelay(pdMS_TO_TICKS(1000)); // Give device time to initialize
    
    LOG_INFO(TASK_TAG, "Entering main processing loop");
    
    // Main processing loop - simple like Boiler Controller
    while (true) {
        // RYN4 handles packet processing internally
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        // Feed watchdog
        taskManager.feedWatchdog();
    }
}