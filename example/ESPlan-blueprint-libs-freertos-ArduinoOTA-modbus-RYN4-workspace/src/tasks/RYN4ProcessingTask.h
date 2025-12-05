// RYN4ProcessingTask.h
#ifndef RYN4_PROCESSING_TASK_H
#define RYN4_PROCESSING_TASK_H

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "RYN4.h"

/**
 * @brief Processing task for RYN4 async queue
 * 
 * This task processes queued Modbus responses for the RYN4 device.
 * It runs continuously and handles packets from the device's internal queue.
 */
class RYN4ProcessingTask {
public:
    static bool init(RYN4* device);
    static bool start();
    static void stop();
    static bool isRunning() { return running; }
    static TaskHandle_t getTaskHandle() { return taskHandle; }
    
    // Task statistics
    static uint32_t getProcessedPackets() { return packetsProcessed; }
    static uint32_t getDroppedPackets() { return packetsDropped; }
    static void resetStats();

private:
    static void task(void* pvParameters);
    
    static RYN4* ryn4Device;
    static TaskHandle_t taskHandle;
    static bool initialized;
    static bool running;
    
    // Statistics
    static uint32_t packetsProcessed;
    static uint32_t packetsDropped;
    static TickType_t lastProcessTime;
};

#endif // RYN4_PROCESSING_TASK_H