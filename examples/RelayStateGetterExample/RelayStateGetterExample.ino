/**
 * @file RelayStateGetterExample.ino
 * @brief Example demonstrating the use of relay state getter methods in RYN4
 * 
 * This example shows how to use the new getRelayState() and getAllRelayStates()
 * methods to query the current state of relays without maintaining separate
 * cached state.
 */

#include <Arduino.h>
#include <RYN4.h>
#include <Logger.h>

// Hardware configuration
#define RYN4_UART Serial2
#define RYN4_TX_PIN 17
#define RYN4_RX_PIN 16
#define RYN4_MODBUS_ADDRESS 0x01

// Create RYN4 instance
RYN4 ryn4Device(RYN4_MODBUS_ADDRESS, "RYN4-Example");

void setup() {
    Serial.begin(115200);
    
    // Initialize logger
    auto& logger = Logger::getInstance();
    logger.setLogLevel(ESP_LOG_INFO);
    logger.setLogFilter("+RYN4");
    
    Serial.println("\n=== RYN4 Relay State Getter Example ===");
    
    // Initialize UART for Modbus communication
    RYN4_UART.begin(9600, SERIAL_8N1, RYN4_RX_PIN, RYN4_TX_PIN);
    
    // Initialize RYN4 device
    esp_err_t result = ryn4Device.begin(&RYN4_UART);
    if (result != ESP_OK) {
        Serial.println("Failed to initialize RYN4 device!");
        return;
    }
    
    // Wait for initialization to complete
    Serial.println("Waiting for RYN4 initialization...");
    auto initResult = ryn4Device.waitForModuleInitComplete(pdMS_TO_TICKS(5000));
    if (!initResult.isOk()) {
        Serial.println("RYN4 initialization timeout!");
        return;
    }
    
    Serial.println("RYN4 initialized successfully!");
    
    // Set a test pattern
    Serial.println("\nSetting test pattern: ON, OFF, ON, OFF, ON, OFF, ON, ON");
    std::vector<bool> pattern = {true, false, true, false, true, false, true, true};
    auto setResult = ryn4Device.setMultipleRelayStatesVerified(pattern);
    if (setResult == ryn4::RelayErrorCode::SUCCESS) {
        Serial.println("Test pattern set successfully!");
    } else {
        Serial.println("Failed to set test pattern!");
    }
    
    delay(1000);  // Give relays time to switch
}

void loop() {
    Serial.println("\n--- Reading Relay States ---");
    
    // Method 1: Get individual relay states
    Serial.println("Individual relay states:");
    for (uint8_t i = 1; i <= 8; i++) {
        auto result = ryn4Device.getRelayState(i);
        if (result.isOk()) {
            Serial.printf("  Relay %d: %s\n", i, result.value ? "ON" : "OFF");
        } else {
            Serial.printf("  Relay %d: ERROR (code: %d)\n", i, 
                         static_cast<int>(result.error));
        }
    }
    
    // Method 2: Get all relay states at once
    Serial.println("\nAll relay states (single call):");
    auto allStates = ryn4Device.getAllRelayStates();
    if (allStates.isOk()) {
        Serial.print("  States: [");
        for (size_t i = 0; i < allStates.value.size(); i++) {
            Serial.print(allStates.value[i] ? "ON" : "OFF");
            if (i < allStates.value.size() - 1) Serial.print(", ");
        }
        Serial.println("]");
        
        // Count active relays
        int activeCount = 0;
        for (bool state : allStates.value) {
            if (state) activeCount++;
        }
        Serial.printf("  Active relays: %d/8\n", activeCount);
    } else {
        Serial.printf("  ERROR: Failed to get all states (code: %d)\n", 
                     static_cast<int>(allStates.error));
    }
    
    // Demonstrate error handling
    Serial.println("\nError handling demonstration:");
    auto invalidResult = ryn4Device.getRelayState(0);  // Invalid index
    if (!invalidResult.isOk()) {
        Serial.printf("  getRelayState(0) correctly returned error: %d\n", 
                     static_cast<int>(invalidResult.error));
    }
    
    // Example: Using getter in a HAL implementation
    Serial.println("\nExample HAL usage:");
    for (uint8_t channel = 0; channel < 8; channel++) {
        // Convert 0-based channel to 1-based relay index
        auto result = ryn4Device.getRelayState(channel + 1);
        
        const char* stateStr;
        if (result.isOk()) {
            stateStr = result.value ? "ON" : "OFF";
        } else {
            stateStr = "UNKNOWN";
        }
        
        Serial.printf("  Channel %d state: %s\n", channel, stateStr);
    }
    
    // Toggle relay 1 and verify state change
    Serial.println("\nToggling relay 1...");
    auto currentState = ryn4Device.getRelayState(1);
    if (currentState.isOk()) {
        bool newState = !currentState.value;
        auto toggleResult = ryn4Device.setRelayStateVerified(1, newState);
        if (toggleResult == ryn4::RelayErrorCode::SUCCESS) {
            auto verifyState = ryn4Device.getRelayState(1);
            if (verifyState.isOk()) {
                Serial.printf("  Relay 1 toggled: %s -> %s\n", 
                             currentState.value ? "ON" : "OFF",
                             verifyState.value ? "ON" : "OFF");
            }
        }
    }
    
    delay(5000);  // Wait 5 seconds before next read
}