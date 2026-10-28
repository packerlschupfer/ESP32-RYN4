// WatchdogDebug.h
// Debug utilities to trace watchdog registration issues
#pragma once

#include <esp_task_wdt.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define WDT_TRACE_TAG "WDT_TRACE"

// Hook into esp_task_wdt_add to trace calls
extern "C" {
    // Original function declaration
    extern esp_err_t __real_esp_task_wdt_add(TaskHandle_t task_handle);
    
    // Our wrapper function
    esp_err_t __wrap_esp_task_wdt_add(TaskHandle_t task_handle) {
        // Get task name
        const char* taskName = "unknown";
        if (task_handle == NULL) {
            task_handle = xTaskGetCurrentTaskHandle();
            taskName = pcTaskGetName(task_handle);
        } else {
            taskName = pcTaskGetName(task_handle);
        }
        
        // Log the call with timestamp
        ESP_LOGW(WDT_TRACE_TAG, "[%lu ms] esp_task_wdt_add called for task: %s (handle: %p)", 
                 millis(), taskName, task_handle);
        
        // Log the call stack if possible
        void* pc = __builtin_return_address(0);
        ESP_LOGW(WDT_TRACE_TAG, "  Called from: %p", pc);
        
        // Call the real function
        esp_err_t result = __real_esp_task_wdt_add(task_handle);
        
        // Log the result
        if (result == ESP_OK) {
            ESP_LOGI(WDT_TRACE_TAG, "  Result: SUCCESS - Task %s added to watchdog", taskName);
        } else if (result == ESP_ERR_INVALID_ARG) {
            ESP_LOGE(WDT_TRACE_TAG, "  Result: ALREADY SUBSCRIBED - Task %s", taskName);
        } else {
            ESP_LOGE(WDT_TRACE_TAG, "  Result: ERROR 0x%x - Task %s", result, taskName);
        }
        
        return result;
    }
}

// Function to dump all current tasks and their watchdog status
void dumpWatchdogStatus() {
    ESP_LOGI(WDT_TRACE_TAG, "=== Current Task Watchdog Status ===");
    
    // Get task list
    UBaseType_t uxArraySize = uxTaskGetNumberOfTasks();
    TaskStatus_t* pxTaskStatusArray = (TaskStatus_t*)pvPortMalloc(uxArraySize * sizeof(TaskStatus_t));
    
    if (pxTaskStatusArray != NULL) {
        UBaseType_t uxTaskCount = uxTaskGetSystemState(pxTaskStatusArray, uxArraySize, NULL);
        
        for (UBaseType_t i = 0; i < uxTaskCount; i++) {
            esp_err_t wdtStatus = esp_task_wdt_status(pxTaskStatusArray[i].xHandle);
            const char* statusStr = (wdtStatus == ESP_OK) ? "SUBSCRIBED" : 
                                   (wdtStatus == ESP_ERR_NOT_FOUND) ? "NOT SUBSCRIBED" : "ERROR";
            
            ESP_LOGI(WDT_TRACE_TAG, "  Task: %-16s Handle: %p WDT: %s", 
                     pxTaskStatusArray[i].pcTaskName, 
                     pxTaskStatusArray[i].xHandle,
                     statusStr);
        }
        
        vPortFree(pxTaskStatusArray);
    }
    
    ESP_LOGI(WDT_TRACE_TAG, "===================================");
}