// tasks/RelayControlTask.cpp
#include "LoggingMacros.h"
#include "RelayControlTask.h"
#include "RelayStatusTask.h"
#include "TaskManager.h"
#include <SemaphoreGuard.h>
#include <esp_task_wdt.h>

extern TaskManager taskManager;

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
#define TASK_NAME "RelayControlTask"
#define TASK_TAG LOG_TAG_RELAY_CONTROL
#define STACK_SIZE STACK_SIZE_RELAY_CONTROL_TASK
#define TASK_PRIORITY PRIORITY_RELAY_CONTROL_TASK

// Static member initialization
RYN4* RelayControlTask::ryn4Device = nullptr;
TaskHandle_t RelayControlTask::taskHandle = nullptr;
QueueHandle_t RelayControlTask::commandQueue = nullptr;
SemaphoreHandle_t RelayControlTask::taskMutex = nullptr;
EventGroupHandle_t RelayControlTask::commandEventGroup = nullptr;
bool RelayControlTask::initialized = false;
bool RelayControlTask::running = false;
bool RelayControlTask::busy = false;
uint32_t RelayControlTask::commandsProcessed = 0;
uint32_t RelayControlTask::commandsFailed = 0;
TickType_t RelayControlTask::lastCommandTime = 0;
uint32_t RelayControlTask::toggleCount[8] = {0};
TickType_t RelayControlTask::toggleTimestamps[8] = {0};
TickType_t RelayControlTask::rateWindowStart = 0;

// Static buffer for string operations to avoid stack allocation
static char logBuffer[256];

bool RelayControlTask::init(RYN4* device) {
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
    
    // Create command event group
    commandEventGroup = xEventGroupCreate();
    if (!commandEventGroup) {
        LOG_ERROR(TASK_TAG, "Failed to create command event group");
        vSemaphoreDelete(taskMutex);
        return false;
    }
    
    // Create command queue with larger depth for burst handling
    const size_t queueDepth = 30;  // Increased from 10
    commandQueue = xQueueCreate(queueDepth, sizeof(RelayCommand));
    if (!commandQueue) {
        LOG_ERROR(TASK_TAG, "Failed to create command queue");
        vSemaphoreDelete(taskMutex);
        vEventGroupDelete(commandEventGroup);
        return false;
    }
    
    // Initialize rate limiting
    rateWindowStart = xTaskGetTickCount();
    memset(toggleCount, 0, sizeof(toggleCount));
    memset(toggleTimestamps, 0, sizeof(toggleTimestamps));
    
    // Store device reference
    ryn4Device = device;
    initialized = true;
    
    LOG_INFO(TASK_TAG, "Relay control task initialized");
    return true;
}

bool RelayControlTask::start() {
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
        true, RELAY_CONTROL_TASK_WATCHDOG_TIMEOUT_MS);
    
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
                 RELAY_CONTROL_TASK_WATCHDOG_TIMEOUT_MS);
    } else {
        LOG_ERROR(TASK_TAG, "Failed to create task");
    }
    
    return success;
}

void RelayControlTask::stop() {
    if (!running || !taskHandle) {
        LOG_WARN(TASK_TAG, "Task not running");
        return;
    }
    
    running = false;
    
    // Send a dummy command to wake up the task
    RelayCommand dummyCmd;
    xQueueSend(commandQueue, &dummyCmd, 0);
    
    // Give task time to exit cleanly
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Delete task if it hasn't exited
    if (taskHandle) {
        vTaskDelete(taskHandle);
        taskHandle = nullptr;
    }
    
    // Clear the queue
    if (commandQueue) {
        xQueueReset(commandQueue);
    }
    
    // Clean up event group
    if (commandEventGroup) {
        vEventGroupDelete(commandEventGroup);
        commandEventGroup = nullptr;
    }
    
    LOG_INFO(TASK_TAG, "Relay control task stopped");
}

bool RelayControlTask::isRunning() {
    return running && taskHandle != nullptr;
}

TaskHandle_t RelayControlTask::getTaskHandle() {
    return taskHandle;
}

