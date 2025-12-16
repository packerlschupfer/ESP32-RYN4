#ifndef RYN4_LOGGING_H
#define RYN4_LOGGING_H

#define RYN4_LOG_TAG "RYN4"

// ==========================
// Feature Configuration
// ==========================
// Optimistic updates: Update internal state immediately after sending command,
// without waiting for hardware verification. Provides faster UI response but
// may show incorrect state if command fails.
// Default: DISABLED (safer - state only updates after verification)
// To enable: #define RYN4_ENABLE_OPTIMISTIC_UPDATES in your build flags
// #define RYN4_ENABLE_OPTIMISTIC_UPDATES

// Define log levels based on debug flag
#ifdef RYN4_DEBUG
    // Debug mode: Show all levels
    #define RYN4_LOG_LEVEL_E ESP_LOG_ERROR
    #define RYN4_LOG_LEVEL_W ESP_LOG_WARN
    #define RYN4_LOG_LEVEL_I ESP_LOG_INFO
    #define RYN4_LOG_LEVEL_D ESP_LOG_DEBUG
    #define RYN4_LOG_LEVEL_V ESP_LOG_VERBOSE
#else
    // Release mode: Only Error, Warn, Info
    #define RYN4_LOG_LEVEL_E ESP_LOG_ERROR
    #define RYN4_LOG_LEVEL_W ESP_LOG_WARN
    #define RYN4_LOG_LEVEL_I ESP_LOG_INFO
    #define RYN4_LOG_LEVEL_D ESP_LOG_NONE  // Suppress
    #define RYN4_LOG_LEVEL_V ESP_LOG_NONE  // Suppress
#endif

// Route to custom logger or ESP-IDF
#ifdef USE_CUSTOM_LOGGER
    #include <LogInterface.h>
    #define RYN4_LOG_E(...) LOG_WRITE(RYN4_LOG_LEVEL_E, RYN4_LOG_TAG, __VA_ARGS__)
    #define RYN4_LOG_W(...) LOG_WRITE(RYN4_LOG_LEVEL_W, RYN4_LOG_TAG, __VA_ARGS__)
    #define RYN4_LOG_I(...) LOG_WRITE(RYN4_LOG_LEVEL_I, RYN4_LOG_TAG, __VA_ARGS__)
    #ifdef RYN4_DEBUG
        #define RYN4_LOG_D(...) LOG_WRITE(RYN4_LOG_LEVEL_D, RYN4_LOG_TAG, __VA_ARGS__)
        #define RYN4_LOG_V(...) LOG_WRITE(RYN4_LOG_LEVEL_V, RYN4_LOG_TAG, __VA_ARGS__)
    #else
        #define RYN4_LOG_D(...) ((void)0)
        #define RYN4_LOG_V(...) ((void)0)
    #endif
#else
    // ESP-IDF logging with compile-time suppression
    #include <esp_log.h>
    #define RYN4_LOG_E(...) ESP_LOGE(RYN4_LOG_TAG, __VA_ARGS__)
    #define RYN4_LOG_W(...) ESP_LOGW(RYN4_LOG_TAG, __VA_ARGS__)
    #define RYN4_LOG_I(...) ESP_LOGI(RYN4_LOG_TAG, __VA_ARGS__)
    #ifdef RYN4_DEBUG
        #define RYN4_LOG_D(...) ESP_LOGD(RYN4_LOG_TAG, __VA_ARGS__)
        #define RYN4_LOG_V(...) ESP_LOGV(RYN4_LOG_TAG, __VA_ARGS__)
    #else
        #define RYN4_LOG_D(...) ((void)0)
        #define RYN4_LOG_V(...) ((void)0)
    #endif
#endif

// Advanced debug features for RYN4
#ifdef RYN4_DEBUG
    #define RYN4_DEBUG_MODBUS    // Modbus protocol debugging
    #define RYN4_DEBUG_RELAY     // Relay state debugging
    #define RYN4_DEBUG_TIMING    // Performance timing
#endif

