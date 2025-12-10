// tasks/RelayStatusTask.cpp
#include "LoggingMacros.h"
#include "RelayStatusTask.h"
#include <stdlib.h>  // For abs() function
#include "TaskManager.h"
#include <SemaphoreGuard.h>
#include <esp_task_wdt.h>
#include <string>
#include <algorithm>

// Utility macros
#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

#ifndef CLAMP
#define CLAMP(x, low, high) (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))
#endif

// Task identification
#define TASK_NAME "RelayStatusTask"
#define TASK_TAG LOG_TAG_RELAY_STATUS
#define STACK_SIZE STACK_SIZE_RELAY_STATUS_TASK
#define TASK_PRIORITY PRIORITY_RELAY_STATUS_TASK
#define STATUS_INTERVAL_MS RELAY_STATUS_TASK_INTERVAL_MS

extern TaskManager taskManager;

// Static member initialization
RYN4* RelayStatusTask::ryn4Device = nullptr;
TaskHandle_t RelayStatusTask::taskHandle = nullptr;
SemaphoreHandle_t RelayStatusTask::taskMutex = nullptr;
bool RelayStatusTask::initialized = false;
bool RelayStatusTask::running = false;
TickType_t RelayStatusTask::lastUpdateTime = 0;
uint32_t RelayStatusTask::updateCount = 0;
uint32_t RelayStatusTask::errorCount = 0;
bool RelayStatusTask::lastRelayStates[8] = {false};
bool RelayStatusTask::statesInitialized = false;

bool RelayStatusTask::init(RYN4* device) {
    if (initialized) {
        LOG_WARN(TASK_TAG, "Task already initialized");
        return true;
    }
    
    if (!device) {
        LOG_ERROR(TASK_TAG, "Invalid device pointer");
        return false;
    }
    
    // Create mutex
    taskMutex = xSemaphoreCreateMutex();
    if (!taskMutex) {
        LOG_ERROR(TASK_TAG, "Failed to create mutex");
        return false;
    }
    
    // Store device reference
    ryn4Device = device;
    initialized = true;
    
    LOG_INFO(TASK_TAG, "Relay status task initialized");
    return true;
}

bool RelayStatusTask::start() {
    if (!initialized) {
        LOG_ERROR(TASK_TAG, "Task not initialized - call init() first");
        return false;
    }
    
    if (running) {
        LOG_WARN(TASK_TAG, "Task already running");
        return true;
    }
    
    // Create the task using TaskManager with watchdog config
    TaskManager::WatchdogConfig wdtConfig = TaskManager::WatchdogConfig::enabled(
        false, RELAY_STATUS_TASK_WATCHDOG_TIMEOUT_MS);
    
    bool success = taskManager.startTask(
        taskFunction,
        TASK_NAME,
        STACK_SIZE,
        nullptr,
        TASK_PRIORITY,
        wdtConfig
    );
    
    if (success) {
        // Retrieve the task handle after creation
        taskHandle = taskManager.getTaskHandleByName(TASK_NAME);
        running = true;
        LOG_INFO(TASK_TAG, "Task started with %d ms watchdog timeout", 
                 RELAY_STATUS_TASK_WATCHDOG_TIMEOUT_MS);
    } else {
        LOG_ERROR(TASK_TAG, "Failed to create task");
    }
    
    return success;
}

void RelayStatusTask::stop() {
    if (!running || !taskHandle) {
        LOG_WARN(TASK_TAG, "Task not running");
        return;
    }
    
    running = false;
    
    // Notify task to wake up and exit
    if (taskHandle) {
        xTaskNotifyGive(taskHandle);
    }
    
    // Give task time to exit cleanly
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Delete task if it hasn't exited
    if (taskHandle) {
        vTaskDelete(taskHandle);
        taskHandle = nullptr;
    }
    
    LOG_INFO(TASK_TAG, "Relay status task stopped");
}

TaskHandle_t RelayStatusTask::getTaskHandle() {
    return taskHandle;
}