bool RelayControlTask::toggleRelay(uint8_t relayIndex) {
    if (relayIndex < 1 || relayIndex > RYN4_NUM_RELAYS) {
        LOG_ERROR(TASK_TAG, "Invalid relay: %d", relayIndex);
        return false;
    }
    
    RelayCommand cmd;
    cmd.type = CommandType::TOGGLE;
    cmd.relayIndex = relayIndex;
    cmd.timestamp = xTaskGetTickCount();
    
    return queueCommand(cmd);
}

bool RelayControlTask::setRelayState(uint8_t relayIndex, bool state) {
    if (relayIndex < 1 || relayIndex > RYN4_NUM_RELAYS) {
        LOG_ERROR(TASK_TAG, "Invalid relay: %d", relayIndex);
        return false;
    }
    
    RelayCommand cmd;
    cmd.type = CommandType::SET_SINGLE;
    cmd.relayIndex = relayIndex;
    cmd.state = state;
    cmd.timestamp = xTaskGetTickCount();
    
    return queueCommand(cmd);
}

bool RelayControlTask::setAllRelays(bool state) {
    RelayCommand cmd;
    cmd.type = CommandType::SET_ALL;
    cmd.state = state;
    cmd.timestamp = xTaskGetTickCount();
    
    return queueCommand(cmd);
}

bool RelayControlTask::setMultipleRelays(const std::vector<bool>& states) {
    RelayCommand cmd;
    cmd.type = CommandType::SET_MULTIPLE;
    cmd.stateCount = std::min(states.size(), (size_t)RYN4_NUM_RELAYS);

    // Copy to fixed array and log each state
    for (size_t i = 0; i < cmd.stateCount; i++) {
        cmd.states[i] = states[i];

        // Log each relay state
        LOG_INFO(LOG_TAG_RELAY_CONTROL, "Relay %u state: %s",
                 (unsigned)(i + 1),
                 cmd.states[i] ? "ON" : "OFF");
    }

    return queueCommand(cmd);
}

bool RelayControlTask::toggleAllRelays() {
    RelayCommand cmd;
    cmd.type = CommandType::TOGGLE_ALL;
    cmd.timestamp = xTaskGetTickCount();
    
    return queueCommand(cmd);
}

size_t RelayControlTask::getPendingCommandCount() {
    if (!commandQueue) return 0;
    return uxQueueMessagesWaiting(commandQueue);
}

bool RelayControlTask::isBusy() {
    return busy;
}

void RelayControlTask::getStatistics(uint32_t& processed, uint32_t& failed) {
    // Check if mutex is initialized
    if (!taskMutex) {
        processed = 0;
        failed = 0;
        return;
    }
    
    SemaphoreGuard guard(taskMutex, pdMS_TO_TICKS(100));
    if (guard.hasLock()) {
        processed = commandsProcessed;
        failed = commandsFailed;
    }
}

