// LoggingMacros.h - Project-level logging configuration
#pragma once

// Check if LOG_NO_CUSTOM_LOGGER is defined
// This is the master switch that disables all custom logging
#ifdef LOG_NO_CUSTOM_LOGGER
    // Force disable custom logger
    #undef USE_CUSTOM_LOGGER
#endif

// When custom logger is being used, include LogInterface.h
#ifdef USE_CUSTOM_LOGGER
    #include <LogInterface.h>
#else
    // When custom logger is not being used, provide fallback macros
    #include <esp_log.h>
    
    // Map LOG_* macros to ESP_LOG* macros for libraries that expect them
    #define LOG_ERROR(tag, ...) ESP_LOGE(tag, __VA_ARGS__)
    #define LOG_WARN(tag, ...) ESP_LOGW(tag, __VA_ARGS__)
    #define LOG_INFO(tag, ...) ESP_LOGI(tag, __VA_ARGS__)
    #define LOG_DEBUG(tag, ...) ESP_LOGD(tag, __VA_ARGS__)
    #define LOG_VERBOSE(tag, ...) ESP_LOGV(tag, __VA_ARGS__)
    
    // Also provide LOG_WRITE for compatibility
    #define LOG_WRITE(level, tag, format, ...) ESP_LOG_LEVEL(level, tag, format, ##__VA_ARGS__)
#endif

// Define project-specific log tags if needed
#ifndef LOG_TAG_MAIN
#define LOG_TAG_MAIN "MAIN"
#endif

#ifndef LOG_TAG_OTA
#define LOG_TAG_OTA "OTA"
#endif

#ifndef LOG_TAG_ETH
#define LOG_TAG_ETH "ETH"
#endif

#ifndef LOG_TAG_MONITORING
#define LOG_TAG_MONITORING "MON"
#endif

#ifndef LOG_TAG_RELAY
#define LOG_TAG_RELAY "RELAY"
#endif

#ifndef LOG_TAG_RELAY_CONTROL
#define LOG_TAG_RELAY_CONTROL "RlyCtrl"
#endif

#ifndef LOG_TAG_RELAY_STATUS
#define LOG_TAG_RELAY_STATUS "RlyStat"
#endif

#ifndef LOG_TAG_MODBUS
#define LOG_TAG_MODBUS "MODBUS"
#endif