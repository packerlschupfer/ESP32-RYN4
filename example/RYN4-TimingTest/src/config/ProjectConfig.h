// ProjectConfig.h
#pragma once
#include <Arduino.h>
#include <esp_log.h>

// If not defined in platformio.ini, set defaults
#ifndef DEVICE_HOSTNAME
#define DEVICE_HOSTNAME "esp32-relay-controller"
#endif

// Ethernet PHY Settings
#ifndef ETH_PHY_MDC_PIN
#define ETH_PHY_MDC_PIN 23
#endif

#ifndef ETH_PHY_MDIO_PIN
#define ETH_PHY_MDIO_PIN 18
#endif

#ifndef ETH_PHY_ADDR
#define ETH_PHY_ADDR 0
#endif

#ifndef ETH_PHY_POWER_PIN
#define ETH_PHY_POWER_PIN -1  // No power pin
#endif

#define ETH_CLOCK_MODE ETH_CLOCK_GPIO17_OUT

// Optional custom MAC address (uncomment and set if needed)
// #define ETH_MAC_ADDRESS {0x02, 0xAB, 0xCD, 0xEF, 0x12, 0x34}

// Ethernet connection timeout
#define ETH_CONNECTION_TIMEOUT_MS 3000  // Reduced from 15000ms to 3000ms for faster startup

// OTA Settings
#ifndef OTA_PASSWORD
#define OTA_PASSWORD "update-password"  // Set your OTA password here
#endif

#ifndef OTA_PORT
#define OTA_PORT 3232
#endif

// Status LED
#ifndef STATUS_LED_PIN
#define STATUS_LED_PIN 2  // Onboard LED on most ESP32 dev boards
#endif

// Modbus RS-485 Settings
#define MODBUS_RX_PIN 36
#define MODBUS_TX_PIN 4
#define MODBUS_BAUD_RATE 9600

// RYN4 Relay Module Settings
#define RYN4_ADDRESS 0x02
#define RYN4_NUM_RELAYS 8
#define DEFAULT_RELAY_STATE false  // false = OFF/CLOSED, true = ON/OPEN

// Task Settings - Three-tier optimization based on logging mode
// Stack sizes are in bytes, not words. Each word = 4 bytes on ESP32
// Minimum recommended is 2048 bytes for simple tasks

#if defined(LOG_MODE_DEBUG_FULL)
    // DEBUG FULL MODE - Maximum logging requires more stack
    #define STACK_SIZE_OTA_TASK              4096    // OTA needs extra for debug logging
    #define STACK_SIZE_MONITORING_TASK       5120    // Extra for extensive logging
    #define STACK_SIZE_RELAY_CONTROL_TASK    4096    // Extra for debug output
    #define STACK_SIZE_RELAY_STATUS_TASK     5120    // Extra for detailed status logs
    #define STACK_SIZE_DEBUG_TASK            6144    // Debug task for system monitoring

#elif defined(LOG_MODE_DEBUG_SELECTIVE)
    // DEBUG SELECTIVE MODE - Moderate stack usage
    #define STACK_SIZE_OTA_TASK              3072    // Standard size
    #define STACK_SIZE_MONITORING_TASK       3072    // Standard size
    #define STACK_SIZE_RELAY_CONTROL_TASK    3072    // Standard size
    #define STACK_SIZE_RELAY_STATUS_TASK     3584    // Slightly more for status formatting
    #define STACK_SIZE_DEBUG_TASK            6144    // Standard size for debug

#else  // LOG_MODE_RELEASE
    // RELEASE MODE - Optimized stack usage for production stability
    #define STACK_SIZE_OTA_TASK              3072    // Stable size
    #define STACK_SIZE_MONITORING_TASK       3072    // Minimal monitoring
    #define STACK_SIZE_RELAY_CONTROL_TASK    3072    // Stable size
    #define STACK_SIZE_RELAY_STATUS_TASK     3584    // Extra for formatting
    #define STACK_SIZE_DEBUG_TASK            6144    // Minimal debug

#endif

// Task priorities (lower number = lower priority, 0 = idle task priority)
// Note: FreeRTOS priorities - higher number = higher priority
// Critical path: Modbus communication -> Relay control -> Status monitoring -> General monitoring
#define PRIORITY_OTA_TASK 1                // Lowest - background updates
#define PRIORITY_DEBUG_TASK 1              // Lowest - diagnostics only
#define PRIORITY_MONITORING_TASK 2         // Low - general system monitoring
#define PRIORITY_RELAY_STATUS_TASK 3       // Medium - relay state monitoring
#define PRIORITY_RELAY_CONTROL_TASK 4      // High - relay control operations
// Note: esp32ModbusRTU task runs at priority 5 (set by the library)

// Task Intervals - Can also be optimized per mode
#if defined(LOG_MODE_DEBUG_FULL)
    // Debug Full - Fast but not excessive
    #define MONITORING_TASK_INTERVAL_MS        60000   // 1 minute (was 30s)
    #define RELAY_STATUS_TASK_INTERVAL_MS      30000    // 30 seconds (was 5s)
    #define RELAY_CONTROL_TASK_INTERVAL_MS     250     // 250ms check (was 100ms)
    #define OTA_TASK_INTERVAL_MS               2000    // 2 seconds (was 1s)
    #define DEBUG_TASK_INTERVAL_MS             5000    // 5 seconds
    #define RESOURCE_LOG_PERIOD_MS             60000   // 1 minute

