// main.cpp - RYN4 Timing Test
// 
// This example measures initialization timing to identify 12-second delay
// 
// This example demonstrates the new ModbusDevice architecture where:
// 1. Global ModbusRTU instance is set via setGlobalModbusRTU() before device creation
// 2. Devices are registered in a global map for automatic response routing
// 3. RYN4 inherits from QueuedModbusDevice for hybrid sync/async operation
// 4. Synchronous reads are used during initialization, then async for normal operation
// 5. No need to pass modbusMaster to device constructors anymore
//
// Key initialization sequence:
// 1. Initialize Serial1 for RS485
// 2. Call setGlobalModbusRTU(&modbusMaster)
// 3. Setup modbusMaster callbacks (mainHandleData, handleError)
// 4. Create RYN4 instance
// 5. Register device with registerModbusDevice()
// 6. Apply custom relay mappings
// 7. Call device->initialize() and wait for completion
// 8. Enable async mode with device->enableQueuedMode() (done internally by RYN4)
//
#include <Arduino.h>
#include <stdlib.h>  // For abs() function
#include "config/ProjectConfig.h"

// Core system includes
#include <TaskManager.h>
#include <Watchdog.h>
#ifdef USE_CUSTOM_LOGGER
    #include <Logger.h>
    #include <LogInterfaceImpl.cpp>  // Include implementation, not header
    #include <ConsoleBackend.h>
#endif

// Network and OTA includes
#include <EthernetManager.h>
#include <OTAManager.h>

// Task includes
#include "tasks/MonitoringTask.h"
#include "tasks/OTATask.h"
#include "tasks/RelayControlTask.h"
#include "tasks/RelayStatusTask.h"
#include "tasks/RYN4ProcessingTask.h"

// RYN4 and Modbus includes
#include "RYN4.h"
#include "ryn4/RelayDefs.h"
#include <ModbusDevice.h>  // For ModbusRegistry
#include <memory>
#include <vector>

// FreeRTOS includes
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "utils/WatchdogHelper.h"


// =============================================================================
// THREE-TIER LOGGING CONFIGURATION
// =============================================================================
// Define only ONE of these modes in your build configuration or platformio.ini:
// - LOG_MODE_RELEASE         : Minimal output (errors and critical warnings only)
// - LOG_MODE_DEBUG_SELECTIVE : Strategic debug output for key areas
// - LOG_MODE_DEBUG_FULL      : Maximum verbosity for all components

// Set default if no mode is defined
#if !defined(LOG_MODE_RELEASE) && !defined(LOG_MODE_DEBUG_SELECTIVE) && !defined(LOG_MODE_DEBUG_FULL)
    #define LOG_MODE_RELEASE  // Default to release mode for safety
#endif

// Validate that only one mode is selected
#if (defined(LOG_MODE_RELEASE) + defined(LOG_MODE_DEBUG_SELECTIVE) + defined(LOG_MODE_DEBUG_FULL)) > 1
    #error "Only one LOG_MODE should be defined"
#endif

// Global objects
#ifdef USE_CUSTOM_LOGGER
    Logger logger(std::make_shared<ConsoleBackend>());
#endif
TaskManager taskManager;

// RYN4 globals
RYN4* relayController = nullptr;
esp32ModbusRTU modbusMaster(&Serial1);

// mainHandleData and handleError are provided by ModbusDevice.h
extern void mainHandleData(uint8_t serverAddress, esp32Modbus::FunctionCode fc,
                          uint16_t startingAddress, const uint8_t* data, size_t length);
extern void handleError(uint8_t serverAddress, esp32Modbus::Error error);

// Default relay configurations with inverse logic (hardware OFF = logical ON)
std::vector<base::BaseRelayMapping> relayConfigurations = {
    {1, nullptr, ryn4::RELAY1_OPEN_BIT, ryn4::RELAY1_CLOSE_BIT, 
     ryn4::RELAY1_STATUS_BIT, ryn4::RELAY1_UPDATE_BIT, ryn4::RELAY1_ERROR_BIT, DEFAULT_RELAY_STATE, true},
    {2, nullptr, ryn4::RELAY2_OPEN_BIT, ryn4::RELAY2_CLOSE_BIT, 
     ryn4::RELAY2_STATUS_BIT, ryn4::RELAY2_UPDATE_BIT, ryn4::RELAY2_ERROR_BIT, DEFAULT_RELAY_STATE, true},
    {3, nullptr, ryn4::RELAY3_OPEN_BIT, ryn4::RELAY3_CLOSE_BIT, 
     ryn4::RELAY3_STATUS_BIT, ryn4::RELAY3_UPDATE_BIT, ryn4::RELAY3_ERROR_BIT, DEFAULT_RELAY_STATE, true},
    {4, nullptr, ryn4::RELAY4_OPEN_BIT, ryn4::RELAY4_CLOSE_BIT, 
     ryn4::RELAY4_STATUS_BIT, ryn4::RELAY4_UPDATE_BIT, ryn4::RELAY4_ERROR_BIT, DEFAULT_RELAY_STATE, true},
    {5, nullptr, ryn4::RELAY5_OPEN_BIT, ryn4::RELAY5_CLOSE_BIT, 
     ryn4::RELAY5_STATUS_BIT, ryn4::RELAY5_UPDATE_BIT, ryn4::RELAY5_ERROR_BIT, DEFAULT_RELAY_STATE, true},
    {6, nullptr, ryn4::RELAY6_OPEN_BIT, ryn4::RELAY6_CLOSE_BIT, 
     ryn4::RELAY6_STATUS_BIT, ryn4::RELAY6_UPDATE_BIT, ryn4::RELAY6_ERROR_BIT, DEFAULT_RELAY_STATE, true},
    {7, nullptr, ryn4::RELAY7_OPEN_BIT, ryn4::RELAY7_CLOSE_BIT, 
     ryn4::RELAY7_STATUS_BIT, ryn4::RELAY7_UPDATE_BIT, ryn4::RELAY7_ERROR_BIT, DEFAULT_RELAY_STATE, true},
    {8, nullptr, ryn4::RELAY8_OPEN_BIT, ryn4::RELAY8_CLOSE_BIT, 
     ryn4::RELAY8_STATUS_BIT, ryn4::RELAY8_UPDATE_BIT, ryn4::RELAY8_ERROR_BIT, DEFAULT_RELAY_STATE, true},
};

// Function prototypes
bool setupSerial();
bool setupLogger();
bool setupEthernetAndOTA();
bool setupModbus();
void setEarlyLogLevels();
void configureTaskLogLevels();
void configureTaskLogLevelsWithTaskManager();
void printSystemInfo();
void printRelayInfo();
void suppressAllHALLogs();
void runMemoryLeakTest();
// void runRelayDiagnosticSequence();
void runRelay8DiagnosticTest();
void verifyRegisterAddressingFix();
void printAllRelayStates();

// Main setup function