void RelayControlTask::taskFunction(void* pvParameters) {
    LOG_INFO(TASK_TAG, "Task started - ready for commands");
    
    // IMPORTANT: Wait before first watchdog reset to ensure task is fully registered
    vTaskDelay(pdMS_TO_TICKS(200));
    taskManager.feedWatchdog();
    
    // RYN4 is already initialized in main.cpp before this task starts
    // No need to wait for initialization
    
    // Check if device is offline
    if (ryn4Device->isModuleOffline()) {
        LOG_ERROR(TASK_TAG, "RYN4 device is OFFLINE - entering monitoring mode");
        
        // Enter monitoring mode - just feed watchdog and wait
        while (running) {
            taskManager.feedWatchdog();
            vTaskDelay(pdMS_TO_TICKS(5000));  // Check every 5 seconds
            
            // Check if device came back online
            if (!ryn4Device->isModuleOffline() && ryn4Device->isInitialized()) {
                LOG_INFO(TASK_TAG, "RYN4 device back online");
                break;
            }
        }
    }
    
    RelayCommand cmd;
    
    while (running) {
        // Feed watchdog at start of loop
        taskManager.feedWatchdog();
        
        // Update rate limiting counters periodically
        updateRateLimitCounters();
        
        // Wait for command with timeout
        BaseType_t queueResult = xQueueReceive(
            commandQueue, 
            &cmd, 
            pdMS_TO_TICKS(RELAY_CONTROL_TASK_INTERVAL_MS)
        );
        
        if (queueResult == pdTRUE) {
            if (!running) {
                break;
            }
            
            // Check command age
            TickType_t commandAge = xTaskGetTickCount() - cmd.timestamp;
            if (commandAge > pdMS_TO_TICKS(1000)) {
                LOG_WARN(TASK_TAG, "Aged command: %lums", pdTICKS_TO_MS(commandAge));
            }
            
            // Set busy flag and processing bit
            busy = true;
            
            // Set processing bit in command event group
            if (commandEventGroup) {
                xEventGroupSetBits(commandEventGroup, COMMAND_PROCESSING_BIT);
            }
            
            bool success = processCommand(cmd);
            
            // Clear busy flag
            busy = false;
            
            // Update command event group with result
            if (commandEventGroup) {
                xEventGroupClearBits(commandEventGroup, COMMAND_PROCESSING_BIT);
                if (success) {
                    xEventGroupSetBits(commandEventGroup, COMMAND_SUCCESS_BIT);
                } else {
                    xEventGroupSetBits(commandEventGroup, COMMAND_ERROR_BIT);
                }
            }
            
            if (success) {
                // Command sent successfully
                commandsProcessed++;
                lastCommandTime = xTaskGetTickCount();
                
                // Notify status task
                xTaskNotifyGive(RelayStatusTask::getTaskHandle());

            } else {
                commandsFailed++;
                LOG_ERROR(TASK_TAG, "Command failed - Type: %d", 
                         static_cast<int>(cmd.type));
            }
            
            // Enforce minimum interval between commands
            vTaskDelay(pdMS_TO_TICKS(RYN4_INTER_COMMAND_DELAY_MS));
        }
        
        // Feed watchdog before next iteration
        taskManager.feedWatchdog();
    }
    
    LOG_INFO(TASK_TAG, "Task ending - Processed: %lu, Failed: %lu",
             commandsProcessed, commandsFailed);
    
    // Clean up
    taskHandle = nullptr;
    vTaskDelete(nullptr);
}

bool RelayControlTask::processCommand(const RelayCommand& cmd) {
    #if defined(LOG_MODE_DEBUG_FULL)
    LOG_DEBUG(TASK_TAG, "Processing command type: %d", static_cast<int>(cmd.type));
    #endif
    
    switch (cmd.type) {
        case CommandType::SET_SINGLE:
            return processSingleRelay(cmd.relayIndex, cmd.state);
            
        case CommandType::TOGGLE:
            return processToggleRelay(cmd.relayIndex);
            
        case CommandType::SET_ALL:
            return processSetAllRelays(cmd.state);
            
        case CommandType::SET_MULTIPLE: {
            // Convert array to vector
            std::vector<bool> statesVector;
            statesVector.reserve(cmd.stateCount);
            for (uint8_t i = 0; i < cmd.stateCount; i++) {
                statesVector.push_back(cmd.states[i]);
            }
            return processSetMultipleRelays(statesVector);
        }
            
        case CommandType::TOGGLE_ALL:
            return processToggleAllRelays();
            
        default:
            LOG_ERROR(TASK_TAG, "Unknown cmd: %d", static_cast<int>(cmd.type));
            return false;
    }
}

