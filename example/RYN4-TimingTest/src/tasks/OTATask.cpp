// OTATask.cpp
#include "LoggingMacros.h"
#include "../tasks/OTATask.h"

#include <SemaphoreGuard.h>
#include <TaskManager.h>
#include <esp_task_wdt.h>

extern TaskManager taskManager;

// Initialize static members
TaskHandle_t OTATask::taskHandle = nullptr;
bool OTATask::otaUpdateInProgress = false;
SemaphoreHandle_t OTATask::otaStatusMutex = nullptr;

bool OTATask::init() {
    LOG_INFO(LOG_TAG_OTA, "Initializing OTA task");

    // Create mutex for thread-safe access to OTA status
    otaStatusMutex = xSemaphoreCreateMutex();
    if (!otaStatusMutex) {
        LOG_ERROR(LOG_TAG_OTA, "Failed to create OTA status mutex");
        return false;
    }

    // Initialize OTA Manager with custom callbacks
    OTAManager::initialize(DEVICE_HOSTNAME,    // Use the project hostname
                           OTA_PASSWORD,       // OTA password from config
                           OTA_PORT,           // OTA port from config
                           isNetworkConnected  // Network check callback
    );

    // Set custom callbacks for OTA events
    OTAManager::setStartCallback(onOTAStart);
    OTAManager::setEndCallback(onOTAEnd);
    OTAManager::setProgressCallback(onOTAProgress);
    OTAManager::setErrorCallback(onOTAError);

    LOG_INFO(LOG_TAG_OTA, "OTA task initialized successfully");
    return true;
}

bool OTATask::start() {
    static bool started = false;
    if (started) {
        LOG_WARN(LOG_TAG_OTA, "OTA task already started");
        return true;
    }

    LOG_INFO(LOG_TAG_OTA, "Starting OTA task");

    // Create the task using TaskManager with watchdog config
    extern TaskManager taskManager;
    TaskManager::WatchdogConfig wdtConfig = TaskManager::WatchdogConfig::enabled(false, OTA_TASK_WATCHDOG_TIMEOUT_MS);
    
    if (!taskManager.startTask(taskFunction, "OTATask", STACK_SIZE_OTA_TASK, 
                              nullptr, PRIORITY_OTA_TASK, wdtConfig)) {
        LOG_ERROR(LOG_TAG_OTA, "Failed to create OTA task");
        return false;
    }

    // Retrieve the task handle after creation
    taskHandle = taskManager.getTaskHandleByName("OTATask");
    started = true;
    LOG_INFO(LOG_TAG_OTA, "OTA task started successfully");
    return true;
}

// OTATask::taskFunction
void OTATask::taskFunction(void* pvParameters) {
    LOG_DEBUG(LOG_TAG_OTA, "OTA task running");

    // Delay for task startup sync
    vTaskDelay(pdMS_TO_TICKS(200));

    // NO NEED TO REGISTER WITH WATCHDOG - Already done in start()

    // Task loop
    for (;;) {
        // Feed watchdog at start of loop
        taskManager.feedWatchdog();

        // Handle OTA updates if network is connected
        if (isNetworkConnected()) {
            OTAManager::handleUpdates();

            // Check if OTA is in progress for LED status
            bool updateInProgress = false;
            {
                SemaphoreGuard guard(otaStatusMutex);
                updateInProgress = otaUpdateInProgress;
            }

            if (!updateInProgress) {
                // StatusLed::setBlink(500);  // 0.5s blink
            }
        } else {
            // Network not connected, flash LED in a pattern to indicate
            bool updateInProgress = false;
            {
                SemaphoreGuard guard(otaStatusMutex);
                updateInProgress = otaUpdateInProgress;
            }

            if (!updateInProgress) {
                // StatusLed::setPattern(3, 100, 2000);
            }
        }

        // Short delay with watchdog feed
        vTaskDelay(pdMS_TO_TICKS(OTA_TASK_INTERVAL_MS));
        taskManager.feedWatchdog();
    }
}

bool OTATask::isNetworkConnected() {
    // Use EthernetManager to check network connection
    return EthernetManager::isConnected();
}

void OTATask::onOTAStart() {
    LOG_INFO(LOG_TAG_OTA, "OTA update starting");

    {
        SemaphoreGuard guard(otaStatusMutex);
        otaUpdateInProgress = true;
    }

    // Set LED to fast blink during update
    // StatusLed::setBlink(100);

    // Notify the sensor task to suspend operations during update
    // SensorTask::suspend();
}

void OTATask::onOTAEnd() {
    LOG_INFO(LOG_TAG_OTA, "OTA update complete, rebooting in 1 second");

    {
        SemaphoreGuard guard(otaStatusMutex);
        otaUpdateInProgress = false;
    }

    // Set LED to solid on to indicate completion
    // StatusLed::setOn();

    // Allow time for log message to be sent
    delay(1000);

    // Restart the device to apply the update
    ESP.restart();
}

void OTATask::onOTAProgress(unsigned int progress, unsigned int total) {
    static int lastPercent = 0;
    int percent = (progress * 100) / total;

    // Log progress every 10%
    if (percent >= lastPercent + 10 || percent == 100) {
        LOG_INFO(LOG_TAG_OTA, "OTA update progress: %u%%", percent);
        lastPercent = percent - (percent % 10);
    }

    // Feed the watchdog during OTA update
    taskManager.feedWatchdog();
}

void OTATask::onOTAError(ota_error_t error) {
    const char* errorMsg = "Unknown Error";

    switch (error) {
        case OTA_AUTH_ERROR:
            errorMsg = "Authentication Failed";
            break;
        case OTA_BEGIN_ERROR:
            errorMsg = "Begin Failed";
            break;
        case OTA_CONNECT_ERROR:
            errorMsg = "Connection Failed";
            break;
        case OTA_RECEIVE_ERROR:
            errorMsg = "Receive Failed";
            break;
        case OTA_END_ERROR:
            errorMsg = "End Failed";
            break;
    }

    LOG_ERROR(LOG_TAG_OTA, "OTA Error[%u]: %s", error, errorMsg);

    // Reset update state
    {
        SemaphoreGuard guard(otaStatusMutex);
        otaUpdateInProgress = false;
    }

    // Resume any suspended operations
    // SensorTask::resume();

    // Indicate error with LED pattern - 5 quick blinks
    // StatusLed::setPattern(5, 100, 1500);
}