bool RelayStatusTask::isRunning() {
    return running && taskHandle != nullptr;
}

TickType_t RelayStatusTask::getLastUpdateTime() {
    return lastUpdateTime;
}

void RelayStatusTask::requestImmediateUpdate() {
    if (taskHandle) {
        // Send notification with specific bit pattern
        xTaskNotify(taskHandle, SENSOR_UPDATE_BIT, eSetBits);
    }
}

void RelayStatusTask::taskFunction(void* pvParameters) {
    LOG_INFO(TASK_TAG, "Task started - entering adaptive monitoring mode");
    
    // Small delay to ensure task is fully registered before first feed
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // RYN4 is already initialized in main.cpp before this task starts
    // No need to wait for initialization
    
    // Skip initial status update - we already have the state from initialization
    // This prevents immediate polling when the task starts
    LOG_INFO(TASK_TAG, "Skipping initial poll - using state from initialization");
    
    // Statistics tracking
    uint32_t pollCount = 0;
    uint32_t notificationCount = 0;
    uint32_t consecutiveTimeouts = 0;
    uint32_t noChangeCount = 0;  // Track consecutive polls with no changes
    
    // Timing control
    TickType_t lastPollTime = xTaskGetTickCount();
    TickType_t lastDetailedReport = xTaskGetTickCount();
    TickType_t lastActivityTime = xTaskGetTickCount();
    
    // Adaptive timing parameters
    const TickType_t MIN_POLL_INTERVAL = pdMS_TO_TICKS(60000);    // 1 minute (was 30s)
    const TickType_t MAX_POLL_INTERVAL = pdMS_TO_TICKS(600000);   // 10 minutes (was 5min)
    const TickType_t INITIAL_POLL_INTERVAL = pdMS_TO_TICKS(120000); // 2 minutes (was 1min)
    const TickType_t DETAILED_REPORT_INTERVAL = pdMS_TO_TICKS(300000); // 5 minutes
    const TickType_t ACTIVITY_TIMEOUT = pdMS_TO_TICKS(120000);   // 2 minutes
    const uint32_t NO_CHANGE_THRESHOLD = 5;  // Number of polls with no changes before increasing interval

    TickType_t currentPollInterval = INITIAL_POLL_INTERVAL;
    bool isActiveMode = false;
    bool stateChanged = false;  // Track if state changed in current iteration

    // Get event groups for monitoring
    EventGroupHandle_t eventGroup = ryn4Device->getEventGroup();
    EventGroupHandle_t updateEventGroup = ryn4Device->getUpdateEventGroup();
    EventGroupHandle_t errorEventGroup = ryn4Device->getErrorEventGroup();
    
    // Import the global bit groups from RYN4
    extern EventBits_t relayAllUpdateBits;
    extern EventBits_t relayAllErrorBits;
    
    // Main loop - adaptive between event-driven and polling
    while (running) {
        // Feed the watchdog at the start of each iteration
        taskManager.feedWatchdog();
        
        // Calculate time until next poll
        TickType_t timeSinceLastPoll = xTaskGetTickCount() - lastPollTime;
        TickType_t timeToWait = (timeSinceLastPoll < currentPollInterval) ? 
                                 (currentPollInterval - timeSinceLastPoll) : 0;
        
        // Limit wait time for watchdog feeding
        if (timeToWait > pdMS_TO_TICKS(5000)) {
            timeToWait = pdMS_TO_TICKS(5000);
        }
        
        // Wait for notifications
        uint32_t notificationValue = 0;
        BaseType_t notifyResult = xTaskNotifyWait(
            0,                    
            0xFFFFFFFF,          
            &notificationValue,   
            timeToWait           
        );
        
        TickType_t currentTime = xTaskGetTickCount();
        stateChanged = false;  // Reset for this iteration
        
        if (notifyResult == pdTRUE && notificationValue != 0) {
            // Notification received
            consecutiveTimeouts = 0;
            lastActivityTime = currentTime;
            
            // Check for data ready notification
            if (notificationValue & SENSOR_UPDATE_BIT) {
                notificationCount++;
                
                #if defined(LOG_MODE_DEBUG_SELECTIVE) || defined(LOG_MODE_DEBUG_FULL)
                if (notificationCount % 10 == 1) { // Log every 10th notification
                    LOG_DEBUG(TASK_TAG, "Processing notification #%lu", 
                             (unsigned long)notificationCount);
                }
                #endif

                // Process the data that's already available
                size_t previousUpdateCount = updateCount;
                processNotificationData();
                
                // Check if processNotificationData detected any changes
                // (it increments updateCount)
                if (updateCount > previousUpdateCount) {
                    stateChanged = true;
                    noChangeCount = 0;  // Reset no-change counter
                    
                    // Enter active mode - reduce polling interval
                    if (!isActiveMode) {
                        isActiveMode = true;
                        currentPollInterval = MIN_POLL_INTERVAL;
                        LOG_INFO(TASK_TAG, "Entering active mode - notifications detected");
                    }
                }
            }
            
            // Check for error notification
            if (notificationValue & SENSOR_ERROR_BIT) {
                errorCount++;
                LOG_ERROR(TASK_TAG, "Error notification (#%lu)", (unsigned long)errorCount);
                handleStatusError();
            }
        }
        
        // Check if it's time for a poll
        timeSinceLastPoll = currentTime - lastPollTime;
        if (timeSinceLastPoll >= currentPollInterval) {
            // Periodic poll
            pollCount++;
            lastPollTime = currentTime;
            
            #if defined(LOG_MODE_DEBUG_SELECTIVE) || defined(LOG_MODE_DEBUG_FULL)
            if (pollCount % 5 == 1) { // Log every 5th poll
                LOG_DEBUG(TASK_TAG, "Periodic poll #%lu (interval: %lus)", 
                         (unsigned long)pollCount,
                         (unsigned long)(pdTICKS_TO_MS(currentPollInterval) / 1000));
            }
            #endif
            
            // Store relay states before update to detect changes
            bool statesBefore[RYN4_NUM_RELAYS];
            if (statesInitialized) {
                memcpy(statesBefore, lastRelayStates, sizeof(statesBefore));
            }
            
            processStatusUpdate();
            
            // Check if states changed during the poll
            if (statesInitialized) {
                stateChanged = false;
                for (int i = 0; i < RYN4_NUM_RELAYS; i++) {
                    if (statesBefore[i] != lastRelayStates[i]) {
                        stateChanged = true;
                        break;
                    }
                }
            }
            
            // Update no-change tracking and adjust poll interval
            if (!stateChanged) {
                noChangeCount++;
                
                // Check for timeout conditions
                if (notifyResult != pdTRUE) {
                    consecutiveTimeouts++;
                    
                    // Exit active mode after inactivity
                    TickType_t timeSinceActivity = currentTime - lastActivityTime;
                    if (isActiveMode && timeSinceActivity > ACTIVITY_TIMEOUT) {
                        isActiveMode = false;
                        LOG_INFO(TASK_TAG, "Exiting active mode - no recent notifications");
                    }
                }
                
                // Gradually increase poll interval when no changes detected
                if (!isActiveMode && noChangeCount >= NO_CHANGE_THRESHOLD) {
                    TickType_t newInterval = currentPollInterval + currentPollInterval / 2;  // Increase by 50%
                    if (newInterval > MAX_POLL_INTERVAL) {
                        newInterval = MAX_POLL_INTERVAL;
                    }
                    
                    if (newInterval != currentPollInterval) {
                        currentPollInterval = newInterval;
                        
                        #if defined(LOG_MODE_DEBUG_SELECTIVE) || defined(LOG_MODE_DEBUG_FULL)
                        LOG_DEBUG(TASK_TAG, "No changes detected for %lu polls, increased interval to %lus", 
                                 (unsigned long)noChangeCount,
                                 (unsigned long)(pdTICKS_TO_MS(currentPollInterval) / 1000));
                        #endif
                    }
                    
                    // Don't let noChangeCount grow indefinitely
                    if (noChangeCount > NO_CHANGE_THRESHOLD * 2) {
                        noChangeCount = NO_CHANGE_THRESHOLD;
                    }
                }
            } else {
                // State changed - reset counters and potentially reduce interval
                noChangeCount = 0;
                consecutiveTimeouts = 0;
                lastActivityTime = currentTime;
                
                // If we're in idle mode and detected a change, switch to active mode
                if (!isActiveMode) {
                    isActiveMode = true;
                    currentPollInterval = MIN_POLL_INTERVAL;
                    LOG_INFO(TASK_TAG, "State change detected - entering active mode");
                }
            }
        }
        
        // Check event group bits efficiently using bit groups
        if ((pollCount % 10 == 0)) { // Check every 10 iterations
            // Check for any relay updates using bit group
            if (updateEventGroup) {
                EventBits_t updateBits = xEventGroupGetBits(updateEventGroup);
                EventBits_t activeUpdateBits = updateBits & relayAllUpdateBits;
                
                if (activeUpdateBits) {
                    #if defined(LOG_MODE_DEBUG_FULL)
                    LOG_DEBUG(TASK_TAG, "Relay update bits active: 0x%08X", activeUpdateBits);
                    // Show which specific relays have updates
                    for (int i = 0; i < 8; i++) {
                        if (activeUpdateBits & ryn4::RELAY_UPDATE_BITS[i]) {
                            LOG_DEBUG(TASK_TAG, "  - Relay %d has pending update", i + 1);
                        }
                    }
                    #endif
                    
                    // Clear the update bits we've seen
                    xEventGroupClearBits(updateEventGroup, activeUpdateBits);
                }
            }
            
            // Check for any relay errors using bit group
            if (errorEventGroup) {
                EventBits_t errorBits = xEventGroupGetBits(errorEventGroup);
                EventBits_t activeErrorBits = errorBits & relayAllErrorBits;
                
                if (activeErrorBits) {
                    LOG_WARN(TASK_TAG, "Relay error bits active: 0x%08X", activeErrorBits);
                    // Log which specific relays have errors
                    for (int i = 0; i < 8; i++) {
                        if (activeErrorBits & ryn4::RELAY_ERROR_BITS[i]) {
                            LOG_ERROR(TASK_TAG, "Relay %d has error condition", i + 1);
                        }
                    }
                    
                    // Clear the error bits we've processed
                    xEventGroupClearBits(errorEventGroup, activeErrorBits);
                }
            }
            
            // Check the general event group for non-relay specific bits
            if (eventGroup) {
                EventBits_t eventBits = xEventGroupGetBits(eventGroup);
                // Only check for general status bits (not relay-specific)
                EventBits_t generalBits = eventBits & 0x03; // SENSOR_UPDATE_BIT | SENSOR_ERROR_BIT
                
                if (generalBits) {
                    #if defined(LOG_MODE_DEBUG_FULL)
                    LOG_DEBUG(TASK_TAG, "General event bits: 0x%02X", generalBits);
                    #endif
                    
                    // Clear processed bits
                    xEventGroupClearBits(eventGroup, generalBits);
                }
            }
        }
        
        // Periodic detailed report
        if (currentTime - lastDetailedReport >= DETAILED_REPORT_INTERVAL) {
            lastDetailedReport = currentTime;
            
            LOG_INFO(TASK_TAG, "=== STATUS REPORT ===");
            LOG_INFO(TASK_TAG, "Uptime: %lus, Updates: %lu, Errors: %lu",
                     (unsigned long)(pdTICKS_TO_MS(currentTime) / 1000),
                     (unsigned long)updateCount,
                     (unsigned long)errorCount);
            LOG_INFO(TASK_TAG, "Polls: %lu, Notifications: %lu, No-change polls: %lu",
                     (unsigned long)pollCount,
                     (unsigned long)notificationCount,
                     (unsigned long)noChangeCount);
            
            if (updateCount > 0) {
                uint32_t successRate = ((updateCount - errorCount) * 100) / updateCount;
                LOG_INFO(TASK_TAG, "Success rate: %lu%%", (unsigned long)successRate);
            }
            
            LOG_INFO(TASK_TAG, "Mode: %s, Poll interval: %lus",
                     isActiveMode ? "Active" : "Idle",
                     (unsigned long)(pdTICKS_TO_MS(currentPollInterval) / 1000));
            
            // Log relay states
            logRelayStates();
            
            // Quick check if any relay has pending updates or errors
            if (updateEventGroup && errorEventGroup) {
                EventBits_t pendingUpdates = xEventGroupGetBits(updateEventGroup) & relayAllUpdateBits;
                EventBits_t pendingErrors = xEventGroupGetBits(errorEventGroup) & relayAllErrorBits;
                
                if (pendingUpdates || pendingErrors) {
                    LOG_WARN(TASK_TAG, "Pending events - Updates: 0x%08X, Errors: 0x%08X",
                            pendingUpdates, pendingErrors);
                }
            }
            
            // Check module health
            if (!ryn4Device->isModuleResponsive()) {
                LOG_ERROR(TASK_TAG, "Module not responsive!");
                // Reset to active mode to try recovering
                isActiveMode = true;
                currentPollInterval = MIN_POLL_INTERVAL;
                noChangeCount = 0;
            }
        }
        
        // Feed watchdog before next iteration
        taskManager.feedWatchdog();
    }
    
    LOG_INFO(TASK_TAG, "Task ending - Updates: %lu, Errors: %lu", 
             updateCount, errorCount);
    
    // Clean up
    taskHandle = nullptr;
    vTaskDelete(nullptr);
}

