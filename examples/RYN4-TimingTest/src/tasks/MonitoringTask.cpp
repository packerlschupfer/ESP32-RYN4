// tasks/MonitoringTask.cpp
#include "LoggingMacros.h"
#include "MonitoringTask.h"
#include <stdlib.h>  // For abs() function
#include <SemaphoreGuard.h>
#include <TaskManager.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include "config/ProjectConfig.h"
#include "RYN4.h"
#include "RelayControlTask.h"
#include "RelayStatusTask.h"
#include "OTATask.h"

extern TaskManager taskManager;
extern RYN4* relayController;  // Global relay controller instance

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

// Initialize static members
TaskHandle_t MonitoringTask::taskHandle = nullptr;

// Static buffer for log messages to avoid stack allocation
static char logBuffer[256];

bool MonitoringTask::init() {
    LOG_INFO(LOG_TAG_MONITORING, "Initializing monitoring task");

    // Add any initialization code here if needed

    LOG_INFO(LOG_TAG_MONITORING, "Monitoring task initialized successfully");
    return true;
}

bool MonitoringTask::start() {
    LOG_INFO(LOG_TAG_MONITORING, "Starting monitoring task");

    // Create the FreeRTOS task with watchdog config
    TaskManager::WatchdogConfig wdtConfig = TaskManager::WatchdogConfig::enabled(
        true, MONITORING_TASK_WATCHDOG_TIMEOUT_MS);
    
    if (!taskManager.startTask(taskFunction, "MonitoringTask", STACK_SIZE_MONITORING_TASK, 
                              nullptr, PRIORITY_MONITORING_TASK, wdtConfig)) {
        LOG_ERROR(LOG_TAG_MONITORING, "Failed to create monitoring task");
        return false;
    }

    // Retrieve the task handle after creation
    taskHandle = taskManager.getTaskHandleByName("MonitoringTask");
    LOG_INFO(LOG_TAG_MONITORING, "Monitoring task started with %d ms watchdog timeout", 
             MONITORING_TASK_WATCHDOG_TIMEOUT_MS);
    return true;
}

void MonitoringTask::stop() {
    if (taskHandle != nullptr) {
        vTaskDelete(taskHandle);
        taskHandle = nullptr;
        LOG_INFO(LOG_TAG_MONITORING, "Monitoring task stopped");
    }
}

bool MonitoringTask::isRunning() {
    return taskHandle != nullptr;
}

TaskHandle_t MonitoringTask::getTaskHandle() {
    return taskHandle;
}

// MonitoringTask::taskFunction
void MonitoringTask::taskFunction(void* pvParameters) {
    LOG_DEBUG(LOG_TAG_MONITORING, "Monitoring task running");

    // Wait before starting main loop to ensure task is registered with watchdog
    vTaskDelay(pdMS_TO_TICKS(2000));

    LOG_INFO(LOG_TAG_MONITORING, "Monitoring task entering main loop");

    // Task loop
    for (;;) {
        // Feed watchdog at the start of each iteration
        taskManager.feedWatchdog();

        #if defined(LOG_MODE_DEBUG_FULL)
        LOG_DEBUG(LOG_TAG_MONITORING, "Starting monitoring cycle");
        #endif

        // Log system health information
        logSystemHealth();

        // Feed watchdog after potentially long operation
        taskManager.feedWatchdog();

        // Log network status
        logNetworkStatus();

        // Feed watchdog
        taskManager.feedWatchdog();

        // Log RYN4 relay module status
        // logRelayModuleStatus();

        // Feed watchdog
        taskManager.feedWatchdog();

        // Log relay operation statistics
        // logRelayStatistics();

        // Before any long delays, feed watchdog again
        taskManager.feedWatchdog();
        
        // Handle delay with proper watchdog feeding
        const int totalDelayMs = MONITORING_TASK_INTERVAL_MS;
        
        // For delays longer than watchdog timeout, we need to feed it periodically
        // ESP32 watchdog typically has a 5-second timeout, so feed every 2 seconds for safety
        const int maxDelayWithoutFeed = 2000;  // 2 seconds
        
        if (totalDelayMs <= maxDelayWithoutFeed) {
            // Short delay - can do it in one go
            vTaskDelay(pdMS_TO_TICKS(totalDelayMs));
            taskManager.feedWatchdog();
        } else {
            // Long delay - need to break it up and feed watchdog periodically
            int remainingMs = totalDelayMs;
            
            #if defined(LOG_MODE_DEBUG_FULL) || defined(LOG_MODE_DEBUG_SELECTIVE)
            int progressLogCounter = 0;
            #endif
            
            #if defined(LOG_MODE_DEBUG_FULL)
            LOG_DEBUG(LOG_TAG_MONITORING, "Entering delay period: %d ms (feeding watchdog every %d ms)", 
                     totalDelayMs, maxDelayWithoutFeed);
            #endif
            
            while (remainingMs > 0) {
                // Calculate delay for this iteration
                int delayMs = (remainingMs > maxDelayWithoutFeed) ? maxDelayWithoutFeed : remainingMs;
                
                // Delay
                vTaskDelay(pdMS_TO_TICKS(delayMs));
                
                // Update remaining time
                remainingMs -= delayMs;
                
                // Feed watchdog after each chunk
                taskManager.feedWatchdog();
                
                // Log progress for very long delays (every 30 seconds) in debug mode
                #if defined(LOG_MODE_DEBUG_FULL) || defined(LOG_MODE_DEBUG_SELECTIVE)
                progressLogCounter += delayMs;
                if (progressLogCounter >= 30000 && remainingMs > 0) {
                    LOG_DEBUG(LOG_TAG_MONITORING, "Delay progress: %d ms remaining of %d ms total", 
                             remainingMs, totalDelayMs);
                    progressLogCounter = 0;
                }
                #endif
            }
        }

        #if defined(LOG_MODE_DEBUG_FULL)
        LOG_DEBUG(LOG_TAG_MONITORING, "Completed monitoring cycle");
        #endif
    }
}

