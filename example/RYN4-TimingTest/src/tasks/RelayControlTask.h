// tasks/RelayControlTask.h
#pragma once

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "config/ProjectConfig.h"
#include "RYN4.h"
#include <vector>

/**
 * @brief Task responsible for controlling relay operations
 * 
 * This task manages all relay control commands through a queue-based system,
 * ensuring thread-safe operation and proper command sequencing.
 */
class RelayControlTask {
public:
    // Command types
    enum class CommandType {
        SET_SINGLE,      // Set single relay state
        SET_ALL,         // Set all relays to same state
        SET_MULTIPLE,    // Set multiple relays with pattern
        TOGGLE,          // Toggle single relay
        TOGGLE_ALL,      // Toggle all relays
        READ_CONFIG,     // Read module configuration
        SET_CONFIG       // Set module configuration
    };
    
    // Command structure
    struct RelayCommand {
        CommandType type;
        uint8_t relayIndex;      // For single relay commands (1-8)
        bool state;              // Desired state
        bool states[RYN4_NUM_RELAYS]; // For multiple relay commands
        uint8_t stateCount;  // Track how many states are valid
        uint32_t timestamp;      // Command timestamp
        
        RelayCommand() : type(CommandType::SET_SINGLE), relayIndex(1), 
                        state(false), timestamp(0) {}
    };
    
    /**
     * @brief Initialize the relay control task
     * @param device Pointer to the RYN4 device instance
     * @return true if initialization successful
     */
    static bool init(RYN4* device);
    
    /**
     * @brief Start the relay control task
     * @return true if task started successfully
     */
    static bool start();
    
    /**
     * @brief Stop the relay control task
     */
    static void stop();
    
    // Control commands - these queue commands for the task to process
    
    /**
     * @brief Toggle a single relay
     * @param relayIndex Relay number (1-8)
     * @return true if command queued successfully
     */
    static bool toggleRelay(uint8_t relayIndex);
    
    /**
     * @brief Set a single relay state
     * @param relayIndex Relay number (1-8)
     * @param state true = ON/OPEN, false = OFF/CLOSED
     * @return true if command queued successfully
     */
    static bool setRelayState(uint8_t relayIndex, bool state);
    
    /**
     * @brief Set all relays to the same state
     * @param state true = ON/OPEN, false = OFF/CLOSED
     * @return true if command queued successfully
     */
    static bool setAllRelays(bool state);
    
    /**
     * @brief Set multiple relays with specific pattern
     * @param states Vector of states for each relay
     * @return true if command queued successfully
     */
    static bool setMultipleRelays(const std::vector<bool>& states);
    
    /**
     * @brief Toggle all relays
     * @return true if command queued successfully
     */
    static bool toggleAllRelays();
    
    /**
     * @brief Get pending command count
     * @return Number of commands in queue
     */
    static size_t getPendingCommandCount();
    
    /**
     * @brief Check if the task is running
     * @return true if task is active
     */
    static bool isRunning();
    
    /**
     * @brief Get the task handle
     * @return TaskHandle_t of the control task
     */
    static TaskHandle_t getTaskHandle();
    
    /**
     * @brief Check if task is busy processing commands
     * @return true if currently processing a command
     */
    static bool isBusy();
    
    /**
     * @brief Get statistics
     */
    static void getStatistics(uint32_t& commandsProcessed, uint32_t& commandsFailed);

private:
    // Task function
    static void taskFunction(void* pvParameters);
    
    // Task configuration
    static constexpr const char* TASK_NAME = "RelayControlTask";
    static constexpr uint32_t STACK_SIZE = STACK_SIZE_RELAY_CONTROL_TASK;
    static constexpr UBaseType_t TASK_PRIORITY = PRIORITY_RELAY_CONTROL_TASK;
    static constexpr const char* TASK_TAG = LOG_TAG_RELAY_CONTROL;
    static constexpr uint32_t TASK_INTERVAL_MS = RELAY_CONTROL_TASK_INTERVAL_MS;
    
    // Task state
    static RYN4* ryn4Device;
    static TaskHandle_t taskHandle;
    static QueueHandle_t commandQueue;
    static SemaphoreHandle_t taskMutex;
    static bool initialized;
    static bool running;
    static bool busy;

    static EventGroupHandle_t commandEventGroup;
    
    // Define command status bits
    static constexpr EventBits_t COMMAND_PROCESSING_BIT = BIT0;
    static constexpr EventBits_t COMMAND_SUCCESS_BIT = BIT1;
    static constexpr EventBits_t COMMAND_ERROR_BIT = BIT2;
    
    // Statistics
    static uint32_t commandsProcessed;
    static uint32_t commandsFailed;
    static TickType_t lastCommandTime;
    
    // Rate limiting
    static uint32_t toggleCount[8];
    static TickType_t toggleTimestamps[8];
    static TickType_t rateWindowStart;
    
    // Private methods
    static bool processCommand(const RelayCommand& cmd);
    static bool processSingleRelay(uint8_t relayIndex, bool state);
    static bool processToggleRelay(uint8_t relayIndex);
    static bool processSetAllRelays(bool state);
    static bool processSetMultipleRelays(const std::vector<bool>& states);
    static bool processToggleAllRelays();
    
    static bool checkRateLimit(uint8_t relayIndex);
    static void updateRateLimitCounters();
    static bool queueCommand(const RelayCommand& cmd);
};