void RelayStatusTask::processNotificationData(const IDeviceInstance::DeviceResult<std::vector<float>>* existingResult) {
    // This method processes data that's already available from a notification
    // It does NOT request new data unless existingResult is nullptr
    
    if (!ryn4Device) {
        return;
    }
    
    // Use throttled logging
    #if defined(LOG_MODE_DEBUG_FULL)
    static uint32_t notificationLogCount = 0;
    if (++notificationLogCount % 10 == 1) {
        LOG_DEBUG(TASK_TAG, "Processing notification data (#%lu)", notificationLogCount);
    }
    #endif
    
    IDeviceInstance::DeviceResult<std::vector<float>> result;
    
    if (existingResult) {
        // Use the provided result - no need to call getData again
        result = *existingResult;
    } else {
        // Only call getData if we don't have existing data
        result = ryn4Device->getData(IDeviceInstance::DeviceDataType::RELAY_STATE);
    }
    
    if (!result.isOk() || result.value.size() != RYN4_NUM_RELAYS) {
        handleStatusError();
        return;
    }
    
    // Update stats
    lastUpdateTime = xTaskGetTickCount();
    updateCount++;
    
    // Check for changes and log appropriately
    // checkForStateChanges handles all change detection and logging
    checkForStateChanges(result);
}