bool RelayControlTask::processSingleRelay(uint8_t relayIndex, bool state) {
    // Check if device is offline
    if (ryn4Device->isModuleOffline()) {
        LOG_ERROR(TASK_TAG, "Cannot control relay - device is offline");
        return false;
    }
    
    #ifdef ENABLE_RELAY_SAFETY_CHECKS
    if (!checkRateLimit(relayIndex)) {
        LOG_WARN(TASK_TAG, "Rate limit R%d", relayIndex);
        return false;
    }
    #endif
    
    LOG_INFO(TASK_TAG, "R%d -> %s", relayIndex, state ? "ON" : "OFF");
    
    // Get the update event group from RYN4
    EventGroupHandle_t updateEventGroup = ryn4Device->getUpdateEventGroup();
    if (!updateEventGroup) {
        LOG_ERROR(TASK_TAG, "Failed to get update event group");
        return false;
    }
    
    // Clear the specific relay update bit before sending command
    if (relayIndex >= 1 && relayIndex <= 8) {
        uint32_t relayUpdateBit = ryn4::RELAY_UPDATE_BITS[relayIndex - 1];
        xEventGroupClearBits(updateEventGroup, relayUpdateBit);
        
        #if defined(LOG_MODE_DEBUG_FULL)
        LOG_DEBUG(TASK_TAG, "Cleared update bit 0x%08X for relay %d", 
                 relayUpdateBit, relayIndex);
        #endif
    }
    
    SemaphoreGuard guard(ryn4Device->getMutexInterface(), pdMS_TO_TICKS(1000));
    if (!guard.hasLock()) {
        LOG_ERROR(TASK_TAG, "Failed to acquire device mutex");
        return false;
    }
    
    ryn4::RelayAction action = state ? ryn4::RelayAction::OPEN : ryn4::RelayAction::CLOSE;
    ryn4::RelayErrorCode result = ryn4Device->controlRelay(relayIndex, action);
    
    if (result == ryn4::RelayErrorCode::SUCCESS) {
        #ifdef ENABLE_RELAY_EVENT_LOGGING
        LOG_INFO(LOG_TAG_RELAY, "[CMD] R%d: %s", relayIndex, state ? "OPEN" : "CLOSE");
        #endif
        
        // Wait for the specific relay update bit to be set
        if (relayIndex >= 1 && relayIndex <= 8) {
            uint32_t relayUpdateBit = ryn4::RELAY_UPDATE_BITS[relayIndex - 1];
            
            EventBits_t bits = xEventGroupWaitBits(
                updateEventGroup,
                relayUpdateBit,
                pdTRUE,    // Clear on exit
                pdFALSE,   // Wait for any
                pdMS_TO_TICKS(300)  // 300ms timeout (reduced from 500ms)
            );
            
            if (bits & relayUpdateBit) {
                LOG_DEBUG(TASK_TAG, "Relay %d update confirmed (bit 0x%08X)", 
                         relayIndex, relayUpdateBit);
                return true;
            } else {
                // Don't immediately fail - the relay might have changed but we missed the notification
                LOG_DEBUG(TASK_TAG, "Update bit timeout for relay %d, checking actual state", 
                         relayIndex);
                
                // Give it a moment more
                vTaskDelay(pdMS_TO_TICKS(50));
                
                // Check the actual relay state
                IDeviceInstance::DeviceResult<std::vector<float>> checkResult = ryn4Device->getData(
                    IDeviceInstance::DeviceDataType::RELAY_STATE);
                    
                if (checkResult.isOk() && checkResult.value.size() >= relayIndex) {
                    bool actualState = checkResult.value[relayIndex - 1] > 0.5f;
                    if (actualState == state) {
                        LOG_INFO(TASK_TAG, "R%d confirmed in correct state despite timeout", 
                                relayIndex);
                        return true;
                    } else {
                        LOG_ERROR(TASK_TAG, "R%d state mismatch: expected %s, actual %s",
                                 relayIndex, state ? "ON" : "OFF", 
                                 actualState ? "ON" : "OFF");
                        return false;
                    }
                }
                
                LOG_WARN(TASK_TAG, "Could not verify relay %d state", relayIndex);
                return false;
            }
        }
        
        return true;
    } else {
        LOG_ERROR(TASK_TAG, "R%d fail: %d", relayIndex, static_cast<int>(result));
        return false;
    }
}

bool RelayControlTask::processToggleRelay(uint8_t relayIndex) {
    // Get current state
    IDeviceInstance::DeviceResult<std::vector<float>> result = ryn4Device->getData(
        IDeviceInstance::DeviceDataType::RELAY_STATE);
    
    if (!result.isOk() || result.value.size() < relayIndex) {
        LOG_ERROR(TASK_TAG, "Failed to get current state");
        return false;
    }
    
    bool currentState = result.value[relayIndex - 1] > 0.5f;
    // Use the updated processSingleRelay which now handles confirmation
    return processSingleRelay(relayIndex, !currentState);
}

bool RelayControlTask::processSetAllRelays(bool state) {
    LOG_INFO(TASK_TAG, "All -> %s", state ? "ON" : "OFF");

    bool states[RYN4_NUM_RELAYS];
    for (int i = 0; i < RYN4_NUM_RELAYS; i++) {
        states[i] = state;
    }
    
    // Create vector on stack
    std::vector<bool> stateVector(states, states + RYN4_NUM_RELAYS);
    return processSetMultipleRelays(stateVector);
}