#elif defined(LOG_MODE_DEBUG_SELECTIVE)
    // Debug Selective - Balanced
    #define MONITORING_TASK_INTERVAL_MS        60000   // 1 minute (was 30s)
    #define RELAY_STATUS_TASK_INTERVAL_MS      30000   // 30 seconds (was 2s)
    #define RELAY_CONTROL_TASK_INTERVAL_MS     500     // 500ms check
    #define OTA_TASK_INTERVAL_MS               3000    // 3 seconds
    #define DEBUG_TASK_INTERVAL_MS             10000   // 10 seconds
    #define RESOURCE_LOG_PERIOD_MS             120000  // 2 minutes

#else  // LOG_MODE_RELEASE
    // Release - Relaxed for production
    #define MONITORING_TASK_INTERVAL_MS        300000  // 5 minutes (was 60s)
    #define RELAY_STATUS_TASK_INTERVAL_MS      30000   // 30 seconds (was 5s)
    #define RELAY_CONTROL_TASK_INTERVAL_MS     1000    // 1 second check
    #define OTA_TASK_INTERVAL_MS               5000    // 5 seconds
    #define DEBUG_TASK_INTERVAL_MS             30000   // 30 seconds
    // #define RESOURCE_LOG_PERIOD_MS             300000  // 5 minutes
    #define RESOURCE_LOG_PERIOD_MS             10000  // 5 minutes
#endif

// Watchdog timeouts - adjusted for new intervals
#define WATCHDOG_TIMEOUT_SECONDS 30
#define WATCHDOG_MIN_HEAP_BYTES 10000

#if defined(LOG_MODE_DEBUG_FULL)
    #define DEBUG_TASK_WATCHDOG_TIMEOUT_MS      30000   // 30 seconds
#elif defined(LOG_MODE_DEBUG_SELECTIVE)
    #define DEBUG_TASK_WATCHDOG_TIMEOUT_MS      45000   // 45 seconds
#else  // LOG_MODE_RELEASE
    #define DEBUG_TASK_WATCHDOG_TIMEOUT_MS      60000   // 60 seconds
#endif

// Individual task watchdog timeouts (in milliseconds)
#define OTA_TASK_WATCHDOG_TIMEOUT_MS (OTA_TASK_INTERVAL_MS * 4 + 5000)
#define MONITORING_TASK_WATCHDOG_TIMEOUT_MS (MONITORING_TASK_INTERVAL_MS + 5000)
#define RELAY_CONTROL_TASK_WATCHDOG_TIMEOUT_MS (RELAY_CONTROL_TASK_INTERVAL_MS * 4 + 5000)
#define RELAY_STATUS_TASK_WATCHDOG_TIMEOUT_MS (RELAY_STATUS_TASK_INTERVAL_MS * 2 + 5000)

// Log Tags
#define LOG_TAG_MAIN "MAIN"
#define LOG_TAG_OTA "OTA"
#define LOG_TAG_ETH "ETH"
#define LOG_TAG_MONITORING "MON"
#define LOG_TAG_RELAY "RELAY"
#define LOG_TAG_RELAY_CONTROL "RlyCtrl"
#define LOG_TAG_RELAY_STATUS "RlyStat"
#define LOG_TAG_MODBUS "MODBUS"

// RYN4 specific settings
#define RYN4_RESPONSE_TIMEOUT_MS 1000           // Timeout for RYN4 responses
#define RYN4_RETRY_COUNT 3                      // Number of retries for failed requests
#define RYN4_INTER_COMMAND_DELAY_MS 50          // Delay between consecutive commands

// Relay operation safety limits
#define MIN_RELAY_SWITCH_INTERVAL_MS 150        // Minimum time between relay toggles
#define MAX_RELAY_TOGGLE_RATE_PER_MIN 30        // Maximum toggles per minute per relay

// Optional relay safety checks
#define ENABLE_RELAY_SAFETY_CHECKS              // Comment out to disable safety checks
#define ENABLE_RELAY_EVENT_LOGGING              // Comment out to disable event logging

// Optional: Debug mode specific buffer sizes
#if defined(LOG_MODE_DEBUG_FULL)
    #define MODBUS_LOG_BUFFER_SIZE 512
    #define STATUS_LOG_BUFFER_SIZE 512
    #define EVENT_LOG_BUFFER_SIZE 512
#else
    #define MODBUS_LOG_BUFFER_SIZE 256
    #define STATUS_LOG_BUFFER_SIZE 256
    #define EVENT_LOG_BUFFER_SIZE 256
#endif

// Include LoggingMacros.h for LOG_* macros
#include "LoggingMacros.h"

// DO NOT redefine ETH_LOG_* or OTA_LOG_* macros here, as they're already defined in the respective
// libraries. Instead, just use LOG_* macros for project-specific logging

// Relay test mode - uncomment to enable automatic relay testing
// #define RELAY_TEST_MODE
#ifdef RELAY_TEST_MODE
    #define RELAY_TEST_INTERVAL_MS 5000         // Toggle relay every 5 seconds
#endif

// Memory leak test - uncomment to enable memory leak testing
// #define ENABLE_MEMORY_LEAK_TEST
#ifdef ENABLE_MEMORY_LEAK_TEST
    #define MEMORY_LEAK_TEST_DELAY_MS 150000    // Run test after 2,5 minutes
#endif