void RelayStatusTask::processStatusUpdate() {
    // This method is called on timeout and DOES request fresh data
    
    if (!ryn4Device) {
        return;
    }
    
    // Use static tracking for throttled logging
    #if defined(LOG_MODE_DEBUG_FULL)
    static uint32_t pollLogCount = 0;
    if (++pollLogCount % 5 == 1) {
        LOG_DEBUG(TASK_TAG, "Processing periodic status update (#%lu)", pollLogCount);
    }
    #endif
    
    // Try to acquire mutex with shorter timeout
    SemaphoreGuard guard(ryn4Device->getMutexInterface(), pdMS_TO_TICKS(500));
    if (!guard.hasLock()) {
        LOG_WARN(TASK_TAG, "Mutex timeout in processStatusUpdate");
        handleStatusError();
        return;
    }
    
    // Request data
    if (!ryn4Device->requestData()) {
        LOG_ERROR(TASK_TAG, "Failed to request relay data");
        handleStatusError();
        return;
    }
    
    // Wait for data to be ready
    EventGroupHandle_t eventGroup = ryn4Device->getEventGroup();
    if (!eventGroup) {
        handleStatusError();
        return;
    }
    
    // Wait for response with shorter timeout
    taskManager.feedWatchdog();
    
    EventBits_t bits = xEventGroupWaitBits(
        eventGroup,
        SENSOR_UPDATE_BIT | SENSOR_ERROR_BIT,
        pdTRUE,     // Clear bits
        pdFALSE,    // Wait for any
        pdMS_TO_TICKS(500)  // Reduced from 1000ms
    );
    
    // Feed watchdog after wait
    taskManager.feedWatchdog();
    
    // Quick error check
    if ((bits & SENSOR_ERROR_BIT) || !(bits & SENSOR_UPDATE_BIT)) {
        handleStatusError();
        return;
    }
    
    // Get the data and process inline to avoid double call
    IDeviceInstance::DeviceResult<std::vector<float>> result = ryn4Device->getData(
        IDeviceInstance::DeviceDataType::RELAY_STATE);
    
    if (result.isOk()) {
        // Process inline instead of calling processNotificationData
        lastUpdateTime = xTaskGetTickCount();
        updateCount++;
        checkForStateChanges(result);
    } else {
        handleStatusError();
    }
}