bool RelayControlTask::processSetMultipleRelays(const std::vector<bool>& states) {
    LOG_INFO(TASK_TAG, "Multi-relay set");
    
    // Get the update event group
    EventGroupHandle_t updateEventGroup = ryn4Device->getUpdateEventGroup();
    if (!updateEventGroup) {
        LOG_ERROR(TASK_TAG, "Failed to get update event group");
        return false;
    }
    
    // Use the global constant instead of building it
    extern EventBits_t relayAllUpdateBits;  // Defined in RYN4.cpp
    
    // Clear all relay update bits before sending command
    xEventGroupClearBits(updateEventGroup, relayAllUpdateBits);
    
    // Get current states to determine which relays will actually change
    EventBits_t expectedChangeBits = 0;
    IDeviceInstance::DeviceResult<std::vector<float>> currentStates = ryn4Device->getData(
        IDeviceInstance::DeviceDataType::RELAY_STATE);
    
    if (currentStates.isOk() && currentStates.value.size() == RYN4_NUM_RELAYS) {
        // Build mask of only relays that will change
        for (size_t i = 0; i < states.size() && i < RYN4_NUM_RELAYS; i++) {
            bool currentState = currentStates.value[i] > 0.5f;
            if (currentState != states[i]) {
                expectedChangeBits |= ryn4::RELAY_UPDATE_BITS[i];
                #if defined(LOG_MODE_DEBUG_FULL)
                LOG_DEBUG(TASK_TAG, "Relay %zu will change: %s -> %s", 
                         i + 1, currentState ? "ON" : "OFF", states[i] ? "ON" : "OFF");
                #endif
            }
        }
        
        // If no changes expected, we're done
        if (expectedChangeBits == 0) {
            LOG_INFO(TASK_TAG, "No relay state changes needed");
            return true;
        }
    } else {
        // If we can't get current state, expect all relays might update
        expectedChangeBits = relayAllUpdateBits;
        LOG_DEBUG(TASK_TAG, "Could not get current states, expecting all relays to update");
    }
    
    SemaphoreGuard guard(ryn4Device->getMutexInterface(), pdMS_TO_TICKS(1000));
    if (!guard.hasLock()) {
        LOG_ERROR(TASK_TAG, "Failed to acquire device mutex");
        return false;
    }
    
    ryn4::RelayErrorCode result = ryn4Device->setMultipleRelayStates(states);
    
    if (result == ryn4::RelayErrorCode::SUCCESS) {
        #ifdef ENABLE_RELAY_EVENT_LOGGING
        // Use static buffer for logging to avoid stack allocation
        int offset = snprintf(logBuffer, sizeof(logBuffer), "[CMD] Multi: ");
        for (size_t i = 0; i < states.size() && offset < sizeof(logBuffer) - 10; i++) {
            offset += snprintf(logBuffer + offset, sizeof(logBuffer) - offset,
                             "R%zu:%s ", i + 1, states[i] ? "ON" : "OFF");
        }
        LOG_INFO(LOG_TAG_RELAY, "%s", logBuffer);
        #endif
        
        // For multi-register writes, the RYN4 might just send an ACK
        // Wait for the acknowledgment with shorter timeout
        EventBits_t bits = xEventGroupWaitBits(
            updateEventGroup,
            expectedChangeBits,     // Only wait for relays we expect to change
            pdFALSE,               // Don't clear on exit (we'll do it manually)
            pdFALSE,               // Wait for any bit (not all)
            pdMS_TO_TICKS(300)     // Reduced timeout - ACK should be fast
        );
        
        if (bits & expectedChangeBits) {
            // We got at least some confirmations
            LOG_DEBUG(TASK_TAG, "Multi-relay update confirmed (bits: 0x%08X of expected 0x%08X)", 
                     bits, expectedChangeBits);
            
            // Clear the bits we received
            xEventGroupClearBits(updateEventGroup, bits & relayAllUpdateBits);
            
            // Check if all expected changes were confirmed
            if ((bits & expectedChangeBits) == expectedChangeBits) {
                return true;
            } else {
                LOG_WARN(TASK_TAG, "Partial confirmation: got 0x%08X, expected 0x%08X",
                        bits & expectedChangeBits, expectedChangeBits);
                // Still return true as some relays updated
                return true;
            }
        } else {
            LOG_WARN(TASK_TAG, "Timeout waiting for multi-relay update confirmation");
            
            // Verify actual states after timeout
            vTaskDelay(pdMS_TO_TICKS(100));  // Give device time to process
            
            IDeviceInstance::DeviceResult<std::vector<float>> verifyResult = ryn4Device->getData(
                IDeviceInstance::DeviceDataType::RELAY_STATE);
                
            if (verifyResult.isOk() && verifyResult.value.size() == states.size()) {
                bool allMatch = true;
                for (size_t i = 0; i < states.size(); i++) {
                    bool actualState = verifyResult.value[i] > 0.5f;
                    if (actualState != states[i]) {
                        allMatch = false;
                        LOG_ERROR(TASK_TAG, "R%zu state mismatch after multi-set: expected %s, actual %s",
                                 i + 1, states[i] ? "ON" : "OFF", actualState ? "ON" : "OFF");
                    }
                }
                
                if (allMatch) {
                    LOG_INFO(TASK_TAG, "All relay states verified correct despite timeout");
                    return true;
                }
            }
            
            return false;
        }
    } else {
        LOG_ERROR(TASK_TAG, "Multi fail: %d", static_cast<int>(result));
        return false;
    }
}