void MonitoringTask::logSystemHealth() {
    // Get free heap memory
    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t heapSize = ESP.getHeapSize();
    
    // Calculate percentage with integer math to avoid float operations
    uint32_t heapPercent = (freeHeap * 100) / heapSize;

    // Get minimum free heap since boot
    uint32_t minFreeHeap = ESP.getMinFreeHeap();

    // Memory leak detection - improved version
    // This algorithm accounts for normal system initialization by waiting for memory
    // to stabilize before starting leak detection. During boot, tasks are created,
    // buffers allocated, and various subsystems initialized - all legitimate memory use.
    static uint32_t lastFreeHeap = 0;
    static bool firstRun = true;
    static uint32_t stabilizedHeap = 0;     // Heap after system stabilization
    static bool systemStabilized = false;   // Flag to track if system is stable
    static uint32_t stabilizationTime = 0;  // Time when system stabilized
    static uint32_t leakAccumulator = 0;   // Track total leaked memory after stabilization
    static uint32_t monitoringCycles = 0;   // Count monitoring cycles
    static uint32_t consecutiveLeaks = 0;   // Track consecutive memory losses
    
    monitoringCycles++;
    
    if (firstRun) {
        lastFreeHeap = freeHeap;
        firstRun = false;
        LOG_INFO(LOG_TAG_MONITORING, "Memory monitoring initialized - baseline heap: %lu bytes", 
                 (unsigned long)freeHeap);
    } else {
        int32_t heapDelta = (int32_t)freeHeap - (int32_t)lastFreeHeap;
        
        // System stabilization detection
        // Consider system stable after 5 monitoring cycles or 5 minutes
        if (!systemStabilized) {
            if (monitoringCycles >= 5 || millis() > 300000) {  // 5 cycles or 5 minutes
                systemStabilized = true;
                stabilizedHeap = freeHeap;
                stabilizationTime = millis();
                LOG_INFO(LOG_TAG_MONITORING, "System memory stabilized at %lu bytes after %lu ms", 
                         (unsigned long)stabilizedHeap, (unsigned long)stabilizationTime);
            } else {
                // During initialization, just track changes without reporting leaks
                if (heapDelta < -5000) {
                    LOG_DEBUG(LOG_TAG_MONITORING, "Memory allocation during init: %ld bytes", 
                             (long)(-heapDelta));
                }
            }
        } else {
            // After stabilization, monitor for real leaks
            // Only log significant changes to avoid noise
            if (heapDelta < -2000) {  // Lost more than 2KB
                // Check if this is a persistent leak or temporary allocation
                consecutiveLeaks++;
                
                // Only report as leak if we see consistent memory loss
                if (consecutiveLeaks >= 3) {
                    leakAccumulator += (uint32_t)(-heapDelta);
                    
                    // Log for significant single-cycle losses
                    if (-heapDelta > 10000) {  // More than 10KB in one go
                        LOG_WARN(LOG_TAG_MONITORING, "Large memory allocation: %ld bytes", 
                                 (long)(-heapDelta));
                        LOG_INFO(LOG_TAG_MONITORING, "Current free heap: %lu bytes (min: %lu)", 
                                (unsigned long)freeHeap, (unsigned long)minFreeHeap);
                    } else if (leakAccumulator > 20000) {  // Total loss > 20KB
                        LOG_WARN(LOG_TAG_MONITORING, "Potential memory leak: Lost %ld bytes (Total: %lu bytes)", 
                                 (long)(-heapDelta), (unsigned long)leakAccumulator);
                    }
                    
                    // Only report critical if we're actually running low on memory
                    if (freeHeap < 50000 && leakAccumulator > 30000) {
                        LOG_ERROR(LOG_TAG_MONITORING, "CRITICAL: Low memory with potential leak!");
                        LOG_ERROR(LOG_TAG_MONITORING, "Free heap: %lu bytes, Total lost: %lu bytes", 
                                 (unsigned long)freeHeap, (unsigned long)leakAccumulator);
                        
                        // Calculate leak rate only after stabilization
                        uint32_t timeSinceStable = millis() - stabilizationTime;
                        if (timeSinceStable > 60000) {  // Only after 1 minute post-stabilization
                            uint32_t leakRatePerMin = (leakAccumulator * 60000) / timeSinceStable;
                            LOG_ERROR(LOG_TAG_MONITORING, "Leak rate: %lu bytes/minute", 
                                     (unsigned long)leakRatePerMin);
                        }
                    }
                }
            } else if (heapDelta > 5000) {  // Recovered more than 5KB
                LOG_INFO(LOG_TAG_MONITORING, "Memory recovered: %ld bytes", (long)heapDelta);
                // Reduce accumulator but don't go negative
                leakAccumulator = (leakAccumulator > (uint32_t)heapDelta) ? 
                                 leakAccumulator - (uint32_t)heapDelta : 0;
                // Reset consecutive leak counter on recovery
                consecutiveLeaks = 0;
            } else {
                // No significant change - reset consecutive leak counter
                consecutiveLeaks = 0;
            }
        }
        
        lastFreeHeap = freeHeap;
    }
    
    // Check if heap is getting low
    if (freeHeap < WATCHDOG_MIN_HEAP_BYTES) {
        LOG_WARN(LOG_TAG_MONITORING, "Low heap warning: %lu bytes free (minimum: %u)", 
                 (unsigned long)freeHeap, WATCHDOG_MIN_HEAP_BYTES);
    }
    
    // Memory fragmentation check
    size_t largestFreeBlock = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    uint32_t fragmentation = 0;
    
    if (freeHeap > 0) {
        fragmentation = 100 - (largestFreeBlock * 100 / freeHeap);
    }
    
    // Get uptime
    uint32_t uptime = millis() / 1000;  // seconds
    uint32_t days = uptime / (24 * 3600);
    uptime %= (24 * 3600);
    uint32_t hours = uptime / 3600;
    uptime %= 3600;
    uint32_t minutes = uptime / 60;
    uint32_t seconds = uptime % 60;

    // Get chip info
    uint32_t chipId = ESP.getEfuseMac() & 0xFFFFFFFF;
    uint8_t chipRev = ESP.getChipRevision();
    uint32_t cpuFreq = ESP.getCpuFreqMHz();

    // Log the information with reduced formatting complexity
    LOG_INFO(LOG_TAG_MONITORING, "=== System Health Report ===");
    
    // Use snprintf with static buffer to avoid stack allocation
    // Use %lu for uint32_t on ESP32
    snprintf(logBuffer, sizeof(logBuffer), "Uptime: %lu days, %02lu:%02lu:%02lu", 
             (unsigned long)days, (unsigned long)hours, 
             (unsigned long)minutes, (unsigned long)seconds);
    LOG_INFO(LOG_TAG_MONITORING, "%s", logBuffer);
    
    snprintf(logBuffer, sizeof(logBuffer), "Free Heap: %lu bytes (%lu%%), Min: %lu bytes", 
             (unsigned long)freeHeap, (unsigned long)heapPercent, (unsigned long)minFreeHeap);
    LOG_INFO(LOG_TAG_MONITORING, "%s", logBuffer);
    
    // Add fragmentation info
    snprintf(logBuffer, sizeof(logBuffer), "Heap Fragmentation: %lu%% (Largest block: %lu bytes)", 
             (unsigned long)fragmentation, (unsigned long)largestFreeBlock);
    LOG_INFO(LOG_TAG_MONITORING, "%s", logBuffer);
    
    // Warn about fragmentation (only after system stabilization)
    if (fragmentation > 50 && systemStabilized) {
        // Only warn if fragmentation is high AND heap is getting low
        if (freeHeap < 100000) {  // Less than 100KB free
            LOG_WARN(LOG_TAG_MONITORING, "High heap fragmentation with low memory - consider restart");
        } else {
            LOG_DEBUG(LOG_TAG_MONITORING, "Heap fragmentation high but memory adequate");
        }
    }
    
    snprintf(logBuffer, sizeof(logBuffer), "Chip: ID=0x%08lX, Rev=%u, CPU=%lu MHz", 
             (unsigned long)chipId, chipRev, (unsigned long)cpuFreq);
    LOG_INFO(LOG_TAG_MONITORING, "%s", logBuffer);

    // Temperature if available (ESP32-S3 and some others)
    #ifdef CONFIG_IDF_TARGET_ESP32S3
    float temp = temperatureRead();
    if (temp != NAN) {
        // Convert to integer with 1 decimal place
        int tempInt = (int)(temp * 10.0f);
        snprintf(logBuffer, sizeof(logBuffer), "CPU Temperature: %d.%d°C", 
                 tempInt / 10, abs(tempInt % 10));
        LOG_INFO(LOG_TAG_MONITORING, "%s", logBuffer);
    }
    #endif
    
    // Stack usage monitoring for this task
    UBaseType_t stackHighWaterMark = uxTaskGetStackHighWaterMark(nullptr);
    if (stackHighWaterMark < 500) {  // Less than 2KB remaining
        LOG_WARN(LOG_TAG_MONITORING, "Low stack warning: %u words (%u bytes) remaining", 
                 stackHighWaterMark, stackHighWaterMark * 4);
    }
    
    #ifdef CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS
    // Only include task list in debug builds
    #if defined(LOG_MODE_DEBUG_FULL)
    
    // Use stack allocation instead of heap to prevent memory leak
    const size_t TASK_BUFFER_SIZE = 1536;  // Adequate for task list
    char taskStatusBuffer[TASK_BUFFER_SIZE];  // Stack allocated - auto cleanup
    
    // Clear buffer first
    memset(taskStatusBuffer, 0, TASK_BUFFER_SIZE);
    
    // Get task list
    vTaskList(taskStatusBuffer);
    
    LOG_DEBUG(LOG_TAG_MONITORING, "Task Status:");
    LOG_DEBUG(LOG_TAG_MONITORING, "Name          State  Prio  Stack   Num  Core");
    LOG_DEBUG(LOG_TAG_MONITORING, "--------------------------------------------");
    
    // Parse and log each line
    char* saveptr = nullptr;
    char* line = strtok_r(taskStatusBuffer, "\n", &saveptr);
    
    while (line != nullptr && strlen(line) > 0) {
        // Extract task name and stack info for critical tasks
        char taskName[20] = {0};
        char state;
        int prio, stack, num, core;
        
        if (sscanf(line, "%19s %c %d %d %d %d", 
                  taskName, &state, &prio, &stack, &num, &core) >= 4) {
            
            // Warn about low stack for any task
            if (stack < 500) {  // Less than 2KB
                LOG_WARN(LOG_TAG_MONITORING, "LOW STACK: %s has only %d words (%d bytes) free", 
                        taskName, stack, stack * 4);
            }
        }
        
        LOG_DEBUG(LOG_TAG_MONITORING, "%s", line);
        line = strtok_r(nullptr, "\n", &saveptr);
    }
    
    // NO NEED TO FREE - Stack allocated buffer is automatically cleaned up
    
    #endif  // LOG_MODE_DEBUG_FULL
    #endif  // CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS
    
    // Log memory pools status if using them
    #ifdef USE_MEMORY_POOLS
    LOG_INFO(LOG_TAG_MONITORING, "Memory Pool Status:");
    // Add your memory pool status logging here
    #endif
}