void RelayStatusTask::checkForStateChanges(const IDeviceInstance::DeviceResult<std::vector<float>>& result) {
    // Fast path - no changes most of the time
    static bool fastCheckEnabled = true;
    bool anyChange = false;
    
    // Quick scan for any changes
    if (statesInitialized && fastCheckEnabled) {
        for (int i = 0; i < RYN4_NUM_RELAYS; i++) {
            bool currentState = result.value[i] > 0.5f;
            if (lastRelayStates[i] != currentState) {
                anyChange = true;
                break;
            }
        }
        
        // Fast exit if no changes
        if (!anyChange) {
            return;
        }
    }
    
    // Detailed change processing
    int changedRelays = 0;
    static char changeBuffer[128];
    int bufPos = 0;
    
    for (int i = 0; i < RYN4_NUM_RELAYS; i++) {
        bool currentState = result.value[i] > 0.5f;
        
        if (!statesInitialized || lastRelayStates[i] != currentState) {
            if (statesInitialized) {
                changedRelays++;
                
                // Batch change notifications
                if (changedRelays == 1) {
                    bufPos = snprintf(changeBuffer, sizeof(changeBuffer), 
                                    "Relay changes: %d(%s->%s)",
                                    i + 1, 
                                    lastRelayStates[i] ? "ON" : "OFF",
                                    currentState ? "ON" : "OFF");
                } else if (bufPos < sizeof(changeBuffer) - 20) {
                    bufPos += snprintf(changeBuffer + bufPos, 
                                     sizeof(changeBuffer) - bufPos,
                                     ", %d(%s->%s)", 
                                     i + 1,
                                     lastRelayStates[i] ? "ON" : "OFF",
                                     currentState ? "ON" : "OFF");
                }
            }
            
            lastRelayStates[i] = currentState;
        }
    }
    
    if (!statesInitialized) {
        statesInitialized = true;
        LOG_INFO(TASK_TAG, "Initial relay states captured");
        logRelayStatesFromResult(result);
    } else if (changedRelays > 0) {
        LOG_INFO(TASK_TAG, "%s", changeBuffer);
        
        #ifdef ENABLE_RELAY_EVENT_LOGGING
        LOG_INFO(LOG_TAG_RELAY, "[EVENT] %d relay(s) changed at tick %lu",
                 changedRelays, xTaskGetTickCount());
        #endif
        
        // Log new state after changes
        logRelayStatesFromResult(result);
    }
}

