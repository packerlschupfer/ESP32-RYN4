// WatchdogHelper.h
// Helper functions for safe watchdog registration that prevent duplicate subscriptions
#pragma once

#include <esp_task_wdt.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class WatchdogHelper {
public:
    /**
     * @brief Safely add current task to watchdog, checking if already registered
     * @return true if successfully added or already registered, false on error
     */
    static bool safeAddCurrentTask() {
        TaskHandle_t currentTask = xTaskGetCurrentTaskHandle();
        if (!currentTask) {
            return false;
        }
        
        // Check current status
        esp_err_t status = esp_task_wdt_status(currentTask);
        
        if (status == ESP_OK) {
            // Already registered
            return true;
        } else if (status == ESP_ERR_NOT_FOUND) {
            // Not registered, try to add
            esp_err_t err = esp_task_wdt_add(currentTask);
            return (err == ESP_OK);
        } else {
            // Some other error
            ESP_LOGE("WDT", "Failed to get watchdog status: 0x%x", status);
            return false;
        }
    }
    
    /**
     * @brief Safely remove current task from watchdog
     * @return true if successfully removed or not registered, false on error
     */
    static bool safeDeleteCurrentTask() {
        TaskHandle_t currentTask = xTaskGetCurrentTaskHandle();
        if (!currentTask) {
            return false;
        }
        
        // Check current status
        esp_err_t status = esp_task_wdt_status(currentTask);
        
        if (status == ESP_ERR_NOT_FOUND) {
            // Not registered, nothing to do
            return true;
        } else if (status == ESP_OK) {
            // Registered, try to remove
            esp_err_t err = esp_task_wdt_delete(currentTask);
            return (err == ESP_OK);
        } else {
            // Some other error
            ESP_LOGE("WDT", "Failed to get watchdog status: 0x%x", status);
            return false;
        }
    }
    
    /**
     * @brief Check if current task is registered with watchdog
     * @return true if registered, false if not
     */
    static bool isCurrentTaskRegistered() {
        TaskHandle_t currentTask = xTaskGetCurrentTaskHandle();
        if (!currentTask) {
            return false;
        }
        
        esp_err_t status = esp_task_wdt_status(currentTask);
        return (status == ESP_OK);
    }
    
    /**
     * @brief Feed watchdog for current task
     * @return true if successful, false on error
     */
    static bool feedWatchdog() {
        esp_err_t err = esp_task_wdt_reset();
        return (err == ESP_OK);
    }
};