void MonitoringTask::logNetworkStatus() {
    if (EthernetManager::isConnected()) {
        // Log basic network status - avoid string operations on stack
        LOG_INFO(LOG_TAG_MONITORING, "Network Status: Connected");
        
        // Use static buffer for IP string formatting
        snprintf(logBuffer, sizeof(logBuffer), "  IP Address: %s", 
                 ETH.localIP().toString().c_str());
        LOG_INFO(LOG_TAG_MONITORING, "%s", logBuffer);
        
        snprintf(logBuffer, sizeof(logBuffer), "  Gateway: %s", 
                 ETH.gatewayIP().toString().c_str());
        LOG_INFO(LOG_TAG_MONITORING, "%s", logBuffer);
        
        snprintf(logBuffer, sizeof(logBuffer), "  DNS: %s", 
                 ETH.dnsIP().toString().c_str());
        LOG_INFO(LOG_TAG_MONITORING, "%s", logBuffer);
    } else {
        LOG_INFO(LOG_TAG_MONITORING, "Network Status: Disconnected");
    }
}

void MonitoringTask::logRelayModuleStatus() {
    LOG_INFO(LOG_TAG_MONITORING, "=== RYN4 Relay Module Status ===");
    
    if (relayController == nullptr) {
        LOG_ERROR(LOG_TAG_MONITORING, "Relay controller not initialized");
        return;
    }
    
    if (!relayController->isInitialized()) {
        LOG_WARN(LOG_TAG_MONITORING, "RYN4 module not initialized");
        return;
    }
    
    // Get module settings
    const ModuleSettings& settings = relayController->getModuleSettings();
    
    snprintf(logBuffer, sizeof(logBuffer), "Module Address: 0x%02X", 
             settings.rs485Address);
    LOG_INFO(LOG_TAG_MONITORING, "%s", logBuffer);
    
    // Use the getBaudRateEnum function to properly convert
    LOG_INFO(LOG_TAG_MONITORING, "Baud Rate: %s", 
             RYN4::baudRateToString(RYN4::getBaudRateEnum(settings.baudRate)).c_str());
    LOG_INFO(LOG_TAG_MONITORING, "Parity: %s", 
             RYN4::parityToString(static_cast<Parity>(settings.parity)).c_str());
    LOG_INFO(LOG_TAG_MONITORING, "Return Delay: %d ms", settings.returnDelay);
    
    // Get current relay states
    IDeviceInstance::DeviceResult<std::vector<float>> result = relayController->getData(
        IDeviceInstance::DeviceDataType::RELAY_STATE);
    
    if (result.isOk() && result.value().size() == RYN4_NUM_RELAYS) {
        static char statusBuffer[256];  // Static to avoid stack allocation each time
        int offset = snprintf(statusBuffer, sizeof(statusBuffer), "Relay States: ");
        int activeCount = 0;
        
        for (int i = 0; i < RYN4_NUM_RELAYS && offset < sizeof(statusBuffer) - 10; i++) {
            bool isOn = result.value()[i] > 0.5f;
            if (isOn) activeCount++;
            
            offset += snprintf(statusBuffer + offset, sizeof(statusBuffer) - offset,
                             "R%d:%s ", i + 1, isOn ? "ON" : "OFF");
        }
        
        LOG_INFO(LOG_TAG_MONITORING, "%s", statusBuffer);
        LOG_INFO(LOG_TAG_MONITORING, "Active Relays: %d/%d", activeCount, RYN4_NUM_RELAYS);
    } else {
        LOG_ERROR(LOG_TAG_MONITORING, "Failed to read relay states");
    }
    
    // Check module responsiveness
    if (relayController->isModuleResponsive()) {
        LOG_INFO(LOG_TAG_MONITORING, "Module Communication: OK");
    } else {
        LOG_ERROR(LOG_TAG_MONITORING, "Module Communication: FAILED");
    }
}