void RelayStatusTask::logRelayStates() {
    // Log the last known relay states without fetching new data
    if (!statesInitialized) {
        return;
    }
    
    // Use compact format
    uint8_t activeBits = 0;
    int activeCount = 0;
    
    for (int i = 0; i < RYN4_NUM_RELAYS; i++) {
        if (lastRelayStates[i]) {
            activeBits |= (1 << i);
            activeCount++;
        }
    }
    
    // Compact representation: "Active: R1,R3,R5 (3/8)" or "All OFF (0/8)"
    if (activeCount == 0) {
        LOG_INFO(TASK_TAG, "All relays OFF (0/%d)", RYN4_NUM_RELAYS);
    } else if (activeCount == RYN4_NUM_RELAYS) {
        LOG_INFO(TASK_TAG, "All relays ON (%d/%d)", RYN4_NUM_RELAYS, RYN4_NUM_RELAYS);
    } else {
        // Binary representation for quick visualization
        char binStr[9];
        for (int i = 0; i < 8; i++) {
            binStr[7-i] = (activeBits & (1 << i)) ? '1' : '0';
        }
        binStr[8] = '\0';
        LOG_INFO(TASK_TAG, "Relays [%s] Active: %d/%d", binStr, activeCount, RYN4_NUM_RELAYS);
    }
}