// Feature-specific logging
#ifdef RYN4_DEBUG_MODBUS
    #define RYN4_LOG_MODBUS(...) RYN4_LOG_D("MODBUS: " __VA_ARGS__)
    #define RYN4_DUMP_MODBUS(msg, buf, len) do { \
        char hex[64]; \
        int pos = 0; \
        for (int i = 0; i < len && i < 20; i++) { \
            pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", buf[i]); \
        } \
        if (len > 20) { \
            pos += snprintf(hex + pos, sizeof(hex) - pos, "..."); \
        } \
        RYN4_LOG_D("MODBUS: %s [%d bytes]: %s", msg, len, hex); \
    } while(0)
#else
    #define RYN4_LOG_MODBUS(...) ((void)0)
    #define RYN4_DUMP_MODBUS(msg, buf, len) ((void)0)
#endif

#ifdef RYN4_DEBUG_RELAY
    #define RYN4_LOG_RELAY(...) RYN4_LOG_D("RELAY: " __VA_ARGS__)
    #define RYN4_LOG_RELAY_STATE(num, state) RYN4_LOG_D("RELAY: Relay %d = %s", num, state ? "ON" : "OFF")
#else
    #define RYN4_LOG_RELAY(...) ((void)0)
    #define RYN4_LOG_RELAY_STATE(num, state) ((void)0)
#endif

#ifdef RYN4_DEBUG_TIMING
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
    #define RYN4_TIME_START() TickType_t _start = xTaskGetTickCount()
    #define RYN4_TIME_END(msg) RYN4_LOG_D("TIMING: %s took %lu ms", msg, pdTICKS_TO_MS(xTaskGetTickCount() - _start))
#else
    #define RYN4_TIME_START() ((void)0)
    #define RYN4_TIME_END(msg) ((void)0)
#endif

// Always log relay state changes in release mode for operational visibility
#define RYN4_LOG_RELAY_CHANGE(relay_num, old_state, new_state) \
    RYN4_LOG_I("Relay %d: %s -> %s", relay_num, \
               old_state ? "ON" : "OFF", new_state ? "ON" : "OFF")

// Initialization logging
#define RYN4_LOG_INIT_STEP(step) RYN4_LOG_I("Init: %s", step)
#define RYN4_LOG_INIT_COMPLETE() RYN4_LOG_I("RYN4 Ready")

// Error logging helpers
#define RYN4_LOG_ERROR_DETAIL(msg, code) RYN4_LOG_E("%s (code: 0x%02X)", msg, code)

// Throttled logging for high-frequency operations
#define RYN4_LOG_THROTTLED(interval_ms, format, ...) do { \
    static TickType_t _last_log = 0; \
    TickType_t _now = xTaskGetTickCount(); \
    if (_now - _last_log >= pdMS_TO_TICKS(interval_ms)) { \
        RYN4_LOG_D(format, ##__VA_ARGS__); \
        _last_log = _now; \
    } \
} while(0)

// Conditional code execution based on debug mode
#ifdef RYN4_DEBUG
    #define RYN4_DEBUG_ONLY(...) __VA_ARGS__
    #define RYN4_RELEASE_ONLY(...) ((void)0)
    #define RYN4_DEBUG_BLOCK if(true)
#else
    #define RYN4_DEBUG_ONLY(...) ((void)0)
    #define RYN4_RELEASE_ONLY(...) __VA_ARGS__
    #define RYN4_DEBUG_BLOCK if(false)
#endif

// Stack monitoring macros
#ifdef RYN4_DEBUG
    #define RYN4_STACK_CHECK_START() UBaseType_t _stack_start = uxTaskGetStackHighWaterMark(nullptr)
    #define RYN4_STACK_CHECK_END(msg) do { \
        UBaseType_t _stack_end = uxTaskGetStackHighWaterMark(nullptr); \
        UBaseType_t _stack_used = (_stack_start - _stack_end) * 4; \
        RYN4_LOG_D("Stack %s: start=%d, end=%d, used=%d bytes", \
                   msg, _stack_start * 4, _stack_end * 4, _stack_used); \
    } while(0)
    #define RYN4_STACK_CHECK_POINT(msg) do { \
        UBaseType_t _stack_free = uxTaskGetStackHighWaterMark(nullptr) * 4; \
        RYN4_LOG_D("Stack %s: free=%d bytes", msg, _stack_free); \
    } while(0)
#else
    #define RYN4_STACK_CHECK_START() ((void)0)
    #define RYN4_STACK_CHECK_END(msg) ((void)0)
    #define RYN4_STACK_CHECK_POINT(msg) ((void)0)
#endif

// Critical operation logging
#ifdef RYN4_DEBUG
    #define RYN4_LOG_CRITICAL_ENTRY(section) RYN4_LOG_D(">>> Entering: %s", section)
    #define RYN4_LOG_CRITICAL_EXIT(section) RYN4_LOG_D("<<< Exiting: %s", section)
#else
    #define RYN4_LOG_CRITICAL_ENTRY(section) ((void)0)
    #define RYN4_LOG_CRITICAL_EXIT(section) ((void)0)
#endif

// Summary logging for relay states
#ifdef RYN4_DEBUG_RELAY
    #define RYN4_LOG_ALL_RELAY_STATES(relays, num_relays) do { \
        char status[128]; \
        int offset = snprintf(status, sizeof(status), "Relay States: "); \
        for (int i = 0; i < num_relays && i < 8; i++) { \
            offset += snprintf(status + offset, sizeof(status) - offset, \
                               "R%d:%s ", i + 1, relays[i].isOn ? "ON" : "OFF"); \
        } \
        RYN4_LOG_I("%s", status); \
    } while(0)
#else
    #define RYN4_LOG_ALL_RELAY_STATES(relays, num_relays) ((void)0)
#endif

// ==========================
// Modbus Error Tracking
// ==========================
// Helper macros for ModbusErrorTracker integration.
// Use after each Modbus operation to track success/error statistics.
//
// Usage:
//   auto result = writeSingleRegister(addr, value);
//   RYN4_TRACK_MODBUS_RESULT(result);
//
// Or for operations inside retry policy:
//   if (success) {
//       RYN4_TRACK_SUCCESS();
//   } else {
//       RYN4_TRACK_TIMEOUT();  // or RYN4_TRACK_ERROR(result.error())
//   }

#include <ModbusErrorTracker.h>

// Track a ModbusResult (has .isOk() and .error() methods)
#define RYN4_TRACK_MODBUS_RESULT(result) do { \
    if ((result).isOk()) { \
        modbus::ModbusErrorTracker::recordSuccess(getServerAddress()); \
    } else { \
        auto _cat = modbus::ModbusErrorTracker::categorizeError((result).error()); \
        modbus::ModbusErrorTracker::recordError(getServerAddress(), _cat); \
    } \
} while(0)

// Track success
#define RYN4_TRACK_SUCCESS() \
    modbus::ModbusErrorTracker::recordSuccess(getServerAddress())

// Track error with ModbusError
#define RYN4_TRACK_ERROR(error) do { \
    auto _cat = modbus::ModbusErrorTracker::categorizeError(error); \
    modbus::ModbusErrorTracker::recordError(getServerAddress(), _cat); \
} while(0)

// Track timeout (common case after failed retry)
#define RYN4_TRACK_TIMEOUT() \
    modbus::ModbusErrorTracker::recordError(getServerAddress(), \
        modbus::ModbusErrorTracker::ErrorCategory::TIMEOUT)

#endif // RYN4_LOGGING_H