void MonitoringTask::logRelayStatistics() {
    LOG_INFO(LOG_TAG_MONITORING, "=== Relay Operation Statistics ===");
    
    // Check if RelayControlTask is initialized before trying to get statistics
    if (!RelayControlTask::isRunning()) {
        LOG_INFO(LOG_TAG_MONITORING, "Relay control task not yet initialized");
        return;
    }
    
    // Get control task statistics
    uint32_t commandsProcessed = 0;
    uint32_t commandsFailed = 0;
    RelayControlTask::getStatistics(commandsProcessed, commandsFailed);
    
    if (commandsProcessed > 0) {
        // Calculate success rate with integer math
        uint32_t successRate = ((commandsProcessed - commandsFailed) * 100) / commandsProcessed;
        
        snprintf(logBuffer, sizeof(logBuffer), 
                "Control Commands: %lu processed, %lu failed (%lu%% success)", 
                (unsigned long)commandsProcessed, 
                (unsigned long)commandsFailed, 
                (unsigned long)successRate);
        LOG_INFO(LOG_TAG_MONITORING, "%s", logBuffer);
    } else {
        LOG_INFO(LOG_TAG_MONITORING, "Control Commands: No commands processed yet");
    }
    
    // Get pending commands
    size_t pendingCommands = RelayControlTask::getPendingCommandCount();
    if (pendingCommands > 0) {
        LOG_WARN(LOG_TAG_MONITORING, "Pending Commands: %d in queue", pendingCommands);
    }
    
    // Check if control task is busy
    if (RelayControlTask::isBusy()) {
        LOG_INFO(LOG_TAG_MONITORING, "Control Task: Currently processing command");
    }
    
    // Get status task info
    if (RelayStatusTask::isRunning()) {
        TickType_t lastUpdate = RelayStatusTask::getLastUpdateTime();
        if (lastUpdate > 0) {
            uint32_t timeSinceUpdate = pdTICKS_TO_MS(xTaskGetTickCount() - lastUpdate);
            LOG_INFO(LOG_TAG_MONITORING, "Status Task: Last update %lu ms ago", 
                     (unsigned long)timeSinceUpdate);
        }
    } else {
        LOG_WARN(LOG_TAG_MONITORING, "Status Task: Not running");
    }
    
    // Log event group status if available
    EventGroupHandle_t eventGroup = relayController ? relayController->getEventGroup() : nullptr;
    if (eventGroup) {
        EventBits_t bits = xEventGroupGetBits(eventGroup);
        
        #if defined(LOG_MODE_DEBUG_FULL) || defined(LOG_MODE_DEBUG_SELECTIVE)
            snprintf(logBuffer, sizeof(logBuffer), "Event Bits: 0x%08lX", (unsigned long)bits);
            LOG_DEBUG(LOG_TAG_MONITORING, "%s", logBuffer);
            
            // Decode some key bits
            if (bits & BIT0) {  // Assuming BIT0 is INIT_COMPLETE
                LOG_DEBUG(LOG_TAG_MONITORING, "  - Initialization complete");
            }
            
            // Check for any error bits (assuming bits 12-19 are error bits)
            uint8_t errorBits = (bits >> 12) & 0xFF;
            if (errorBits) {
                snprintf(logBuffer, sizeof(logBuffer), "  - Error bits set: 0x%02X", errorBits);
                LOG_WARN(LOG_TAG_MONITORING, "%s", logBuffer);
            }
        #else
            (void)bits;  // Suppress warning in release mode
        #endif
    }
}


// Add this utility function to help identify memory allocation sources
void MonitoringTask::trackMemoryAllocation(const char* location, size_t size) {
    #if defined(LOG_MODE_DEBUG_FULL)
    static struct AllocationTracker {
        const char* location;
        size_t totalAllocated;
        uint32_t count;
    } trackers[20];
    static int trackerCount = 0;
    
    // Find or create tracker for this location
    int idx = -1;
    for (int i = 0; i < trackerCount; i++) {
        if (strcmp(trackers[i].location, location) == 0) {
            idx = i;
            break;
        }
    }
    
    if (idx == -1 && trackerCount < 20) {
        idx = trackerCount++;
        trackers[idx].location = location;
        trackers[idx].totalAllocated = 0;
        trackers[idx].count = 0;
    }
    
    if (idx != -1) {
        trackers[idx].totalAllocated += size;
        trackers[idx].count++;
        
        // Log if this location has allocated more than 10KB total
        if (trackers[idx].totalAllocated > 10240) {
            LOG_WARN(LOG_TAG_MONITORING, "High memory usage at %s: %lu bytes in %lu allocations",
                     location, (unsigned long)trackers[idx].totalAllocated,
                     (unsigned long)trackers[idx].count);
        }
    }
    #endif
}