void RelayStatusTask::logRelayStatesFromResult(const IDeviceInstance::DeviceResult<std::vector<float>>& result) {
    // Optimized version using bit manipulation
    uint8_t stateBits = 0;
    int activeCount = 0;
    
    // Convert to bit representation
    for (int i = 0; i < RYN4_NUM_RELAYS; i++) {
        if (result.value[i] > 0.5f) {
            stateBits |= (1 << i);
            activeCount++;
        }
    }
    
    // Use different formats based on state
    if (activeCount == 0) {
        LOG_INFO(TASK_TAG, "All relays OFF [00000000]");
    } else if (activeCount == RYN4_NUM_RELAYS) {
        LOG_INFO(TASK_TAG, "All relays ON [11111111]");
    } else {
        // Binary representation for mixed states
        char binStr[9];
        for (int i = 0; i < 8; i++) {
            binStr[7-i] = (stateBits & (1 << i)) ? '1' : '0';
        }
        binStr[8] = '\0';
        
        LOG_INFO(TASK_TAG, "Relays [%s] Active: %d/%d", 
                 binStr, activeCount, RYN4_NUM_RELAYS);
    }
}

void RelayStatusTask::handleStatusError() {
    errorCount++;
    
    // Exponential backoff for error logging
    static uint32_t errorLogThreshold = 10;
    
    if (errorCount >= errorLogThreshold) {
        // Convert to integer with 1 decimal place
        int errorRateInt = (updateCount > 0) ? 
            (int)((float)errorCount / (float)(updateCount + errorCount) * 1000.0f) : 1000;
            
        LOG_WARN(TASK_TAG, "Error rate: %d.%d%% (%lu errors / %lu total)",
                 errorRateInt / 10, abs(errorRateInt % 10), 
                 (unsigned long)errorCount, 
                 (unsigned long)(updateCount + errorCount));
        
        errorLogThreshold = MIN(errorLogThreshold * 2, 1000); // Cap at 1000
    }
    
    // Critical error detection
    if (errorCount > 100 && updateCount < 10) {
        static bool criticalErrorLogged = false;
        if (!criticalErrorLogged) {
            LOG_ERROR(TASK_TAG, "CRITICAL: Excessive errors with minimal successful updates");
            LOG_ERROR(TASK_TAG, "Device may need power cycle or connection check");
            criticalErrorLogged = true;
        }
    }
    
    // Auto-recovery attempt
    if (errorCount > 50 && (errorCount % 50) == 0) {
        LOG_INFO(TASK_TAG, "Attempting recovery - requesting module check");
        if (ryn4Device) {
            // Force a responsiveness check on next update
            taskManager.feedWatchdog();
            ryn4Device->isModuleResponsive();
        }
    }
}