void setup() {
    // VERY FIRST THING - suppress HAL logs before ANY hardware init
    suppressAllHALLogs();
    
    // Set log levels
    esp_log_level_set("*", ESP_LOG_INFO);              // Set default level for all tags
    esp_log_level_set("task_wdt", ESP_LOG_INFO);      // Show watchdog messages
    esp_log_level_set("Watchdog", ESP_LOG_INFO);      // Show watchdog activity
    esp_log_level_set("TM", ESP_LOG_INFO);           // Show TaskManager activity
    
    // Additional ESP-IDF component suppression (like boilercontroller)
    esp_log_level_set("efuse", ESP_LOG_NONE);
    esp_log_level_set("cpu_start", ESP_LOG_NONE);
    esp_log_level_set("heap_init", ESP_LOG_NONE);
    esp_log_level_set("intr_alloc", ESP_LOG_NONE);
    esp_log_level_set("spi_flash", ESP_LOG_WARN);
    
    // THEN set your early log levels
    setEarlyLogLevels();
    
    // Initialize serial communication
    if (!setupSerial()) {
        // Can't log yet, just halt
        vTaskDelete(nullptr);
        return;
    }

    // Print current logging mode
    #if defined(LOG_MODE_DEBUG_FULL)
        Serial.println("Initializing (Debug Full Mode)...");
    #elif defined(LOG_MODE_DEBUG_SELECTIVE)
        Serial.println("Initializing (Debug Selective Mode)...");
    #else
        Serial.println("Initializing (Release Mode)...");
    #endif

    // Initialize logger based on mode
    if (!setupLogger()) {
        Serial.println("FATAL: Logger initialization failed");
        vTaskDelete(nullptr);
        return;
    }

    LOG_INFO(LOG_TAG_MAIN, "System startup - Hostname: %s", DEVICE_HOSTNAME);

    // Initialize TaskManager's watchdog system before creating any tasks
    LOG_INFO(LOG_TAG_MAIN, "Initializing TaskManager watchdog system...");
    
    // Initialize both TaskManager and ESP-IDF watchdogs like Boiler Controller
    if (!taskManager.initWatchdog(30, true)) {  // 30 second timeout, panic enabled
        LOG_ERROR(LOG_TAG_MAIN, "Failed to initialize TaskManager watchdog");
    } else {
        LOG_INFO(LOG_TAG_MAIN, "TaskManager watchdog initialized successfully (30s timeout)");
    }
    
    // Also initialize ESP-IDF Task Watchdog via Watchdog class
    if (!Watchdog::quickInit(30, true)) {  // 30 second timeout, panic enabled
        LOG_ERROR(LOG_TAG_MAIN, "Failed to initialize ESP-IDF Task Watchdog");
    } else {
        LOG_INFO(LOG_TAG_MAIN, "ESP-IDF Task Watchdog initialized successfully");
    }

    // Start Ethernet ASAP (non-blocking) to begin PHY negotiation
    LOG_INFO(LOG_TAG_MAIN, "Starting Ethernet initialization");
    
    // Handle custom MAC if defined
    #ifdef ETH_MAC_ADDRESS
        uint8_t mac[] = ETH_MAC_ADDRESS;
        EthernetManager::setMacAddress(mac);
        LOG_INFO(LOG_TAG_MAIN, "Using custom MAC address: %02X:%02X:%02X:%02X:%02X:%02X", 
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    #endif
    
    // Start Ethernet asynchronously
    if (!EthernetManager::initializeAsync(DEVICE_HOSTNAME, ETH_PHY_ADDR, ETH_PHY_MDC_PIN,
                                          ETH_PHY_MDIO_PIN, ETH_PHY_POWER_PIN, ETH_CLOCK_MODE)) {
        LOG_ERROR(LOG_TAG_MAIN, "Failed to start Ethernet");
    }

    // Initialize monitoring task while Ethernet is negotiating
    if (!MonitoringTask::init()) {
        LOG_ERROR(LOG_TAG_MAIN, "Failed to initialize monitoring task");
    }

    // Start monitoring task
    if (!MonitoringTask::start()) {
        LOG_ERROR(LOG_TAG_MAIN, "Failed to start monitoring task");
    }

    // Initialize Modbus hardware early (while Ethernet negotiates)
    LOG_INFO(LOG_TAG_MAIN, "Initializing Modbus RS-485 hardware");
    Serial1.begin(MODBUS_BAUD_RATE, SERIAL_8N1, MODBUS_RX_PIN, MODBUS_TX_PIN);
    
    // Small delay for RS-485 stabilization (reduced from 1000ms)
    delay(200);

    // NOW wait for Ethernet and setup OTA
    setupEthernetAndOTA();

    // Setup Modbus and RYN4 after basic hardware is ready
    if (!setupModbus()) {
        LOG_ERROR(LOG_TAG_MAIN, "Failed to setup Modbus/RYN4 - system cannot function");
    }

    // Initialize debug task if enabled
#if defined(CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS) && TM_ENABLE_DEBUG_TASK
    // Start debug task using TaskManager API
    auto debugWdtConfig = TaskManager::WatchdogConfig::disabled();
    if (taskManager.startTask(TaskManager::debugTaskWrapper, "DebugTask", 4096,
                              &taskManager, 1, debugWdtConfig)) {
        taskManager.setResourceLogPeriod(30000);  // Log every 30 seconds
        LOG_INFO(LOG_TAG_MAIN, "Debug task started successfully");
    } else {
        LOG_WARN(LOG_TAG_MAIN, "Failed to start debug task");
    }
#endif

    // Skip watchdog registration in setup() to avoid "already subscribed" errors
    // The main loop() will handle its own watchdog registration

    // Configure task-specific log levels
    // Note: TaskManager API no longer provides configureTaskLogLevel method
    // Log levels can be set using ESP-IDF logging directly if needed

    // Log initial system state
    LOG_INFO(LOG_TAG_MAIN, "=== System Initialization Complete ===");
    LOG_INFO(LOG_TAG_MAIN, "Hostname: %s", DEVICE_HOSTNAME);
    LOG_INFO(LOG_TAG_MAIN, "RYN4 Address: 0x%02X", RYN4_ADDRESS);
    LOG_INFO(LOG_TAG_MAIN, "Number of Relays: %d", RYN4_NUM_RELAYS);
    LOG_INFO(LOG_TAG_MAIN, "Log Mode: %s", 
        #if defined(LOG_MODE_DEBUG_FULL)
            "DEBUG FULL"
        #elif defined(LOG_MODE_DEBUG_SELECTIVE)
            "DEBUG SELECTIVE"
        #else
            "RELEASE"
        #endif
    );
    
    if (EthernetManager::isConnected()) {
        LOG_INFO(LOG_TAG_MAIN, "IP Address: %s", ETH.localIP().toString().c_str());
    }
    
    // Log initial watchdog statistics
    taskManager.logWatchdogStats();
    
    // Relay test will be performed in the main loop after initialization
    // to avoid blocking setup() for too long
    LOG_INFO(LOG_TAG_MAIN, "Relay test will start after system initialization");
    
    LOG_INFO(LOG_TAG_MAIN, "=========== Setup Complete ===========");
}

// Setup Serial Communication
bool setupSerial() {
    Serial.setRxBufferSize(1024);
    Serial.setTxBufferSize(1024);
    Serial.begin(115200);

    // Print startup banner immediately
    Serial.println("\n======================================");
    Serial.println("   ESP32 RYN4 Relay Controller v1.0"   );
    Serial.println("======================================");

    unsigned long startTime = millis();
    while (!Serial && (millis() - startTime < 2000)) {
        delay(10);
    }
    
    if (!Serial) {
        return false;
    }
    
    delay(300);  // Give serial monitor time to fully connect
    return true;
}

// Setup Logger - THREE-TIER VERSION
bool setupLogger() {
    #ifdef USE_CUSTOM_LOGGER
        #if defined(LOG_MODE_DEBUG_FULL)
            logger.init(2048);               // Large buffer for extensive logging
            logger.enableLogging(true);
            logger.setLogLevel(ESP_LOG_DEBUG); // Everything
            logger.setMaxLogsPerSecond(100);  // Higher rate for debugging
        #elif defined(LOG_MODE_DEBUG_SELECTIVE)
            logger.init(1024);               // Medium buffer
            logger.enableLogging(true);
            logger.setLogLevel(ESP_LOG_INFO);  // INFO and above
            logger.setMaxLogsPerSecond(80);   // Moderate rate
        #else  // LOG_MODE_RELEASE
            logger.init(512);                // Small buffer to save memory
            logger.enableLogging(true);
            logger.setLogLevel(ESP_LOG_INFO);  // INFO and above for operational visibility
            logger.setMaxLogsPerSecond(100);   // Lower rate to prevent flooding
        #endif
    #endif
    
    // Configure per-tag log levels
    configureTaskLogLevels();
    
    LOG_INFO(LOG_TAG_MAIN, "Logger initialized - Mode: %s, Buffer: %d",
        #if defined(LOG_MODE_DEBUG_FULL)
            "DEBUG FULL", 2048
        #elif defined(LOG_MODE_DEBUG_SELECTIVE)
            "DEBUG SELECTIVE", 1024
        #else
            "RELEASE", 512
        #endif
    );
    
    return true;
}

// Setup Ethernet and OTA
bool setupEthernetAndOTA() {
    LOG_INFO(LOG_TAG_MAIN, "Waiting for Ethernet connection (timeout: %dms)...", 
             ETH_CONNECTION_TIMEOUT_MS);
    
    if (EthernetManager::waitForConnection(ETH_CONNECTION_TIMEOUT_MS)) {
        LOG_INFO(LOG_TAG_MAIN, "Ethernet connected successfully!");
        EthernetManager::logEthernetStatus();
        
        delay(1000);  // Let network stabilize

        // Initialize OTA only after network is connected
        LOG_INFO(LOG_TAG_MAIN, "Initializing OTA subsystem");
        if (!OTATask::init()) {
            LOG_ERROR(LOG_TAG_MAIN, "Failed to initialize OTA task");
            return false;
        }

        // Start OTA task
        if (!OTATask::start()) {
            LOG_ERROR(LOG_TAG_MAIN, "Failed to start OTA task");
            return false;
        }

        LOG_INFO(LOG_TAG_MAIN, "OTA service started on port %d", OTA_PORT);
        return true;
    } else {
        LOG_WARN(LOG_TAG_MAIN, "Ethernet connection timeout - continuing without network");
        LOG_WARN(LOG_TAG_MAIN, "OTA will not be available without network connection");
        return false;
    }
}

// Setup Modbus and RYN4
bool setupModbus() {
    LOG_INFO(LOG_TAG_MAIN, "========================================");
    LOG_INFO(LOG_TAG_MAIN, "setupModbus() ENTRY at %lu ms", millis());
    LOG_INFO(LOG_TAG_MAIN, "========================================");
    LOG_INFO(LOG_TAG_MAIN, "Initializing Modbus RS-485 subsystem");

    // Re-apply log levels just before Modbus initialization
    setEarlyLogLevels();
    
    // Increase Modbus timeout for offline device testing
    LOG_INFO(LOG_TAG_MAIN, "Setting Modbus timeout to 500ms for better offline handling");
    modbusMaster.setTimeOutValue(500);  // 500ms instead of default 1000ms
    
    // Serial1 already initialized in setup()
    int attempts = 0;
    while (!Serial1 && attempts++ < 200) {  
        delay(10);
    }
    
    if (!Serial1) {
        LOG_ERROR(LOG_TAG_MAIN, "Failed to initialize Serial1 for RS-485");
        return false; 
    }
    
    LOG_INFO(LOG_TAG_MAIN, "RS-485 ready, initializing Modbus master");
    
    // CRITICAL: Set the global ModbusRTU instance for ModbusDevice base class
    // This must be done BEFORE creating any ModbusDevice instances
    LOG_INFO(LOG_TAG_MAIN, "Setting global ModbusRTU instance at %lu ms...", millis());
    setGlobalModbusRTU(&modbusMaster);
    LOG_INFO(LOG_TAG_MAIN, "setGlobalModbusRTU completed");
    
    // Small delay to ensure globalModbusRTU is properly set
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Setup Modbus master with global callback for routing responses
    modbusMaster.onData([](uint8_t serverAddress, esp32Modbus::FunctionCode fc, 
                           uint16_t address, const uint8_t* data, size_t length) {
        LOG_DEBUG(LOG_TAG_MAIN, "Modbus data received - Addr: 0x%02X, FC: %d, StartAddr: 0x%04X, Len: %d", 
                 serverAddress, fc, address, length);
        
        // Use the global ModbusDevice routing function
        mainHandleData(serverAddress, fc, address, data, length);
    });
    // Note: Error handling is done through the global handleError function
    // which is called by esp32ModbusRTU when errors occur
    // The esp32ModbusRTU library now properly integrates with the watchdog system
    
    // Add error callback to see if there are communication errors
    modbusMaster.onError([](esp32Modbus::Error error) {
        LOG_ERROR(LOG_TAG_MAIN, "ModbusRTU Error: %d", static_cast<int>(error));
        handleError(0xFF, error);  // Call the global error handler
    });
    
    // Start the Modbus RTU task on core 1 (like Boiler Controller does)
    LOG_INFO(LOG_TAG_MAIN, "Starting modbusMaster.begin(1) at %lu ms...", millis());
    modbusMaster.begin(1);  // Pin to core 1
    LOG_INFO(LOG_TAG_MAIN, "modbusMaster.begin() completed at %lu ms", millis());
    
    LOG_INFO(LOG_TAG_MAIN, "Modbus master started on core 1 with callbacks registered");
    
    // Give the ModbusRTU task time to fully initialize
    delay(500);  // Increased from 50ms to match Boiler Controller

    // Create RYN4 instance (new architecture - no modbusMaster parameter needed)
    LOG_INFO(LOG_TAG_MAIN, "Creating RYN4 instance for address 0x%02X...", RYN4_ADDRESS);
    relayController = new RYN4(RYN4_ADDRESS, "RYN4");
    if (!relayController) {
        LOG_ERROR(LOG_TAG_MAIN, "Failed to allocate memory for RYN4 controller");
        return false;
    }

    LOG_INFO(LOG_TAG_MAIN, "RYN4 instance created successfully");
    
    // Registration is now handled internally by the initialize() method
    LOG_INFO(LOG_TAG_MAIN, "RYN4 device created for address 0x%02X", RYN4_ADDRESS);
    
    // Apply relay configurations before initialization
    relayController->setCustomRelayMappings(relayConfigurations);
    LOG_INFO(LOG_TAG_MAIN, "Custom relay mappings applied");
    
    // Initialize the RYN4 with detailed timing measurements
    LOG_INFO(LOG_TAG_MAIN, "========== RYN4 TIMING TEST START AT %lu ms ==========", millis());
    LOG_INFO(LOG_TAG_MAIN, "Starting RYN4 initialization timing measurements...");
    
    // Test configuration to identify delay source
    // Note: If RYN4::InitConfig doesn't compile, we'll use the default initialize()
    // and manually configure these options later
    LOG_INFO(LOG_TAG_MAIN, "Test config: Using minimal initialization (skipping relay operations)");
    
    unsigned long totalStart = millis();
    unsigned long stepStart = totalStart;
    
    // Step 1: Call initialize() - using default for now
    LOG_INFO(LOG_TAG_MAIN, "[TIMING] Calling relayController->initialize()...");
    LOG_INFO(LOG_TAG_MAIN, "[TIMING] NOTE: To minimize delays, disable resetRelaysOnInit in your code");
    auto initResult = relayController->initialize();
    unsigned long afterInit = millis();
    LOG_INFO(LOG_TAG_MAIN, "[TIMING] initialize() returned after: %lu ms", afterInit - stepStart);
    
    if (initResult.isError()) {
        LOG_ERROR(LOG_TAG_MAIN, "RYN4 initialization failed with error: %d", 
                  static_cast<int>(initResult.error));
        LOG_INFO(LOG_TAG_MAIN, "[TIMING] Total time before failure: %lu ms", millis() - totalStart);
        // Unregistration is handled internally by the destructor
        delete relayController;
        relayController = nullptr;
        return false;
    }

    // Step 2: Wait for initialization to complete
    stepStart = millis();
    LOG_INFO(LOG_TAG_MAIN, "[TIMING] Waiting for initialization to complete (timeout: %dms)...", 
             RYN4_RESPONSE_TIMEOUT_MS * 3);
    
    auto waitResult = relayController->waitForInitializationComplete(
        pdMS_TO_TICKS(RYN4_RESPONSE_TIMEOUT_MS * 3));
    
    unsigned long afterWait = millis();
    LOG_INFO(LOG_TAG_MAIN, "[TIMING] waitForInitializationComplete() took: %lu ms", afterWait - stepStart);
    
    if (waitResult.isError()) {
        LOG_ERROR(LOG_TAG_MAIN, "RYN4 initialization timeout - check connections");
        LOG_ERROR(LOG_TAG_MAIN, "Configured settings - Address: 0x%02X, Baud: %d, RX: %d, TX: %d", 
                  RYN4_ADDRESS, MODBUS_BAUD_RATE, MODBUS_RX_PIN, MODBUS_TX_PIN);
        LOG_ERROR(LOG_TAG_MAIN, "Verify device is powered on and DIP switches match these settings");
        LOG_INFO(LOG_TAG_MAIN, "[TIMING] Total time until timeout: %lu ms", millis() - totalStart);
        // In release mode, we might want to continue anyway
        #ifdef LOG_MODE_RELEASE
        LOG_WARN(LOG_TAG_MAIN, "Continuing despite initialization failure (release mode)");
        #else
        LOG_ERROR(LOG_TAG_MAIN, "System will retry initialization in background");
        #endif
    } else {
        unsigned long totalTime = millis() - totalStart;
        LOG_INFO(LOG_TAG_MAIN, "RYN4 initialization completed successfully");
        LOG_INFO(LOG_TAG_MAIN, "[TIMING] ========== TIMING SUMMARY ==========");
        LOG_INFO(LOG_TAG_MAIN, "[TIMING] initialize() call: %lu ms", afterInit - totalStart);
        LOG_INFO(LOG_TAG_MAIN, "[TIMING] waitForComplete: %lu ms", afterWait - stepStart);
        LOG_INFO(LOG_TAG_MAIN, "[TIMING] TOTAL TIME: %lu ms", totalTime);
        LOG_INFO(LOG_TAG_MAIN, "[TIMING] ====================================");
        
        // Print initial relay status
        relayController->printRelayStatus();
    }

    // Create RYN4 processing task to handle async queue
    // This must be done AFTER initialization completes
    // Add a small delay to ensure system stability
    vTaskDelay(pdMS_TO_TICKS(200));
    
    LOG_INFO(LOG_TAG_MAIN, "Creating RYN4 processing task...");
    if (!RYN4ProcessingTask::init(relayController)) {
        LOG_ERROR(LOG_TAG_MAIN, "Failed to initialize RYN4 processing task");
        return false;
    }
    
    if (!RYN4ProcessingTask::start()) {
        LOG_ERROR(LOG_TAG_MAIN, "Failed to start RYN4 processing task");
        return false;
    }
    
    LOG_INFO(LOG_TAG_MAIN, "RYN4 processing task started successfully");
    
    // Register the processing task for event notifications
    TaskHandle_t processingTaskHandle = RYN4ProcessingTask::getTaskHandle();
    if (processingTaskHandle != nullptr && relayController != nullptr) {
        relayController->setProcessingTask(processingTaskHandle);
        LOG_INFO(LOG_TAG_MAIN, "Processing task registered for event notifications");
    }
    
    // Small delay to ensure task is fully started
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Note: The RYN4 device uses a hybrid sync/async model
    // The processing task will handle async operations when available
    // For now, we'll proceed without explicitly enabling async mode
    LOG_INFO(LOG_TAG_MAIN, "RYN4 initialized - processing task will handle operations");

    // Initialize control task
    if (!RelayControlTask::init(relayController)) {
        LOG_ERROR(LOG_TAG_MAIN, "Failed to initialize relay control task");
        return false;
    }

    // Start control task
    if (!RelayControlTask::start()) {
        LOG_ERROR(LOG_TAG_MAIN, "Failed to start relay control task");
        return false;
    }

    // Initialize status task
    if (!RelayStatusTask::init(relayController)) {
        LOG_ERROR(LOG_TAG_MAIN, "Failed to initialize relay status task");
        return false;
    }

    // Start status task
    if (!RelayStatusTask::start()) {
        LOG_ERROR(LOG_TAG_MAIN, "Failed to start relay status task");
        return false;
    } else {
        // IMPORTANT: Register status task for direct event notifications
        TaskHandle_t statusTaskHandle = RelayStatusTask::getTaskHandle();
        if (statusTaskHandle != nullptr && relayController != nullptr) {
            relayController->setDataReceiverTask(statusTaskHandle);
            LOG_INFO(LOG_TAG_MAIN, "Status task registered for event notifications");
        } else {
            LOG_WARN(LOG_TAG_MAIN, "Failed to register status task for notifications");
        }
    }

    LOG_INFO(LOG_TAG_MAIN, "Modbus/RYN4 system initialized successfully");
    
    // Add debug to monitor TX activity
    LOG_INFO(LOG_TAG_MAIN, "=== SYSTEM READY - Monitor TX LED ===");
    LOG_INFO(LOG_TAG_MAIN, "TX LED should only blink on:");
    LOG_INFO(LOG_TAG_MAIN, "  - Relay commands");
    LOG_INFO(LOG_TAG_MAIN, "  - Periodic polls (every 2+ minutes)");
    LOG_INFO(LOG_TAG_MAIN, "  - Event notifications");
    
    return true;
}

void suppressAllHALLogs() {
    // Suppress ALL HAL logs
    esp_log_level_set("esp32-hal-*", ESP_LOG_NONE);
    esp_log_level_set("esp32-hal-periman", ESP_LOG_NONE);
    esp_log_level_set("esp32-hal-uart", ESP_LOG_NONE);
    esp_log_level_set("esp32-hal-cpu", ESP_LOG_NONE);
    esp_log_level_set("esp32-hal-gpio", ESP_LOG_NONE);
    esp_log_level_set("esp32-hal-i2c", ESP_LOG_NONE);
    esp_log_level_set("esp32-hal-spi", ESP_LOG_NONE);
    esp_log_level_set("esp32-hal-timer", ESP_LOG_NONE);
    esp_log_level_set("esp32-hal-ledc", ESP_LOG_NONE);
    esp_log_level_set("esp32-hal-matrix", ESP_LOG_NONE);
    
    // Also suppress other noisy components
    esp_log_level_set("system_api", ESP_LOG_NONE);
    esp_log_level_set("ArduinoOTA", ESP_LOG_ERROR);
}

// Set early ESP-IDF log levels - THREE-TIER VERSION
void setEarlyLogLevels() {
    #if defined(LOG_MODE_DEBUG_FULL)
        // =============================================================================
        // FULL DEBUG MODE - Maximum verbosity for troubleshooting
        // =============================================================================
        esp_log_level_set("*", ESP_LOG_DEBUG);  // Default everything to DEBUG
        
        // Your application components - full debug
        esp_log_level_set("loopTask", ESP_LOG_DEBUG);
        esp_log_level_set("MAIN", ESP_LOG_DEBUG);
        esp_log_level_set("ETH", ESP_LOG_DEBUG);
        esp_log_level_set("OTA", ESP_LOG_DEBUG);
        esp_log_level_set("TaskManager", ESP_LOG_DEBUG);
        esp_log_level_set("MonitoringTask", ESP_LOG_DEBUG);
        esp_log_level_set("MON", ESP_LOG_DEBUG);
        
        // RYN4 and Modbus library tags - full debug
        esp_log_level_set("RYN4", ESP_LOG_DEBUG);
        esp_log_level_set("ModbusD", ESP_LOG_DEBUG);
        esp_log_level_set("ModbusDevice", ESP_LOG_DEBUG);
        esp_log_level_set("esp32ModbusRTU", ESP_LOG_DEBUG);
        esp_log_level_set("ModbusRTU", ESP_LOG_DEBUG);
        esp_log_level_set("Modbus", ESP_LOG_DEBUG);
        
        // Additional possible Modbus log tags
        esp_log_level_set("esp32Modbus", ESP_LOG_DEBUG);
        esp_log_level_set("esp32ModbusMsg", ESP_LOG_DEBUG);
        esp_log_level_set("ModbusMsg", ESP_LOG_DEBUG);
        esp_log_level_set("ModbusMaster", ESP_LOG_DEBUG);
        esp_log_level_set("ModbusSlave", ESP_LOG_DEBUG);
        esp_log_level_set("MB_PORT", ESP_LOG_DEBUG);
        esp_log_level_set("MB_RTU", ESP_LOG_DEBUG);
        
        // Task-related tags - full debug
        esp_log_level_set("RelayControlTas", ESP_LOG_DEBUG);
        esp_log_level_set("RelayStatusTask", ESP_LOG_DEBUG);
        esp_log_level_set("RlyCtrl", ESP_LOG_DEBUG);
        esp_log_level_set("RlyStat", ESP_LOG_DEBUG);
        esp_log_level_set("OTATask", ESP_LOG_DEBUG);
        
    #elif defined(LOG_MODE_DEBUG_SELECTIVE)
        // =============================================================================
        // SELECTIVE DEBUG MODE - Strategic debugging without overwhelming output
        // =============================================================================
        esp_log_level_set("*", ESP_LOG_INFO);  // Default to INFO
        
        // Your application components - selective info
        esp_log_level_set("loopTask", ESP_LOG_INFO);
        esp_log_level_set("MAIN", ESP_LOG_INFO);
        esp_log_level_set("ETH", ESP_LOG_INFO);
        esp_log_level_set("OTA", ESP_LOG_INFO);
        esp_log_level_set("TaskManager", ESP_LOG_INFO);
        esp_log_level_set("MonitoringTask", ESP_LOG_INFO);
        esp_log_level_set("MON", ESP_LOG_INFO);
        
        // RYN4 - see operations but not protocol details
        esp_log_level_set("RYN4", ESP_LOG_INFO);
        
        // Modbus library - quieter
        esp_log_level_set("ModbusD", ESP_LOG_WARN);
        esp_log_level_set("ModbusDevice", ESP_LOG_WARN);
        esp_log_level_set("esp32ModbusRTU", ESP_LOG_WARN);
        esp_log_level_set("ModbusRTU", ESP_LOG_WARN);
        esp_log_level_set("Modbus", ESP_LOG_WARN);
        
        // Tasks - selective info
        esp_log_level_set("RelayControlTas", ESP_LOG_INFO);
        esp_log_level_set("RelayStatusTask", ESP_LOG_INFO);
        esp_log_level_set("RlyCtrl", ESP_LOG_INFO);
        esp_log_level_set("RlyStat", ESP_LOG_INFO);
        esp_log_level_set("OTATask", ESP_LOG_INFO);
        
    #else  // LOG_MODE_RELEASE (default)
        // =============================================================================
        // RELEASE MODE - Minimal output for production
        // =============================================================================
        esp_log_level_set("*", ESP_LOG_WARN);  // Default to WARN
        
        // Critical components at INFO level for basic operation visibility
        esp_log_level_set("loopTask", ESP_LOG_INFO);
        esp_log_level_set("MAIN", ESP_LOG_INFO);
        esp_log_level_set("ETH", ESP_LOG_INFO);
        esp_log_level_set("OTA", ESP_LOG_INFO);
        esp_log_level_set("TaskManager", ESP_LOG_INFO);
        esp_log_level_set("MonitoringTask", ESP_LOG_INFO);
        esp_log_level_set("MON", ESP_LOG_INFO);
        
        // RYN4 - operational info only
        esp_log_level_set("RYN4", ESP_LOG_INFO);
        
        // Modbus library - errors only
        esp_log_level_set("ModbusD", ESP_LOG_ERROR);
        esp_log_level_set("ModbusDevice", ESP_LOG_ERROR);
        esp_log_level_set("esp32ModbusRTU", ESP_LOG_ERROR);
        esp_log_level_set("ModbusRTU", ESP_LOG_ERROR);
        esp_log_level_set("Modbus", ESP_LOG_ERROR);
        
        // Task-related tags
        esp_log_level_set("RelayControlTas", ESP_LOG_INFO);
        esp_log_level_set("RelayStatusTask", ESP_LOG_INFO);
        esp_log_level_set("RlyCtrl", ESP_LOG_INFO);
        esp_log_level_set("RlyStat", ESP_LOG_INFO);
        esp_log_level_set("OTATask", ESP_LOG_WARN);
    #endif
    
    // ALWAYS suppress HAL logs unless in full debug mode
    #if !defined(LOG_MODE_DEBUG_FULL)
        esp_log_level_set("esp32-hal-periman", ESP_LOG_NONE);
        esp_log_level_set("esp32-hal-uart", ESP_LOG_NONE);
        esp_log_level_set("esp32-hal-cpu", ESP_LOG_NONE);
        esp_log_level_set("ArduinoOTA", ESP_LOG_ERROR);
    #endif
}

// Configure task log levels for custom logger - THREE-TIER VERSION
void configureTaskLogLevels() {
    #ifdef RYN4_USE_CUSTOM_LOGGER
        #if defined(LOG_MODE_DEBUG_FULL)
            // =============================================================================
            // FULL DEBUG MODE - Everything visible
            // =============================================================================
            logger.setTagLevel(LOG_TAG_MAIN, ESP_LOG_DEBUG);
            logger.setTagLevel(LOG_TAG_ETH, ESP_LOG_DEBUG);
            logger.setTagLevel(LOG_TAG_OTA, ESP_LOG_DEBUG);
            logger.setTagLevel(LOG_TAG_MONITORING, ESP_LOG_DEBUG);
            logger.setTagLevel("MON", ESP_LOG_DEBUG);
            
            // Relay and Modbus logs - full debug
            logger.setTagLevel(LOG_TAG_RELAY, ESP_LOG_DEBUG);
            logger.setTagLevel(LOG_TAG_RELAY_CONTROL, ESP_LOG_DEBUG);
            logger.setTagLevel(LOG_TAG_RELAY_STATUS, ESP_LOG_DEBUG);
            logger.setTagLevel("RlyCtrl", ESP_LOG_DEBUG);
            logger.setTagLevel("RlyStat", ESP_LOG_DEBUG);
            logger.setTagLevel("RYN4", ESP_LOG_DEBUG);
            logger.setTagLevel(LOG_TAG_MODBUS, ESP_LOG_DEBUG);
            logger.setTagLevel("ModbusD", ESP_LOG_DEBUG);
            
            // TaskManager logs
            logger.setTagLevel("TaskManager", ESP_LOG_DEBUG);
            logger.setTagLevel("DBG", ESP_LOG_DEBUG);
            logger.setTagLevel("DebugTask", ESP_LOG_DEBUG);
            
        #elif defined(LOG_MODE_DEBUG_SELECTIVE)
            // =============================================================================
            // SELECTIVE DEBUG MODE - Key information only
            // =============================================================================
            logger.setTagLevel(LOG_TAG_MAIN, ESP_LOG_INFO);
            logger.setTagLevel(LOG_TAG_ETH, ESP_LOG_INFO);
            logger.setTagLevel(LOG_TAG_OTA, ESP_LOG_INFO);
            logger.setTagLevel(LOG_TAG_MONITORING, ESP_LOG_INFO);
            logger.setTagLevel("MON", ESP_LOG_INFO);
            
            // Relay - see operations but not all details
            logger.setTagLevel(LOG_TAG_RELAY, ESP_LOG_INFO);
            logger.setTagLevel(LOG_TAG_RELAY_CONTROL, ESP_LOG_INFO);
            logger.setTagLevel(LOG_TAG_RELAY_STATUS, ESP_LOG_INFO);
            logger.setTagLevel("RlyCtrl", ESP_LOG_INFO);
            logger.setTagLevel("RlyStat", ESP_LOG_INFO);
            logger.setTagLevel("RYN4", ESP_LOG_INFO);
            
            // Modbus library - quieter
            logger.setTagLevel(LOG_TAG_MODBUS, ESP_LOG_WARN);
            logger.setTagLevel("ModbusD", ESP_LOG_WARN);
            
            // TaskManager
            logger.setTagLevel("TaskManager", ESP_LOG_INFO);
            logger.setTagLevel("DBG", ESP_LOG_WARN);
            logger.setTagLevel("DebugTask", ESP_LOG_WARN);
            
        #else  // LOG_MODE_RELEASE
            // =============================================================================
            // RELEASE MODE - Minimal output but show essential operations
            // =============================================================================
            logger.setTagLevel(LOG_TAG_MAIN, ESP_LOG_INFO);
            logger.setTagLevel(LOG_TAG_ETH, ESP_LOG_INFO);
            logger.setTagLevel(LOG_TAG_OTA, ESP_LOG_WARN);
            logger.setTagLevel(LOG_TAG_MONITORING, ESP_LOG_WARN);
            logger.setTagLevel("MON", ESP_LOG_WARN);
            
            // Relay - INFO for operational visibility
            logger.setTagLevel(LOG_TAG_RELAY, ESP_LOG_INFO);
            logger.setTagLevel(LOG_TAG_RELAY_CONTROL, ESP_LOG_INFO);
            logger.setTagLevel(LOG_TAG_RELAY_STATUS, ESP_LOG_INFO);
            logger.setTagLevel("RlyCtrl", ESP_LOG_INFO);
            logger.setTagLevel("RlyStat", ESP_LOG_INFO);
            logger.setTagLevel("RYN4", ESP_LOG_INFO);
            
            // Modbus - errors only
            logger.setTagLevel(LOG_TAG_MODBUS, ESP_LOG_ERROR);
            logger.setTagLevel("ModbusD", ESP_LOG_ERROR);
            
            // TaskManager - warnings and errors
            logger.setTagLevel("TaskManager", ESP_LOG_WARN);
            logger.setTagLevel("DBG", ESP_LOG_ERROR);
            logger.setTagLevel("DebugTask", ESP_LOG_ERROR);
        #endif
    #endif

    LOG_INFO(LOG_TAG_MAIN, "Configured task-specific log levels - Mode: %s", 
        #if defined(LOG_MODE_DEBUG_FULL)
            "DEBUG FULL"
        #elif defined(LOG_MODE_DEBUG_SELECTIVE)
            "DEBUG SELECTIVE"
        #else
            "RELEASE"
        #endif
    );
}

// Configure log levels via TaskManager after tasks are created - THREE-TIER VERSION
// Function removed - TaskManager API no longer supports configureTaskLogLevel
// void configureTaskLogLevelsWithTaskManager() {
//     // Log levels should be set using ESP-IDF logging APIs directly
// }

// // Add this function to main.cpp before the setup() function

// void runRelayDiagnosticSequence() {
//     LOG_INFO(LOG_TAG_MAIN, "=== RELAY DIAGNOSTIC SEQUENCE STARTING ===");
//     LOG_INFO(LOG_TAG_MAIN, "This test will slowly turn each relay ON then OFF");
//     LOG_INFO(LOG_TAG_MAIN, "Watch for index mismatches in the update bits");
    
//     // Make sure relay system is ready
//     if (!relayController || !relayController->isInitialized() || 
//         !RelayControlTask::isRunning()) {
//         LOG_ERROR(LOG_TAG_MAIN, "Relay system not ready for diagnostic test");
//         return;
//     }
    
//     // Wait for system to stabilize
//     LOG_INFO(LOG_TAG_MAIN, "Waiting 3 seconds for system to stabilize...");
//     vTaskDelay(pdMS_TO_TICKS(3000));
    
//     // First, ensure all relays are OFF
//     LOG_INFO(LOG_TAG_MAIN, "Step 1: Turning all relays OFF");
//     if (!RelayControlTask::setAllRelays(false)) {
//         LOG_ERROR(LOG_TAG_MAIN, "Failed to turn all relays OFF");
//         return;
//     }
//     vTaskDelay(pdMS_TO_TICKS(2000));
    
//     // Log the relay update bit constants for reference
//     LOG_INFO(LOG_TAG_MAIN, "=== RELAY UPDATE BIT MAPPING ===");
//     for (int i = 0; i < 8; i++) {
//         LOG_INFO(LOG_TAG_MAIN, "Relay %d -> Update Bit: 0x%08X (bit position %d)", 
//                  i + 1, ryn4::RELAY_UPDATE_BITS[i], __builtin_ctz(ryn4::RELAY_UPDATE_BITS[i]));
//     }
//     LOG_INFO(LOG_TAG_MAIN, "================================");
    
//     // Test each relay individually
//     for (int relay = 1; relay <= 8; relay++) {
//         LOG_INFO(LOG_TAG_MAIN, "\n--- Testing Relay %d ---", relay);
        
//         // Turn ON
//         LOG_INFO(LOG_TAG_MAIN, "Turning Relay %d ON", relay);
        
//         // Log what update bit we expect
//         uint32_t expectedBit = ryn4::RELAY_UPDATE_BITS[relay - 1];
//         LOG_INFO(LOG_TAG_MAIN, "Expected update bit: 0x%08X", expectedBit);
        
//         if (!RelayControlTask::setRelayState(relay, true)) {
//             LOG_ERROR(LOG_TAG_MAIN, "Failed to turn relay %d ON", relay);
//             continue;
//         }
        
//         // Wait for operation to complete
//         vTaskDelay(pdMS_TO_TICKS(1500));
        
//         // Get current state
//         IDeviceInstance::DataResult result = relayController->getData(
//             IDeviceInstance::DeviceDataType::RELAY_STATE);
        
//         if (result.success && result.values.size() >= relay) {
//             bool isOn = result.values[relay - 1] > 0.5f;
//             LOG_INFO(LOG_TAG_MAIN, "Relay %d state after ON command: %s", 
//                      relay, isOn ? "ON" : "OFF");
            
//             if (!isOn) {
//                 LOG_ERROR(LOG_TAG_MAIN, "ERROR: Relay %d failed to turn ON!", relay);
//             }
//         }
        
//         // Wait a bit with relay ON
//         vTaskDelay(pdMS_TO_TICKS(2000));
        
//         // Turn OFF
//         LOG_INFO(LOG_TAG_MAIN, "Turning Relay %d OFF", relay);
        
//         if (!RelayControlTask::setRelayState(relay, false)) {
//             LOG_ERROR(LOG_TAG_MAIN, "Failed to turn relay %d OFF", relay);
//             continue;
//         }
        
//         // Wait for operation to complete
//         vTaskDelay(pdMS_TO_TICKS(1500));
        
//         // Verify OFF state
//         result = relayController->getData(IDeviceInstance::DeviceDataType::RELAY_STATE);
        
//         if (result.success && result.values.size() >= relay) {
//             bool isOn = result.values[relay - 1] > 0.5f;
//             LOG_INFO(LOG_TAG_MAIN, "Relay %d state after OFF command: %s", 
//                      relay, isOn ? "ON" : "OFF");
            
//             if (isOn) {
//                 LOG_ERROR(LOG_TAG_MAIN, "ERROR: Relay %d failed to turn OFF!", relay);
//             }
//         }
        
//         // Wait between relays
//         vTaskDelay(pdMS_TO_TICKS(1000));
        
//         // Feed watchdog
//         taskManager.feedWatchdog();
//     }
    
//     LOG_INFO(LOG_TAG_MAIN, "\n=== DIAGNOSTIC SEQUENCE COMPLETE ===");
    
//     // Final state check
//     LOG_INFO(LOG_TAG_MAIN, "Final relay states:");
//     IDeviceInstance::DataResult finalResult = relayController->getData(
//         IDeviceInstance::DeviceDataType::RELAY_STATE);
    
//     if (finalResult.success && finalResult.values.size() == 8) {
//         for (int i = 0; i < 8; i++) {
//             bool isOn = finalResult.values[i] > 0.5f;
//             LOG_INFO(LOG_TAG_MAIN, "  Relay %d: %s", i + 1, isOn ? "ON" : "OFF");
//         }
//     }
    
//     LOG_INFO(LOG_TAG_MAIN, "Diagnostic test finished");
// }

// Main loop
// Global watchdog state - initialized once
static bool g_hasWatchdog = false;

void loop() {
    // Keep it simple like Boiler Controller - no complex watchdog registration in loop
    // The loopTask watchdog is handled by Arduino framework
    
    static unsigned long lastWatchdogFeed = 0;
    static bool firstLoop = true;
    
    if (firstLoop) {
        firstLoop = false;
        LOG_INFO(LOG_TAG_MAIN, "=========== Main Loop Started ===========");
        lastWatchdogFeed = millis();
        
        // Simple watchdog check - don't try to register, just feed if needed
        if (WatchdogHelper::isCurrentTaskRegistered()) {
            LOG_INFO(LOG_TAG_MAIN, "Loop task watchdog already active");
            g_hasWatchdog = true;
        } else {
            LOG_INFO(LOG_TAG_MAIN, "Loop task watchdog not active (normal for Arduino)");
            g_hasWatchdog = false;
        }
    }
    
    // Feed watchdog every second
    if (millis() - lastWatchdogFeed >= 1000) {
        // Always try to feed through TaskManager (it handles whether we're registered)
        taskManager.feedWatchdog();
        lastWatchdogFeed = millis();
    }

    // Minimal delay to yield to other tasks (like Boiler Controller)
    delay(10); // 10ms loop delay

    static unsigned long lastWatchdogStats = 0;
    static unsigned long lastSystemInfo = 0;
    static unsigned long lastRelayInfo = millis();  // Initialize to current time to prevent immediate trigger
    static unsigned long lastEventCheck = millis(); // Initialize to current time to prevent immediate trigger
    static unsigned long bootTime = millis();
    static bool printedUptime = false;
    static bool relayTestDone = false;  // Set to false to enable tests
    static unsigned long relayTestStartTime = 0;
    static bool singleRelayTestDone = false;  // Set to false to enable tests
    static unsigned long singleRelayTestStartTime = 0;

    // Configure debug intervals based on log mode
    #if defined(LOG_MODE_DEBUG_FULL)
        const unsigned long eventDebugInterval = 10000;    // 10 seconds
        const unsigned long relayDebugInterval = 30000;    // 30 seconds
        const unsigned long watchdogStatsInterval = 60000; // 1 minute
        const unsigned long systemInfoInterval = 300000;   // 5 minutes
    #elif defined(LOG_MODE_DEBUG_SELECTIVE)
        const unsigned long eventDebugInterval = 30000;    // 30 seconds
        const unsigned long relayDebugInterval = 60000;    // 1 minute
        const unsigned long watchdogStatsInterval = 120000; // 2 minutes
        const unsigned long systemInfoInterval = 300000;   // 5 minutes
    #else  // LOG_MODE_RELEASE
        const unsigned long eventDebugInterval = 0;        // Disabled
        const unsigned long relayDebugInterval = 0;        // Disabled
        const unsigned long watchdogStatsInterval = 300000; // 5 minutes
        const unsigned long systemInfoInterval = 600000;   // 10 minutes
    #endif

    // Print uptime after 1 minute
    if (!printedUptime && (millis() - bootTime > 60000)) {
        printedUptime = true;
        unsigned long uptimeSeconds = millis() / 1000;
        unsigned long hours = uptimeSeconds / 3600;
        unsigned long minutes = (uptimeSeconds % 3600) / 60;
        unsigned long seconds = uptimeSeconds % 60;
        LOG_INFO(LOG_TAG_MAIN, "System uptime: %02lu:%02lu:%02lu", hours, minutes, seconds);
    }
    
    // Periodic check for offline device
    static unsigned long lastOfflineCheck = 0;
    if (relayController && (millis() - lastOfflineCheck > 30000)) {  // Every 30 seconds
        lastOfflineCheck = millis();
        if (!relayController->isModuleResponsive()) {
            LOG_WARN(LOG_TAG_MAIN, "RYN4 device is OFFLINE - relay tests skipped");
            LOG_INFO(LOG_TAG_MAIN, "Check: Power, RS-485 connections, and address 0x%02X", RYN4_ADDRESS);
        }
    }
    
    // MINIMAL TEST: Single relay control without any status checking
    static bool minimalTestDone = false;
    static unsigned long minimalTestTime = 0;
    if (!minimalTestDone && relayController && relayController->isInitialized() && 
        (millis() - bootTime > 20000)) {  // Run after 20 seconds
        if (minimalTestTime == 0) {
            minimalTestTime = millis();
            LOG_INFO(LOG_TAG_MAIN, "=== MINIMAL MODBUS TEST - NO STATUS CHECKING ===");
            LOG_INFO(LOG_TAG_MAIN, "This test will ONLY send relay commands, no getData() calls");
            LOG_INFO(LOG_TAG_MAIN, "Watch TX LED to see baseline Modbus traffic");
            LOG_INFO(LOG_TAG_MAIN, "Waiting 5 seconds before starting...");
        }
        
        if (millis() - minimalTestTime >= 5000 && millis() - minimalTestTime < 8000) {
            LOG_INFO(LOG_TAG_MAIN, "Turning Relay 1 ON (no status check)");
            relayController->controlRelay(1, ryn4::RelayAction::CLOSE);
            minimalTestTime = millis() + 3000; // Skip to next phase
        }
        else if (millis() - minimalTestTime >= 8000 && millis() - minimalTestTime < 11000) {
            LOG_INFO(LOG_TAG_MAIN, "Turning Relay 1 OFF (no status check)");
            relayController->controlRelay(1, ryn4::RelayAction::OPEN);
            minimalTestTime = millis() + 3000; // Skip to next phase
        }
        else if (millis() - minimalTestTime >= 11000) {
            LOG_INFO(LOG_TAG_MAIN, "=== MINIMAL TEST COMPLETE ===");
            LOG_INFO(LOG_TAG_MAIN, "TX LED should have blinked only for the two relay commands");
            minimalTestDone = true;
        }
        return; // Don't run other tests while this is running
    }
    
    // Run single relay verification test after 30 seconds (only if device is online)
    // Now safe to run with event-driven processing
    if (!singleRelayTestDone && minimalTestDone && relayController && relayController->isInitialized() && 
        (millis() - bootTime > 30000) && relayController->isModuleResponsive()) {
        if (singleRelayTestStartTime == 0) {
            LOG_INFO(LOG_TAG_MAIN, "=== Single Relay Verification Test ===");
            LOG_INFO(LOG_TAG_MAIN, "Testing relay addressing and off-by-one errors");
            LOG_INFO(LOG_TAG_MAIN, "MEMORY TRACKING ENABLED - Initial heap: %lu bytes", ESP.getFreeHeap());
            singleRelayTestStartTime = millis();
        }
        
        static int singleTestStep = 0;
        static int testRelayNum = 1;  // Test relay 1, 4, and 8
        
        switch (singleTestStep) {
            case 0: // Initial delay
                if (millis() - singleRelayTestStartTime >= 1000) {
                    LOG_INFO(LOG_TAG_MAIN, "\n--- Testing Relay %d specifically ---", testRelayNum);
                    singleTestStep = 1;
                }
                break;
                
            case 1: // Turn single relay ON
                {
                    LOG_INFO(LOG_TAG_MAIN, "Turning Relay %d ON (array index %d)...", testRelayNum, testRelayNum - 1);
                    LOG_INFO(LOG_TAG_MAIN, "Before command:");
                    printAllRelayStates();  // Now safe with event-driven processing
                    
                    // Memory tracking - before control command
                    uint32_t heapBeforeControl = ESP.getFreeHeap();
                    auto result = relayController->controlRelay(testRelayNum, ryn4::RelayAction::CLOSE);
                    uint32_t heapAfterControl = ESP.getFreeHeap();
                    
                    if (result == ryn4::RelayErrorCode::SUCCESS) {
                        LOG_INFO(LOG_TAG_MAIN, "✓ Command sent successfully (CLOSE = ON)");
                    } else {
                        LOG_ERROR(LOG_TAG_MAIN, "✗ Command failed with error code: %d", (int)result);
                    }
                    
                    if (heapBeforeControl - heapAfterControl > 100) {
                        LOG_WARN(LOG_TAG_MAIN, "Memory loss in controlRelay(ON): %ld bytes",
                                 (long)(heapBeforeControl - heapAfterControl));
                    }
                    
                    // Add a small delay to ensure command is processed
                    vTaskDelay(pdMS_TO_TICKS(200)); // Reduced delay
                    
                    // Feed watchdog during test
                    if (g_hasWatchdog) {
                        taskManager.feedWatchdog();
                    }
                    
                    LOG_INFO(LOG_TAG_MAIN, "After command (immediate):");
                    printAllRelayStates();  // Now safe with event-driven processing
                    
                    // Log current heap
                    LOG_INFO(LOG_TAG_MAIN, "Current heap after relay %d ON: %lu bytes", 
                             testRelayNum, ESP.getFreeHeap());
                }
                singleTestStep = 2;
                singleRelayTestStartTime = millis();
                break;
                
            case 2: // Wait and verify ON state
                if (millis() - singleRelayTestStartTime >= 2000) {  // 2 second wait time
                    // Keep direct readAllRelayStatus() commented - use event-driven getData() instead
                    // relayController->readAllRelayStatus();  // Don't use - bypasses event system
                    
                    // Feed watchdog during test
                    if (g_hasWatchdog) {
                        taskManager.feedWatchdog();
                    }
                    
                    // Single verification read instead of continuous polling
                    LOG_INFO(LOG_TAG_MAIN, "Verifying: Only Relay %d should be ON", testRelayNum);
                    printAllRelayStates();  // Keep only this verification read
                    singleTestStep = 3;
                }
                break;
                
            case 3: // Turn relay OFF
                {
                    LOG_INFO(LOG_TAG_MAIN, "Turning Relay %d OFF...", testRelayNum);
                    
                    // Memory tracking - before control command
                    uint32_t heapBeforeControl = ESP.getFreeHeap();
                    auto result = relayController->controlRelay(testRelayNum, ryn4::RelayAction::OPEN);
                    uint32_t heapAfterControl = ESP.getFreeHeap();
                    
                    if (result == ryn4::RelayErrorCode::SUCCESS) {
                        LOG_INFO(LOG_TAG_MAIN, "✓ Command sent successfully");
                    } else {
                        LOG_ERROR(LOG_TAG_MAIN, "✗ Command failed with error code: %d", (int)result);
                    }
                    
                    if (heapBeforeControl - heapAfterControl > 100) {
                        LOG_WARN(LOG_TAG_MAIN, "Memory loss in controlRelay(OFF): %ld bytes",
                                 (long)(heapBeforeControl - heapAfterControl));
                    }
                    
                    // Add a small delay to ensure command is processed
                    vTaskDelay(pdMS_TO_TICKS(100));
                    
                    // Feed watchdog during test
                    if (g_hasWatchdog) {
                        taskManager.feedWatchdog();
                    }
                    
                    // Log current heap
                    LOG_INFO(LOG_TAG_MAIN, "Current heap after relay %d OFF: %lu bytes", 
                             testRelayNum, ESP.getFreeHeap());
                }
                singleTestStep = 4;
                singleRelayTestStartTime = millis();
                break;
                
            case 4: // Wait and move to next relay or finish
                if (millis() - singleRelayTestStartTime >= 3000) {  // Increased wait between relay tests
                    if (testRelayNum == 1) {
                        testRelayNum = 4;  // Test middle relay
                        singleTestStep = 0;
                        singleRelayTestStartTime = millis();
                    } else if (testRelayNum == 4) {
                        testRelayNum = 8;  // Test last relay
                        singleTestStep = 0;
                        singleRelayTestStartTime = millis();
                    } else {
                        LOG_INFO(LOG_TAG_MAIN, "=== Single Relay Test Complete ===");
                        LOG_INFO(LOG_TAG_MAIN, "Turning all relays OFF...");
                        
                        // Turn off all relays (CLOSE = OFF with inverted logic)
                        for (int i = 1; i <= RYN4_NUM_RELAYS; i++) {
                            relayController->controlRelay(i, ryn4::RelayAction::CLOSE);
                        }
                        
                        // Small delay to ensure commands are processed
                        vTaskDelay(pdMS_TO_TICKS(500));
                        
                        LOG_INFO(LOG_TAG_MAIN, "All relays should now be OFF\n");
                        singleRelayTestDone = true;
                        singleTestStep = 0;
                        testRelayNum = 1;
                    }
                }
                break;
        }
        return; // Don't run main test until single test is done
    }
    
    // Run full relay test after single relay test completes (only if device is online)
    // Now safe to run with event-driven processing
    static uint32_t relayTestStartHeap = 0;
    static uint32_t relayTestOperations = 0;
    // Only check if single relay test is actually done (not still running)
    if (!relayTestDone && singleRelayTestDone && relayController && 
        relayController->isInitialized() && (millis() - bootTime > 10000)) {
        // Check responsiveness only after all other conditions pass
        if (relayController->isModuleResponsive()) {
        if (relayTestStartTime == 0) {
            LOG_INFO(LOG_TAG_MAIN, "=== Starting Relay Test Sequence ===");
            LOG_INFO(LOG_TAG_MAIN, "This will click each relay ON then OFF");
            relayTestStartHeap = ESP.getFreeHeap();
            LOG_INFO(LOG_TAG_MAIN, "MEMORY TRACKING - Start heap: %lu bytes", relayTestStartHeap);
            relayTestStartTime = millis();
            relayTestOperations = 0;
        }
        
        // Run relay test in steps to avoid blocking
        static int testStep = 0;
        static int relayIndex = 1;
        
        switch (testStep) {
            case 0: // Turn relay ON
                if (relayIndex <= RYN4_NUM_RELAYS) {
                    LOG_INFO(LOG_TAG_MAIN, "Testing Relay %d...", relayIndex);
                    
                    uint32_t heapBeforeOn = ESP.getFreeHeap();
                    auto resultOn = relayController->controlRelay(relayIndex, ryn4::RelayAction::CLOSE);
                    uint32_t heapAfterOn = ESP.getFreeHeap();
                    relayTestOperations++;
                    
                    if (resultOn == ryn4::RelayErrorCode::SUCCESS) {
                        LOG_INFO(LOG_TAG_MAIN, "  Relay %d: ON ✓", relayIndex);
                    } else {
                        LOG_ERROR(LOG_TAG_MAIN, "  Relay %d: Failed to turn ON", relayIndex);
                    }
                    
                    if (heapBeforeOn - heapAfterOn > 100) {
                        LOG_WARN(LOG_TAG_MAIN, "Memory loss turning relay %d ON: %ld bytes (op #%lu)",
                                 relayIndex, (long)(heapBeforeOn - heapAfterOn), relayTestOperations);
                    }
                    
                    testStep = 1;
                    relayTestStartTime = millis(); // Reset timer for delay
                } else {
                    testStep = 3; // Move to pattern test
                }
                break;
                
            case 1: // Wait 2 seconds with relay ON
                if (millis() - relayTestStartTime >= 2000) {
                    uint32_t heapBeforeOff = ESP.getFreeHeap();
                    auto resultOff = relayController->controlRelay(relayIndex, ryn4::RelayAction::OPEN);
                    uint32_t heapAfterOff = ESP.getFreeHeap();
                    relayTestOperations++;
                    
                    if (resultOff == ryn4::RelayErrorCode::SUCCESS) {
                        LOG_INFO(LOG_TAG_MAIN, "  Relay %d: OFF ✓", relayIndex);
                    } else {
                        LOG_ERROR(LOG_TAG_MAIN, "  Relay %d: Failed to turn OFF", relayIndex);
                    }
                    
                    if (heapBeforeOff - heapAfterOff > 100) {
                        LOG_WARN(LOG_TAG_MAIN, "Memory loss turning relay %d OFF: %ld bytes (op #%lu)",
                                 relayIndex, (long)(heapBeforeOff - heapAfterOff), relayTestOperations);
                    }
                    
                    // Periodic memory report
                    if (relayIndex % 4 == 0) {
                        uint32_t currentHeap = ESP.getFreeHeap();
                        LOG_INFO(LOG_TAG_MAIN, "Memory check after relay %d: %lu bytes (lost %ld total)",
                                 relayIndex, currentHeap, (long)(relayTestStartHeap - currentHeap));
                    }
                    
                    testStep = 2;
                    relayTestStartTime = millis();
                }
                break;
                
            case 2: // Wait 1 second before next relay
                if (millis() - relayTestStartTime >= 1000) {
                    relayIndex++;
                    testStep = 0;
                }
                break;
                
            case 3: // Pattern test - even relays ON
                LOG_INFO(LOG_TAG_MAIN, "Pattern test: Even relays ON...");
                for (int i = 2; i <= RYN4_NUM_RELAYS; i += 2) {
                    relayController->controlRelay(i, ryn4::RelayAction::CLOSE);
                }
                testStep = 4;
                relayTestStartTime = millis();
                break;
                
            case 4: // Wait 3 seconds with pattern
                if (millis() - relayTestStartTime >= 3000) {
                    LOG_INFO(LOG_TAG_MAIN, "All relays OFF...");
                    
                    uint32_t heapBeforeAllOff = ESP.getFreeHeap();
                    for (int i = 1; i <= RYN4_NUM_RELAYS; i++) {
                        relayController->controlRelay(i, ryn4::RelayAction::OPEN);
                        relayTestOperations++;
                    }
                    uint32_t heapAfterAllOff = ESP.getFreeHeap();
                    
                    if (heapBeforeAllOff - heapAfterAllOff > 200) {
                        LOG_WARN(LOG_TAG_MAIN, "Memory loss turning all relays OFF: %ld bytes",
                                 (long)(heapBeforeAllOff - heapAfterAllOff));
                    }
                    
                    // Final memory report
                    uint32_t finalHeap = ESP.getFreeHeap();
                    uint32_t totalLost = relayTestStartHeap - finalHeap;
                    
                    LOG_INFO(LOG_TAG_MAIN, "=== Relay Test Complete ===");
                    LOG_INFO(LOG_TAG_MAIN, "MEMORY REPORT:");
                    LOG_INFO(LOG_TAG_MAIN, "  Operations: %lu", relayTestOperations);
                    LOG_INFO(LOG_TAG_MAIN, "  Start heap: %lu bytes", relayTestStartHeap);
                    LOG_INFO(LOG_TAG_MAIN, "  End heap: %lu bytes", finalHeap);
                    LOG_INFO(LOG_TAG_MAIN, "  Total lost: %lu bytes", totalLost);
                    if (relayTestOperations > 0) {
                        LOG_INFO(LOG_TAG_MAIN, "  Average loss per operation: %lu bytes",
                                 totalLost / relayTestOperations);
                    }
                    
                    if (totalLost > 1000) {
                        LOG_ERROR(LOG_TAG_MAIN, "MEMORY LEAK DETECTED: Lost %lu bytes during relay test!", totalLost);
                    }
                    
                    // Turn all relays OFF after memory test
                    LOG_INFO(LOG_TAG_MAIN, "Turning all relays OFF after memory test...");
                    std::vector<bool> allOff(8, false);  // 8 relays, all OFF
                    ryn4::RelayErrorCode result = relayController->setMultipleRelayStates(allOff);
                    if (result == ryn4::RelayErrorCode::SUCCESS) {
                        LOG_INFO(LOG_TAG_MAIN, "All relays turned OFF successfully");
                    } else {
                        LOG_ERROR(LOG_TAG_MAIN, "Failed to turn all relays OFF: %d", static_cast<int>(result));
                    }
                    
                    relayTestDone = true;
                    testStep = 0;
                    relayIndex = 1;
                }
                break;
        }
        }  // Close the isModuleResponsive check
    }

    // =========================================================================
    // RELAY 8 DIAGNOSTIC TEST - Run once after 30 seconds
    // =========================================================================
    static bool relay8DiagnosticRun = true;  // Set to true to disable test
    static bool registerFixVerified = true;   // Set to true to disable test
    static const unsigned long RELAY8_DIAGNOSTIC_DELAY = 30000; // 30 seconds after boot
    static const unsigned long REGISTER_FIX_VERIFY_DELAY = 10000; // 10 seconds after diagnostic
    
    // DISABLED: Automatic relay 8 diagnostic to prevent polling
    if (false && !relay8DiagnosticRun && millis() > RELAY8_DIAGNOSTIC_DELAY) {
        // Check if relay system is ready
        if (relayController && relayController->isInitialized() && 
            RelayControlTask::isRunning()) {
            relay8DiagnosticRun = true;
            LOG_INFO(LOG_TAG_MAIN, "Running relay 8 diagnostic test");
            runRelay8DiagnosticTest();
        } else if (millis() > RELAY8_DIAGNOSTIC_DELAY + 30000) {
            // If still not ready after 60 seconds total, skip the test
            relay8DiagnosticRun = true;
            LOG_WARN(LOG_TAG_MAIN, "Skipping relay 8 diagnostic - system not ready");
        }
    }
    
    // =========================================================================
    // REGISTER ADDRESSING FIX VERIFICATION - Run after diagnostic
    // =========================================================================
    // Run register addressing fix verification after diagnostic
    // Now safe to run with event-driven processing
    if (relay8DiagnosticRun && !registerFixVerified && 
        millis() > RELAY8_DIAGNOSTIC_DELAY + REGISTER_FIX_VERIFY_DELAY) {
        if (relayController && relayController->isInitialized()) {
            registerFixVerified = true;
            LOG_INFO(LOG_TAG_MAIN, "Running register addressing fix verification");
            verifyRegisterAddressingFix();
        }
    }

    // Check for event bit changes (only in debug modes) with rate limiting
    if (eventDebugInterval > 0 && relayController) {
        unsigned long now = millis();
        
        // Rate limit event checking to every 1 second minimum
        const unsigned long EVENT_CHECK_MIN_INTERVAL = 1000;
        
        if (now - lastEventCheck >= EVENT_CHECK_MIN_INTERVAL) {
            lastEventCheck = now;
            
            // Monitor both event groups for different purposes
            EventGroupHandle_t operationalEventGroup = relayController->getEventGroup();
            EventGroupHandle_t initEventGroup = relayController->getInitEventGroup();
            EventGroupHandle_t updateEventGroup = relayController->getUpdateEventGroup();
            
            // Track initialization event bits (these should be stable after init)
            static EventBits_t lastInitBits = 0;
            if (initEventGroup) {
                EventBits_t currentInitBits = xEventGroupGetBits(initEventGroup);
                
                // Log only when bits change
                if (currentInitBits != lastInitBits) {
                    #if defined(LOG_MODE_DEBUG_FULL)
                        LOG_DEBUG(LOG_TAG_MAIN, "RYN4 Init bits changed: 0x%08X -> 0x%08X", 
                                  lastInitBits, currentInitBits);
                        LOG_DEBUG(LOG_TAG_MAIN, "  Device responsive: %s, Relay config: %s",
                                  (currentInitBits & BIT0) ? "YES" : "NO",
                                  (currentInitBits & BIT1) ? "YES" : "NO");
                    #elif defined(LOG_MODE_DEBUG_SELECTIVE)
                        LOG_DEBUG(LOG_TAG_MAIN, "RYN4 Init state changed: 0x%08X", currentInitBits);
                    #endif
                    
                    lastInitBits = currentInitBits;
                }
            }
            
            // Track relay update event bits (NEW SECTION)
            static EventBits_t lastUpdateBits = 0;
            static unsigned long lastUpdateLog = 0;
            
            if (updateEventGroup) {
                EventBits_t currentUpdateBits = xEventGroupGetBits(updateEventGroup);
                
                // Check if any relay update bits changed
                if (currentUpdateBits != lastUpdateBits) {
                    #if defined(LOG_MODE_DEBUG_FULL)
                        LOG_DEBUG(LOG_TAG_MAIN, "RYN4 Update bits changed: 0x%08X -> 0x%08X", 
                                  lastUpdateBits, currentUpdateBits);
                        
                        // Check each relay's update bit individually
                        for (int i = 0; i < 8; i++) {
                            // These are the actual bit positions from RelayDefs.h
                            uint32_t relayBit = 0;
                            switch(i) {
                                case 0: relayBit = (1UL << 1UL); break;   // RELAY1_UPDATE_BIT
                                case 1: relayBit = (1UL << 4UL); break;   // RELAY2_UPDATE_BIT
                                case 2: relayBit = (1UL << 7UL); break;   // RELAY3_UPDATE_BIT
                                case 3: relayBit = (1UL << 10UL); break;  // RELAY4_UPDATE_BIT
                                case 4: relayBit = (1UL << 13UL); break;  // RELAY5_UPDATE_BIT
                                case 5: relayBit = (1UL << 16UL); break;  // RELAY6_UPDATE_BIT
                                case 6: relayBit = (1UL << 19UL); break;  // RELAY7_UPDATE_BIT
                                case 7: relayBit = (1UL << 22UL); break;  // RELAY8_UPDATE_BIT
                            }
                            
                            bool wasSet = (lastUpdateBits & relayBit) != 0;
                            bool isSet = (currentUpdateBits & relayBit) != 0;
                            
                            if (wasSet != isSet) {
                                LOG_DEBUG(LOG_TAG_MAIN, "  Relay %d update bit %s (bit %d = 0x%08X)", 
                                         i + 1, 
                                         isSet ? "SET" : "CLEARED",
                                         __builtin_ctz(relayBit), // Get bit position
                                         relayBit);
                                
                                // Special attention to relay 8
                                if (i == 7) {
                                    LOG_DEBUG(LOG_TAG_MAIN, "  >>> RELAY 8 UPDATE BIT CHANGED <<<");
                                }
                            }
                        }
                    #elif defined(LOG_MODE_DEBUG_SELECTIVE)
                        // Just log that update bits changed
                        LOG_DEBUG(LOG_TAG_MAIN, "RYN4 Update bits: 0x%08X", currentUpdateBits);
                    #endif
                    
                    lastUpdateBits = currentUpdateBits;
                    lastUpdateLog = now;
                } else if (now - lastUpdateLog > eventDebugInterval) {
                    // Periodic logging of update bits if any are set
                    #if defined(LOG_MODE_DEBUG_FULL)
                        if (currentUpdateBits != 0) {
                            LOG_DEBUG(LOG_TAG_MAIN, "RYN4 Update bits (periodic): 0x%08X", 
                                     currentUpdateBits);
                            
                            // Show which relays have update bits set
                            for (int i = 0; i < 8; i++) {
                                uint32_t relayBit = 0;
                                switch(i) {
                                    case 0: relayBit = (1UL << 1UL); break;
                                    case 1: relayBit = (1UL << 4UL); break;
                                    case 2: relayBit = (1UL << 7UL); break;
                                    case 3: relayBit = (1UL << 10UL); break;
                                    case 4: relayBit = (1UL << 13UL); break;
                                    case 5: relayBit = (1UL << 16UL); break;
                                    case 6: relayBit = (1UL << 19UL); break;
                                    case 7: relayBit = (1UL << 22UL); break;
                                }
                                
                                if (currentUpdateBits & relayBit) {
                                    LOG_DEBUG(LOG_TAG_MAIN, "  Relay %d update bit is SET", i + 1);
                                }
                            }
                        }
                    #endif
                    lastUpdateLog = now;
                }
            }
            
            // Track operational event bits separately (filter out noisy bits)
            static EventBits_t lastOperationalBits = 0;
            static unsigned long lastOperationalLog = 0;
            
            if (operationalEventGroup) {
                EventBits_t currentBits = xEventGroupGetBits(operationalEventGroup);
                
                // Define noisy bits that toggle frequently
                const EventBits_t NOISY_BITS = SENSOR_UPDATE_BIT | SENSOR_ERROR_BIT; // 0x03
                
                // Check if any non-noisy bits changed
                EventBits_t significantBits = currentBits & ~NOISY_BITS;
                EventBits_t lastSignificantBits = lastOperationalBits & ~NOISY_BITS;
                
                if (significantBits != lastSignificantBits) {
                    #if defined(LOG_MODE_DEBUG_FULL)
                        LOG_DEBUG(LOG_TAG_MAIN, "RYN4 Operational bits changed: 0x%08X -> 0x%08X", 
                                  lastOperationalBits, currentBits);
                        
                        // Note: The operational event group bits 4-11 and 12-19 are NOT used
                        // for relay updates anymore - those are in the separate update event group
                        
                    #elif defined(LOG_MODE_DEBUG_SELECTIVE)
                        // Only log if errors are present
                        uint8_t errorBits = (currentBits >> 12) & 0xFF;
                        if (errorBits) {
                            LOG_DEBUG(LOG_TAG_MAIN, "RYN4 Error bits set: 0x%02X", errorBits);
                        }
                    #endif
                    
                    lastOperationalBits = currentBits;
                    lastOperationalLog = now;
                } else if (now - lastOperationalLog > eventDebugInterval) {
                    // Periodic logging of current state (excluding noisy bits)
                    #if defined(LOG_MODE_DEBUG_FULL)
                        if (significantBits != 0) {
                            LOG_DEBUG(LOG_TAG_MAIN, "RYN4 Operational state: 0x%08X (filtered)", 
                                     significantBits);
                        }
                    #endif
                    lastOperationalLog = now;
                }
            }
            
            // Summary debug output for relay command/response flow
            #if defined(LOG_MODE_DEBUG_FULL)
            static unsigned long lastFlowLog = 0;
            if (now - lastFlowLog > 10000) { // Every 10 seconds
                lastFlowLog = now;
                
                // Check if any relay control commands are in progress
                if (RelayControlTask::isBusy()) {
                    LOG_DEBUG(LOG_TAG_MAIN, "=== Relay Control Flow Status ===");
                    LOG_DEBUG(LOG_TAG_MAIN, "Control task: BUSY (processing command)");
                    
                    if (updateEventGroup) {
                        EventBits_t updateBits = xEventGroupGetBits(updateEventGroup);
                        LOG_DEBUG(LOG_TAG_MAIN, "Update bits waiting: 0x%08X", updateBits);
                    }
                }
            }
            #endif
        }
    }

    // Periodic watchdog stats
    if (millis() - lastWatchdogStats > watchdogStatsInterval) {
        lastWatchdogStats = millis();
        taskManager.logWatchdogStats();

        #if defined(LOG_MODE_DEBUG_FULL) || defined(LOG_MODE_DEBUG_SELECTIVE)
        // Note: TaskManager API no longer provides getTaskWatchdogStats
        // Watchdog stats are logged by logWatchdogStats() above
        #endif

        #if 0  // Commented out - API no longer available
        // Check specific task stats in debug modes
        uint32_t missedFeeds, totalFeeds;
        const char* tasks[] = {"MonitoringTask", "OTATask", "RelayControlTask", "RelayStatusTask"};
        
        for (const char* taskName : tasks) {
            if (taskManager.getTaskWatchdogStats(taskName, missedFeeds, totalFeeds)) {
                LOG_INFO(LOG_TAG_MAIN, "%s watchdog: %u feeds, %u missed", 
                         taskName, totalFeeds, missedFeeds);
            }
        }
        #endif  // 0
    }

    // Periodic system info
    if (millis() - lastSystemInfo > systemInfoInterval) {
        lastSystemInfo = millis();
        printSystemInfo();
    }

    // Periodic relay info (only in debug modes)
    // Now safe with event-driven processing - relayDebugInterval is 30-60 seconds
    if (relayDebugInterval > 0 && millis() - lastRelayInfo > relayDebugInterval) {
        lastRelayInfo = millis();
        printRelayInfo();
    }

    // Memory leak test - only if enabled
    #ifdef ENABLE_MEMORY_LEAK_TEST
    #if defined(LOG_MODE_DEBUG_FULL) || defined(LOG_MODE_DEBUG_SELECTIVE)
    static bool memoryTestRun = false;
    static const unsigned long MEMORY_TEST_DELAY = MEMORY_LEAK_TEST_DELAY_MS;
    
    if (!memoryTestRun && millis() > MEMORY_TEST_DELAY) {
        memoryTestRun = true;
        
        // Make sure relay system is initialized before running test
        if (relayController && relayController->isInitialized() && 
            RelayControlTask::isRunning()) {
            LOG_INFO(LOG_TAG_MAIN, "Starting memory leak test after %lu seconds",
                     MEMORY_TEST_DELAY / 1000);
            runMemoryLeakTest();
        } else {
            LOG_WARN(LOG_TAG_MAIN, "Skipping memory test - relay system not ready");
        }
    }
    #endif
    #endif

    #ifdef RELAY_TEST_MODE
    // Automatic relay test sequence
    static unsigned long lastTestTime = 0;
    static uint8_t testRelayIndex = 1;
    static bool waitingForQueue = false;
    static bool waitingForInit = false;
    static unsigned long lastInitWarnTime = 0;
    
    // First check if relay controller is initialized
    if (relayController && !relayController->isInitialized()) {
        // Log warning periodically, not on every loop
        unsigned long now = millis();
        if (now - lastInitWarnTime > 10000) {  // Every 10 seconds
            LOG_WARN(LOG_TAG_MAIN, "Test mode: Waiting for RYN4 initialization...");
            lastInitWarnTime = now;
        }
        waitingForInit = true;
        return;  // Skip the rest of the test logic
    } else if (waitingForInit && relayController && relayController->isInitialized()) {
        // Just became initialized
        LOG_INFO(LOG_TAG_MAIN, "Test mode: RYN4 initialized, starting relay tests");
        waitingForInit = false;
        lastTestTime = millis();  // Reset test timer
    }
    
    // Only proceed if controller is initialized and test is active
    if (relayController && relayController->isInitialized() && 
        testRelayIndex > 0 &&  // Stop testing when index is 0
        millis() - lastTestTime > RELAY_TEST_INTERVAL_MS) {
        
        // Check queue space before sending command
        size_t pendingCommands = RelayControlTask::getPendingCommandCount();
        
        if (pendingCommands > 5) {
            if (!waitingForQueue) {
                LOG_WARN(LOG_TAG_MAIN, "Test mode: Queue busy (%d commands), delaying test", 
                         pendingCommands);
                waitingForQueue = true;
            }
            // Skip this iteration
            lastTestTime = millis() - (RELAY_TEST_INTERVAL_MS - 1000); // Retry in 1 second
        } else {
            waitingForQueue = false;
            lastTestTime = millis();
            
            LOG_INFO(LOG_TAG_MAIN, "Test mode: Toggling relay %d", testRelayIndex);
            RelayControlTask::toggleRelay(testRelayIndex);
            
            testRelayIndex++;
            if (testRelayIndex > RYN4_NUM_RELAYS) {
                // Test sequence complete - turn all relays OFF
                LOG_INFO(LOG_TAG_MAIN, "Test sequence complete - turning all relays OFF");
                
                // Use setMultipleRelayStates to turn all relays OFF
                std::vector<bool> allOff(8, false);  // 8 relays, all OFF
                ryn4::RelayErrorCode result = relayController->setMultipleRelayStates(allOff);
                if (result == ryn4::RelayErrorCode::SUCCESS) {
                    LOG_INFO(LOG_TAG_MAIN, "All relays turned OFF successfully");
                } else {
                    LOG_ERROR(LOG_TAG_MAIN, "Failed to turn all relays OFF: %d", static_cast<int>(result));
                }
                
                // Disable further testing
                testRelayIndex = 0;  // Set to 0 to stop the test
            }
        }
    }
    #endif
}

// Print system information
void printSystemInfo() {
    LOG_INFO(LOG_TAG_MAIN, "=== System Information ===");
    
    // Uptime
    unsigned long uptimeSeconds = millis() / 1000;
    unsigned long days = uptimeSeconds / 86400;
    unsigned long hours = (uptimeSeconds % 86400) / 3600;
    unsigned long minutes = (uptimeSeconds % 3600) / 60;
    LOG_INFO(LOG_TAG_MAIN, "Uptime: %lu days, %02lu:%02lu", days, hours, minutes);
    
    // Memory
    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t heapSize = ESP.getHeapSize();
    // Convert to integer with 1 decimal place
    int heapPercentInt = (int)((float)freeHeap / heapSize * 1000.0);
    LOG_INFO(LOG_TAG_MAIN, "Free heap: %u bytes (%d.%d%%), Min: %u bytes", 
             freeHeap, heapPercentInt / 10, abs(heapPercentInt % 10), ESP.getMinFreeHeap());
    
    // Check for memory leaks
    static uint32_t lastFreeHeap = 0;
    if (lastFreeHeap > 0 && freeHeap < lastFreeHeap - 5000) {
        LOG_WARN(LOG_TAG_MAIN, "Potential memory leak: %d bytes lost", 
                 lastFreeHeap - freeHeap);
    }
    lastFreeHeap = freeHeap;
    
    // Network
    LOG_INFO(LOG_TAG_MAIN, "Hostname: %s", DEVICE_HOSTNAME);
    if (EthernetManager::isConnected()) {
        LOG_INFO(LOG_TAG_MAIN, "IP: %s, Gateway: %s", 
                 ETH.localIP().toString().c_str(),
                 ETH.gatewayIP().toString().c_str());
        LOG_INFO(LOG_TAG_MAIN, "Link speed: %d Mbps, Full duplex: %s",
                 ETH.linkSpeed(), ETH.fullDuplex() ? "Yes" : "No");
    } else {
        LOG_INFO(LOG_TAG_MAIN, "Ethernet: Not connected");
    }
    
    // RYN4 Status
    if (relayController && relayController->isInitialized()) {
        const ModuleSettings& settings = relayController->getModuleSettings();
        LOG_INFO(LOG_TAG_MAIN, "RYN4: Address 0x%02X, Baud %s, Parity %s",
                 settings.rs485Address,
                 RYN4::baudRateToString(static_cast<BaudRate>(settings.baudRate)).c_str(),
                 RYN4::parityToString(static_cast<Parity>(settings.parity)).c_str());
                 
        // Show event-driven architecture status
        EventGroupHandle_t eventGroup = relayController->getEventGroup();
        if (eventGroup) {
            EventBits_t bits = xEventGroupGetBits(eventGroup);
            LOG_INFO(LOG_TAG_MAIN, "Event Status: Init=%s, Updates=0x%02X, Errors=0x%02X",
                     (bits & BIT0) ? "YES" : "NO",
                     (bits >> 4) & 0xFF,
                     (bits >> 12) & 0xFF);
        }
    } else {
        LOG_INFO(LOG_TAG_MAIN, "RYN4: Not initialized");
    }
    
    LOG_INFO(LOG_TAG_MAIN, "Log Mode: %s",
        #if defined(LOG_MODE_DEBUG_FULL)
            "DEBUG FULL"
        #elif defined(LOG_MODE_DEBUG_SELECTIVE)
            "DEBUG SELECTIVE"
        #else
            "RELEASE"
        #endif
    );
    
    LOG_INFO(LOG_TAG_MAIN, "=========================");
}

// Print relay status information
void printRelayInfo() {
    if (!relayController || !relayController->isInitialized()) {
        LOG_WARN(LOG_TAG_MAIN, "Cannot print relay info - controller not initialized");
        return;
    }
    
    LOG_INFO(LOG_TAG_MAIN, "=== Relay Status ===");
    
    // Get relay states using getData
    IDeviceInstance::DeviceResult<std::vector<float>> result = relayController->getData(
        IDeviceInstance::DeviceDataType::RELAY_STATE);
    
    if (result.isOk() && result.value.size() == RYN4_NUM_RELAYS) {
        std::string statusLine = "Relays: ";
        int activeCount = 0;
        
        for (int i = 0; i < RYN4_NUM_RELAYS; i++) {
            bool isOn = result.value[i] > 0.5f;
            if (isOn) activeCount++;
            statusLine += "R" + std::to_string(i + 1) + ":" + 
                         (isOn ? "ON " : "OFF ");
        }
        LOG_INFO(LOG_TAG_MAIN, "%s", statusLine.c_str());
        LOG_INFO(LOG_TAG_MAIN, "Active relays: %d/%d", activeCount, RYN4_NUM_RELAYS);
    } else {
        LOG_ERROR(LOG_TAG_MAIN, "Failed to get relay states");
    }
    
    // Event status
    EventGroupHandle_t eventGroup = relayController->getEventGroup();
    if (eventGroup) {
        EventBits_t bits = xEventGroupGetBits(eventGroup);
        EventBits_t updateBits = (bits >> 4) & 0xFF;
        EventBits_t errorBits = (bits >> 12) & 0xFF;
        
        if (updateBits || errorBits) {
            LOG_INFO(LOG_TAG_MAIN, "Events - Updates: 0x%02X, Errors: 0x%02X", 
                     updateBits, errorBits);
        }
    }
    
    LOG_INFO(LOG_TAG_MAIN, "===================");
}

void runMemoryLeakTest() {
    LOG_INFO(LOG_TAG_MAIN, "=== MEMORY LEAK TEST STARTING ===");
    
    // Check if system is ready
    if (!relayController || !relayController->isInitialized() || 
        !RelayControlTask::isRunning()) {
        LOG_ERROR(LOG_TAG_MAIN, "System not ready for memory leak test");
        return;
    }
    
    // Wait for any pending operations to complete
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Force garbage collection if possible
    heap_caps_malloc_extmem_enable(0); // Disable external memory temporarily
    heap_caps_malloc_extmem_enable(1); // Re-enable to force cleanup
    
    uint32_t startHeap = ESP.getFreeHeap();
    uint32_t minHeap = startHeap;
    uint32_t testStartTime = millis();

    LOG_INFO(LOG_TAG_MAIN, "Starting heap: %lu bytes", startHeap);
    LOG_INFO(LOG_TAG_MAIN, "Test configuration:");
    LOG_INFO(LOG_TAG_MAIN, "  - Min relay switch interval: %d ms", MIN_RELAY_SWITCH_INTERVAL_MS);
    LOG_INFO(LOG_TAG_MAIN, "  - Max toggle rate: %d per minute", MAX_RELAY_TOGGLE_RATE_PER_MIN);
    
    // Structure to track memory usage by test phase
    struct TestPhase {
        const char* name;
        uint32_t heapBefore;
        uint32_t heapAfter;
        uint32_t operations;
        uint32_t errors;
    };
    
    TestPhase phases[5] = {
        {"Individual Relay Toggle", 0, 0, 0, 0},
        {"Sequential Relay Control", 0, 0, 0, 0},
        {"Set All Relays", 0, 0, 0, 0},
        {"Pattern Testing", 0, 0, 0, 0},
        {"Stress Test", 0, 0, 0, 0}
    };
    
    // =========================================================================
    // TEST 1: Individual Relay Toggle Operations
    // =========================================================================
    LOG_INFO(LOG_TAG_MAIN, "\n--- Test 1: Individual Relay Toggle ---");
    phases[0].heapBefore = ESP.getFreeHeap();
    
    // Reset rate limiting counters by waiting
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    for (int cycle = 0; cycle < 5; cycle++) {
        LOG_INFO(LOG_TAG_MAIN, "Toggle cycle %d/5", cycle + 1);
        
        for (int relay = 1; relay <= 8; relay++) {
            // Check queue space before sending
            size_t pending = RelayControlTask::getPendingCommandCount();
            if (pending > 5) {
                LOG_WARN(LOG_TAG_MAIN, "Queue congested (%d pending), waiting...", pending);
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            
            // Toggle the relay
            if (!RelayControlTask::toggleRelay(relay)) {
                phases[0].errors++;
                LOG_ERROR(LOG_TAG_MAIN, "Failed to queue toggle for relay %d", relay);
            } else {
                phases[0].operations++;
            }
            
            // Wait for command to process and respect rate limiting
            vTaskDelay(pdMS_TO_TICKS(150)); // Increased from 100ms
            
            // Feed watchdog
            taskManager.feedWatchdog();
        }
        
        // Check memory after each cycle
        uint32_t currentHeap = ESP.getFreeHeap();
        if (currentHeap < minHeap) {
            minHeap = currentHeap;
        }
        
        LOG_INFO(LOG_TAG_MAIN, "Cycle %d complete - Heap: %lu (delta: %ld)", 
                 cycle + 1, currentHeap, (long)(currentHeap - phases[0].heapBefore));
        
        // Longer delay between cycles to allow rate limit to reset
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    phases[0].heapAfter = ESP.getFreeHeap();
    // Original simple format
    LOG_INFO(LOG_TAG_MAIN, "Test 1 complete: Heap before=%lu, after=%lu, delta=%ld",
             phases[0].heapBefore, phases[0].heapAfter, 
             (long)(phases[0].heapAfter - phases[0].heapBefore));
    // Additional metrics
    if (phases[0].errors > 0) {
        LOG_WARN(LOG_TAG_MAIN, "  Operations: %lu successful, %lu failed",
                 phases[0].operations, phases[0].errors);
    }
    
    // =========================================================================
    // TEST 2: Sequential Relay Control (Open all, then close all)
    // =========================================================================
    LOG_INFO(LOG_TAG_MAIN, "\n--- Test 2: Sequential Relay Control ---");
    phases[1].heapBefore = ESP.getFreeHeap();
    
    for (int cycle = 0; cycle < 3; cycle++) {
        LOG_INFO(LOG_TAG_MAIN, "Sequential cycle %d/3", cycle + 1);
        
        // Open all relays sequentially
        for (int relay = 1; relay <= 8; relay++) {
            if (RelayControlTask::setRelayState(relay, true)) {
                phases[1].operations++;
            } else {
                phases[1].errors++;
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            taskManager.feedWatchdog();
        }
        
        // Wait for settling
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        // Close all relays sequentially
        for (int relay = 1; relay <= 8; relay++) {
            if (RelayControlTask::setRelayState(relay, false)) {
                phases[1].operations++;
            } else {
                phases[1].errors++;
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            taskManager.feedWatchdog();
        }
        
        // Check memory
        uint32_t currentHeap = ESP.getFreeHeap();
        LOG_INFO(LOG_TAG_MAIN, "Sequential cycle %d - Heap: %lu", cycle + 1, currentHeap);
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    phases[1].heapAfter = ESP.getFreeHeap();
    // Original simple format
    LOG_INFO(LOG_TAG_MAIN, "Test 2 complete: Heap before=%lu, after=%lu, delta=%ld",
             phases[1].heapBefore, phases[1].heapAfter,
             (long)(phases[1].heapAfter - phases[1].heapBefore));
    // Additional metrics
    if (phases[1].errors > 0) {
        LOG_WARN(LOG_TAG_MAIN, "  Operations: %lu successful, %lu failed",
                 phases[1].operations, phases[1].errors);
    }
    
    // =========================================================================
    // TEST 3: Set All Relays (Batch Operations)
    // =========================================================================
    LOG_INFO(LOG_TAG_MAIN, "\n--- Test 3: Set All Relays ---");
    phases[2].heapBefore = ESP.getFreeHeap();
    
    // Wait for queue to clear
    while (RelayControlTask::getPendingCommandCount() > 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    for (int cycle = 0; cycle < 5; cycle++) {
        // Set all ON
        if (RelayControlTask::setAllRelays(true)) {
            phases[2].operations++;
        } else {
            phases[2].errors++;
        }
        
        vTaskDelay(pdMS_TO_TICKS(500)); // Wait for operation to complete
        
        // Set all OFF
        if (RelayControlTask::setAllRelays(false)) {
            phases[2].operations++;
        } else {
            phases[2].errors++;
        }
        
        vTaskDelay(pdMS_TO_TICKS(500));
        
        // Feed watchdog
        taskManager.feedWatchdog();
        
        // Check memory every other cycle
        if (cycle % 2 == 0) {
            uint32_t currentHeap = ESP.getFreeHeap();
            LOG_INFO(LOG_TAG_MAIN, "Batch cycle %d - Heap: %lu", cycle + 1, currentHeap);
        }
    }
    
    phases[2].heapAfter = ESP.getFreeHeap();
    // Original simple format  
    LOG_INFO(LOG_TAG_MAIN, "Test 3 complete: Heap before=%lu, after=%lu, delta=%ld",
             phases[2].heapBefore, phases[2].heapAfter,
             (long)(phases[2].heapAfter - phases[2].heapBefore));
    // Additional metrics
    if (phases[2].errors > 0) {
        LOG_WARN(LOG_TAG_MAIN, "  Operations: %lu successful, %lu failed",
                 phases[2].operations, phases[2].errors);
    }
    
    // =========================================================================
    // TEST 4: Pattern Testing (Various relay patterns)
    // =========================================================================
    LOG_INFO(LOG_TAG_MAIN, "\n--- Test 4: Pattern Testing ---");
    phases[3].heapBefore = ESP.getFreeHeap();
    
    // Test patterns
    std::vector<std::vector<bool>> patterns = {
        {true, false, true, false, true, false, true, false},  // Alternating
        {true, true, false, false, true, true, false, false},  // Pairs
        {true, true, true, true, false, false, false, false},  // Half and half
        {false, true, true, false, false, true, true, false},  // Custom pattern
    };
    
    for (size_t p = 0; p < patterns.size(); p++) {
        LOG_INFO(LOG_TAG_MAIN, "Testing pattern %zu/%zu", p + 1, patterns.size());
        
        if (RelayControlTask::setMultipleRelays(patterns[p])) {
            phases[3].operations++;
        } else {
            phases[3].errors++;
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
        taskManager.feedWatchdog();
    }
    
    phases[3].heapAfter = ESP.getFreeHeap();
    // Original simple format
    LOG_INFO(LOG_TAG_MAIN, "Test 4 complete: Heap before=%lu, after=%lu, delta=%ld",
             phases[3].heapBefore, phases[3].heapAfter,
             (long)(phases[3].heapAfter - phases[3].heapBefore));
    // Additional metrics
    if (phases[3].errors > 0) {
        LOG_WARN(LOG_TAG_MAIN, "  Operations: %lu successful, %lu failed",
                 phases[3].operations, phases[3].errors);
    }
    
    // =========================================================================
    // TEST 5: Stress Test (Rapid operations with error handling)
    // =========================================================================
    LOG_INFO(LOG_TAG_MAIN, "\n--- Test 5: Stress Test ---");
    phases[4].heapBefore = ESP.getFreeHeap();
    
    // Clear rate limiting by waiting
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    uint32_t stressStartTime = millis();
    uint32_t stressDuration = 10000; // 10 seconds
    
    while (millis() - stressStartTime < stressDuration) {
        // Random relay
        uint8_t relay = (rand() % 8) + 1;
        
        // Random action
        int action = rand() % 3;
        bool success = false;
        
        switch (action) {
            case 0: // Toggle
                success = RelayControlTask::toggleRelay(relay);
                break;
            case 1: // Set ON
                success = RelayControlTask::setRelayState(relay, true);
                break;
            case 2: // Set OFF
                success = RelayControlTask::setRelayState(relay, false);
                break;
        }
        
        if (success) {
            phases[4].operations++;
        } else {
            phases[4].errors++;
        }
        
        // Adaptive delay based on queue depth
        size_t pending = RelayControlTask::getPendingCommandCount();
        uint32_t delayMs = 100 + (pending * 50); // More pending = longer delay
        vTaskDelay(pdMS_TO_TICKS(delayMs));
        
        // Feed watchdog frequently during stress test
        taskManager.feedWatchdog();
        
        // Update min heap
        uint32_t currentHeap = ESP.getFreeHeap();
        if (currentHeap < minHeap) {
            minHeap = currentHeap;
        }
    }
    
    // Wait for all commands to process
    while (RelayControlTask::getPendingCommandCount() > 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
        taskManager.feedWatchdog();
    }
    
    phases[4].heapAfter = ESP.getFreeHeap();
    // Original simple format
    LOG_INFO(LOG_TAG_MAIN, "Test 5 complete: Heap before=%lu, after=%lu, delta=%ld",
             phases[4].heapBefore, phases[4].heapAfter,
             (long)(phases[4].heapAfter - phases[4].heapBefore));
    // Additional metrics
    if (phases[4].errors > 0) {
        LOG_WARN(LOG_TAG_MAIN, "  Operations: %lu successful, %lu failed",
                 phases[4].operations, phases[4].errors);
    }
    
    // =========================================================================
    // FINAL REPORT (Keep original simple format first)
    // =========================================================================
    uint32_t finalHeap = ESP.getFreeHeap();
    uint32_t totalLost = startHeap - finalHeap;
    uint32_t testDuration = millis() - testStartTime;
    
    // Original simple report format
    LOG_INFO(LOG_TAG_MAIN, "=== MEMORY LEAK TEST COMPLETE ===");
    LOG_INFO(LOG_TAG_MAIN, "Start heap: %lu, End heap: %lu", startHeap, finalHeap);
    LOG_INFO(LOG_TAG_MAIN, "Total memory lost: %lu bytes", totalLost);
    LOG_INFO(LOG_TAG_MAIN, "Minimum heap reached: %lu bytes", minHeap);
    
    // Original pass/fail assessment
    if (totalLost > 2000) {
        LOG_ERROR(LOG_TAG_MAIN, "MEMORY LEAK DETECTED: Lost %lu bytes", totalLost);
    } else {
        LOG_INFO(LOG_TAG_MAIN, "Memory usage acceptable (< 2KB variation)");
    }
    
    // Additional detailed metrics
    LOG_INFO(LOG_TAG_MAIN, "\n=== Detailed Analysis ===");
    LOG_INFO(LOG_TAG_MAIN, "Test Duration: %lu ms", testDuration);
    
    // Detailed phase analysis
    LOG_INFO(LOG_TAG_MAIN, "\n--- Phase Analysis ---");
    uint32_t totalOps = 0;
    uint32_t totalErrors = 0;
    
    for (int i = 0; i < 5; i++) {
        int32_t phaseLeak = phases[i].heapBefore - phases[i].heapAfter;
        totalOps += phases[i].operations;
        totalErrors += phases[i].errors;
        
        LOG_INFO(LOG_TAG_MAIN, "%s:", phases[i].name);
        LOG_INFO(LOG_TAG_MAIN, "  Memory: %ld bytes %s", 
                 abs(phaseLeak), phaseLeak > 0 ? "lost" : "gained");
        LOG_INFO(LOG_TAG_MAIN, "  Operations: %lu successful, %lu failed",
                 phases[i].operations, phases[i].errors);
        
        if (phases[i].errors > 0) {
            // Convert to integer with 1 decimal place
            int errorRateInt = (int)((float)phases[i].errors / 
                            (phases[i].operations + phases[i].errors) * 1000.0f);
            LOG_WARN(LOG_TAG_MAIN, "  Error rate: %d.%d%%", 
                     errorRateInt / 10, abs(errorRateInt % 10));
        }
    }
    
    LOG_INFO(LOG_TAG_MAIN, "\nTotal operations: %lu", totalOps);
    LOG_INFO(LOG_TAG_MAIN, "Total errors: %lu", totalErrors);
    
    if (totalErrors > 0) {
        // Convert to integer with 1 decimal place
        int overallErrorRateInt = (int)((float)totalErrors / (totalOps + totalErrors) * 1000.0f);
        LOG_WARN(LOG_TAG_MAIN, "Overall error rate: %d.%d%%", 
                 overallErrorRateInt / 10, abs(overallErrorRateInt % 10));
    }
    
    // Memory leak assessment
    if (totalLost > 5000) {
        LOG_ERROR(LOG_TAG_MAIN, "SEVERE MEMORY LEAK DETECTED: Lost %lu bytes", totalLost);
        // Convert to integer with 2 decimal places
        int leakRateInt = (int)((float)totalLost / (testDuration / 1000.0f) * 100.0f);
        LOG_ERROR(LOG_TAG_MAIN, "Average leak rate: %d.%02d bytes/second", 
                  leakRateInt / 100, abs(leakRateInt % 100));
    } else if (totalLost > 2000) {
        LOG_WARN(LOG_TAG_MAIN, "MODERATE MEMORY LEAK: Lost %lu bytes", totalLost);
    } else if (totalLost > 500) {
        LOG_INFO(LOG_TAG_MAIN, "Minor memory variation: %lu bytes (likely fragmentation)", totalLost);
    } else {
        LOG_INFO(LOG_TAG_MAIN, "Memory usage stable - no significant leaks detected");
    }
    
    // Check heap fragmentation
    size_t largestFreeBlock = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    uint32_t fragmentation = 100 - (largestFreeBlock * 100 / finalHeap);
    LOG_INFO(LOG_TAG_MAIN, "Heap fragmentation: %lu%% (largest free block: %lu bytes)",
             fragmentation, largestFreeBlock);
    
    if (fragmentation > 50) {
        LOG_WARN(LOG_TAG_MAIN, "High fragmentation detected - consider restart");
    }
    
    // Recommendations
    LOG_INFO(LOG_TAG_MAIN, "\n--- Recommendations ---");
    if (totalErrors > totalOps * 0.1) {  // More than 10% error rate
        LOG_WARN(LOG_TAG_MAIN, "High error rate detected - check rate limiting settings");
        LOG_WARN(LOG_TAG_MAIN, "Consider increasing MIN_RELAY_SWITCH_INTERVAL_MS");
    }
    
    if (minHeap < 20000) {
        LOG_WARN(LOG_TAG_MAIN, "Heap dropped below 20KB - system may become unstable");
    }
}

void runRelay8DiagnosticTest() {
    LOG_INFO(LOG_TAG_MAIN, "=== RELAY 8 DIAGNOSTIC TEST ===");
    LOG_INFO(LOG_TAG_MAIN, "Testing relay 8 control with the fixed implementation");
    
    // Make sure relay system is ready
    if (!relayController || !relayController->isInitialized() || 
        !RelayControlTask::isRunning()) {
        LOG_ERROR(LOG_TAG_MAIN, "Relay system not ready for diagnostic test");
        return;
    }
    
    // Wait for system to stabilize
    LOG_INFO(LOG_TAG_MAIN, "Waiting for system to stabilize...");
    for (int i = 0; i < 20; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_task_wdt_reset();
    }
    
    // =========================================================================
    // TEST 1: Read Current State (What does hardware report at startup?)
    // =========================================================================
    LOG_INFO(LOG_TAG_MAIN, "\n--- Test 1: Initial State Check ---");
    LOG_INFO(LOG_TAG_MAIN, "Reading current relay states (no commands sent)...");
    
    IDeviceInstance::DeviceResult<std::vector<float>> result = relayController->getData(
        IDeviceInstance::DeviceDataType::RELAY_STATE);
    
    if (result.isOk() && result.value.size() == 8) {
        LOG_INFO(LOG_TAG_MAIN, "Initial relay states as reported by hardware:");
        int onCount = 0;
        for (int i = 0; i < 8; i++) {
            bool isOn = result.value[i] > 0.5f;
            if (isOn) onCount++;
            LOG_INFO(LOG_TAG_MAIN, "  Relay %d: %s", i + 1, isOn ? "ON" : "OFF");
        }
        LOG_INFO(LOG_TAG_MAIN, "Summary: %d relays report as ON, %d as OFF", onCount, 8 - onCount);
    } else {
        LOG_ERROR(LOG_TAG_MAIN, "Failed to read initial relay states");
    }
    
    // =========================================================================
    // TEST 2: Single Relay Control Test (Known Working)
    // =========================================================================
    LOG_INFO(LOG_TAG_MAIN, "\n--- Test 2: Single Relay Control (Relay 1) ---");
    
    // Turn relay 1 ON using individual control
    LOG_INFO(LOG_TAG_MAIN, "Turning Relay 1 ON (individual control)...");
    if (!RelayControlTask::setRelayState(1, true)) {
        LOG_ERROR(LOG_TAG_MAIN, "Failed to send ON command for relay 1");
    }
    
    // Wait for command to process
    for (int i = 0; i < 15; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_task_wdt_reset();
    }
    
    // Check state
    result = relayController->getData(IDeviceInstance::DeviceDataType::RELAY_STATE);
    if (result.isOk() && result.value.size() >= 1) {
        bool relay1On = result.value[0] > 0.5f;
        LOG_INFO(LOG_TAG_MAIN, "Relay 1 after ON command: %s %s", 
                 relay1On ? "ON" : "OFF",
                 relay1On ? "✓ SUCCESS" : "✗ FAILED");
    }
    
    // Turn relay 1 OFF
    LOG_INFO(LOG_TAG_MAIN, "Turning Relay 1 OFF (individual control)...");
    if (!RelayControlTask::setRelayState(1, false)) {
        LOG_ERROR(LOG_TAG_MAIN, "Failed to send OFF command for relay 1");
    }
    
    // Wait for command to process
    for (int i = 0; i < 15; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_task_wdt_reset();
    }
    
    // Check state
    result = relayController->getData(IDeviceInstance::DeviceDataType::RELAY_STATE);
    if (result.isOk() && result.value.size() >= 1) {
        bool relay1On = result.value[0] > 0.5f;
        LOG_INFO(LOG_TAG_MAIN, "Relay 1 after OFF command: %s %s", 
                 relay1On ? "ON" : "OFF",
                 !relay1On ? "✓ SUCCESS" : "✗ FAILED");
    }
    
    // =========================================================================
    // TEST 3: Multi-Relay Write Test (Simple Pattern)
    // =========================================================================
    LOG_INFO(LOG_TAG_MAIN, "\n--- Test 3: Multi-Relay Write Test ---");
    
    // First ensure all are OFF
    LOG_INFO(LOG_TAG_MAIN, "Setting all relays OFF (multi-write)...");
    if (!RelayControlTask::setAllRelays(false)) {
        LOG_ERROR(LOG_TAG_MAIN, "Failed to send all OFF command");
    }
    
    // Wait
    for (int i = 0; i < 20; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_task_wdt_reset();
    }
    
    // Now set a simple pattern: only relay 1 ON
    LOG_INFO(LOG_TAG_MAIN, "Setting pattern: [ON, OFF, OFF, OFF, OFF, OFF, OFF, OFF]");
    std::vector<bool> simplePattern = {true, false, false, false, false, false, false, false};
    
    if (!RelayControlTask::setMultipleRelays(simplePattern)) {
        LOG_ERROR(LOG_TAG_MAIN, "Failed to send pattern command");
    }
    
    // Wait
    for (int i = 0; i < 20; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_task_wdt_reset();
    }
    
    // Check result
    result = relayController->getData(IDeviceInstance::DeviceDataType::RELAY_STATE);
    if (result.isOk() && result.value.size() == 8) {
        LOG_INFO(LOG_TAG_MAIN, "Pattern result:");
        bool patternCorrect = true;
        for (int i = 0; i < 8; i++) {
            bool expected = simplePattern[i];
            bool actual = result.value[i] > 0.5f;
            LOG_INFO(LOG_TAG_MAIN, "  Relay %d: expected %s, got %s %s", 
                     i + 1, 
                     expected ? "ON" : "OFF",
                     actual ? "ON" : "OFF",
                     (expected == actual) ? "✓" : "✗");
            if (expected != actual) patternCorrect = false;
        }
        LOG_INFO(LOG_TAG_MAIN, "Multi-write test: %s", patternCorrect ? "PASSED" : "FAILED");
    }
    
    // =========================================================================
    // TEST 4: Raw Status Read Test
    // =========================================================================
    LOG_INFO(LOG_TAG_MAIN, "\n--- Test 4: Raw Status Read Test ---");
    LOG_INFO(LOG_TAG_MAIN, "Reading all relay status registers directly...");
    
    // This will trigger a read of registers 1-8
    auto readResult = relayController->readAllRelayStatus();
    if (readResult == ryn4::RelayErrorCode::SUCCESS) {
        LOG_INFO(LOG_TAG_MAIN, "Read command sent, waiting for response...");
        
        // Wait for response
        for (int i = 0; i < 10; i++) {
            vTaskDelay(pdMS_TO_TICKS(100));
            esp_task_wdt_reset();
        }
        
        // The response will be logged by handleData
        // Check the states again
        result = relayController->getData(IDeviceInstance::DeviceDataType::RELAY_STATE);
        if (result.isOk() && result.value.size() == 8) {
            LOG_INFO(LOG_TAG_MAIN, "States after raw read:");
            for (int i = 0; i < 8; i++) {
                bool isOn = result.value[i] > 0.5f;
                LOG_INFO(LOG_TAG_MAIN, "  Relay %d: %s", i + 1, isOn ? "ON" : "OFF");
            }
        }
    } else {
        LOG_ERROR(LOG_TAG_MAIN, "Failed to send read command");
    }
    
    // =========================================================================
    // SUMMARY
    // =========================================================================
    LOG_INFO(LOG_TAG_MAIN, "\n=== DIAGNOSTIC SUMMARY ===");
    LOG_INFO(LOG_TAG_MAIN, "Key observations:");
    LOG_INFO(LOG_TAG_MAIN, "1. Check if initial states match physical relay states");
    LOG_INFO(LOG_TAG_MAIN, "2. Verify single relay control works correctly");
    LOG_INFO(LOG_TAG_MAIN, "3. Identify if multi-relay write has issues");
    LOG_INFO(LOG_TAG_MAIN, "4. Look for patterns in the failures");
    LOG_INFO(LOG_TAG_MAIN, "==========================");
    
    // Reset all relays
    LOG_INFO(LOG_TAG_MAIN, "Resetting all relays to OFF...");
    RelayControlTask::setAllRelays(false);
    esp_task_wdt_reset();
}

void verifyRegisterAddressingFix() {
    LOG_INFO(LOG_TAG_MAIN, "=== Verifying Register Addressing Fix ===");
    
    // Make sure relay system is ready
    if (!relayController || !relayController->isInitialized()) {
        LOG_ERROR(LOG_TAG_MAIN, "Relay controller not ready for verification test");
        return;
    }
    
    // Test 1: Read all relay status
    LOG_INFO(LOG_TAG_MAIN, "Test 1: Reading all relay status (registers 1-8)");
    
    auto result = relayController->readAllRelayStatus();
    if (result == ryn4::RelayErrorCode::SUCCESS) {
        LOG_INFO(LOG_TAG_MAIN, "readAllRelayStatus() succeeded - waiting for response");
        
        // Wait for response with watchdog feeding
        for (int i = 0; i < 10; i++) {
            vTaskDelay(pdMS_TO_TICKS(100));
            esp_task_wdt_reset();
        }
        
        // Check relay states
        IDeviceInstance::DeviceResult<std::vector<float>> states = relayController->getData(
            IDeviceInstance::DeviceDataType::RELAY_STATE);
        
        if (states.isOk() && states.value.size() == 8) {
            LOG_INFO(LOG_TAG_MAIN, "Successfully read all 8 relay states:");
            for (int i = 0; i < 8; i++) {
                LOG_INFO(LOG_TAG_MAIN, "  Relay %d: %s", i + 1, 
                         states.value[i] > 0.5f ? "ON" : "OFF");
            }
        } else {
            LOG_ERROR(LOG_TAG_MAIN, "Failed to get relay states after readAllRelayStatus");
        }
    } else {
        LOG_ERROR(LOG_TAG_MAIN, "readAllRelayStatus() failed");
    }
    
    // Test 2: Read individual relay status
    LOG_INFO(LOG_TAG_MAIN, "Test 2: Reading individual relay status");
    
    for (int relay = 1; relay <= 8; relay++) {
        bool state;
        result = relayController->readRelayStatus(relay, state);
        
        if (result == ryn4::RelayErrorCode::SUCCESS) {
            LOG_INFO(LOG_TAG_MAIN, "readRelayStatus(%d) command sent", relay);
        } else {
            LOG_ERROR(LOG_TAG_MAIN, "readRelayStatus(%d) failed", relay);
        }
        
        // Wait and feed watchdog
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_task_wdt_reset();
    }
    
    // Wait a bit more and check final states
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Final state check
    LOG_INFO(LOG_TAG_MAIN, "Final relay states after individual reads:");
    IDeviceInstance::DeviceResult<std::vector<float>> finalStates = relayController->getData(
        IDeviceInstance::DeviceDataType::RELAY_STATE);
    
    if (finalStates.isOk() && finalStates.value.size() == 8) {
        for (int i = 0; i < 8; i++) {
            LOG_INFO(LOG_TAG_MAIN, "  Relay %d: %s", i + 1, 
                     finalStates.value[i] > 0.5f ? "ON" : "OFF");
        }
    }
    
    // Turn off all relays after verification
    LOG_INFO(LOG_TAG_MAIN, "Turning all relays OFF after verification...");
    for (int i = 1; i <= RYN4_NUM_RELAYS; i++) {
        relayController->controlRelay(i, ryn4::RelayAction::OPEN);
    }
    
    // Small delay to ensure commands are processed
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_task_wdt_reset();
    
    LOG_INFO(LOG_TAG_MAIN, "=== Register Addressing Fix Verification Complete ===");
}
// Helper function to print a single relay state (more efficient)
void printSingleRelayState(int relayNum) {
    if (!relayController || relayNum < 1 || relayNum > 8) return;
    
    // For now, we still need to read all states, but we only print one
    IDeviceInstance::DeviceResult<std::vector<float>> states = relayController->getData(
        IDeviceInstance::DeviceDataType::RELAY_STATE);
    
    if (states.isOk() && states.value.size() >= relayNum) {
        LOG_INFO(LOG_TAG_MAIN, "Relay %d state: %s", 
                 relayNum, states.value[relayNum-1] > 0.5f ? "ON" : "OFF");
    }
}

// Helper function to print all relay states
void printAllRelayStates() {
    if (!relayController) return;
    
    IDeviceInstance::DeviceResult<std::vector<float>> states = relayController->getData(
        IDeviceInstance::DeviceDataType::RELAY_STATE);
    
    if (states.isOk() && states.value.size() >= 8) {
        char buffer[256];
        int offset = snprintf(buffer, sizeof(buffer), "Current relay states: ");
        
        for (int i = 0; i < 8; i++) {
            // Convert float value to integer with 1 decimal place
            int valueInt = (int)(states.value[i] * 10.0f);
            offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                             "[%d:%s(%d.%d)] ", i + 1,
                             states.value[i] > 0.5f ? "ON" : "OFF",
                             valueInt / 10, abs(valueInt % 10));
        }
        
        LOG_INFO(LOG_TAG_MAIN, "%s", buffer);
    } else {
        LOG_ERROR(LOG_TAG_MAIN, "Failed to get relay states (error=%d, size=%zu)", 
                 static_cast<int>(states.error), states.value.size());
    }
}