bool RelayControlTask::processToggleAllRelays() {
    // Get current states
    IDeviceInstance::DeviceResult<std::vector<float>> result = ryn4Device->getData(
        IDeviceInstance::DeviceDataType::RELAY_STATE);
    
    if (!result.isOk() || result.value.size() != RYN4_NUM_RELAYS) {
        LOG_ERROR(TASK_TAG, "Failed to get current states");
        return false;
    }
    
    // Create inverted states vector
    std::vector<bool> newStates;  // Stack allocated
    newStates.reserve(RYN4_NUM_RELAYS);
    
    for (const auto& value : result.value) {
        newStates.push_back(!(value > 0.5f));
    }
    
    return processSetMultipleRelays(newStates);
}

bool RelayControlTask::checkRateLimit(uint8_t relayIndex) {
    if (relayIndex < 1 || relayIndex > RYN4_NUM_RELAYS) {
        return false;
    }
    
    uint8_t idx = relayIndex - 1;
    TickType_t now = xTaskGetTickCount();
    
    // Check minimum interval
    if (toggleTimestamps[idx] != 0) {
        TickType_t elapsed = now - toggleTimestamps[idx];
        if (elapsed < pdMS_TO_TICKS(MIN_RELAY_SWITCH_INTERVAL_MS)) {
            return false;
        }
    }
    
    // Check rate limit
    if (toggleCount[idx] >= MAX_RELAY_TOGGLE_RATE_PER_MIN) {
        return false;
    }
    
    // Update tracking
    toggleTimestamps[idx] = now;
    toggleCount[idx]++;
    
    return true;
}

void RelayControlTask::updateRateLimitCounters() {
    TickType_t now = xTaskGetTickCount();
    TickType_t elapsed = now - rateWindowStart;
    
    // Reset counters every minute
    if (elapsed >= pdMS_TO_TICKS(60000)) {
        memset(toggleCount, 0, sizeof(toggleCount));
        rateWindowStart = now;
        #if defined(LOG_MODE_DEBUG_FULL)
        LOG_DEBUG(TASK_TAG, "Rate limit counters reset");
        #endif
    }
}

bool RelayControlTask::queueCommand(const RelayCommand& cmd) {
    if (!commandQueue) {
        LOG_ERROR(TASK_TAG, "Queue not initialized");
        return false;
    }
    
    // Check queue space
    UBaseType_t spacesAvailable = uxQueueSpacesAvailable(commandQueue);
    if (spacesAvailable == 0) {
        LOG_ERROR(TASK_TAG, "Queue full - command rejected");
        return false;
    }
    
    if (xQueueSend(commandQueue, &cmd, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_ERROR(TASK_TAG, "Failed to queue command");
        return false;
    }
    
    #if defined(LOG_MODE_DEBUG_FULL) || defined(LOG_MODE_DEBUG_SELECTIVE)
    LOG_DEBUG(TASK_TAG, "Command queued (spaces left: %d)", spacesAvailable - 1);
    #endif
    
    // Optional: Notify the task immediately for faster response
    if (taskHandle) {
        xTaskNotifyGive(taskHandle);
    }
    
    return true;
}
