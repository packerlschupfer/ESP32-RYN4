/**
 * @file basic_relay_control.cpp
 * @brief Basic example of using the RYN4 library to control relays
 * 
 * This example demonstrates:
 * - Basic initialization
 * - Individual relay control
 * - Batch relay control
 * - State verification
 * - Error handling
 */

#include <Arduino.h>
#include <RYN4.h>
#include <Logger.h>
#include <ConsoleBackend.h>

// Create logger instance
Logger logger(std::make_shared<ConsoleBackend>());

// Modbus configuration
#define MODBUS_SLAVE_ID 2
#define MODBUS_RX_PIN 16
#define MODBUS_TX_PIN 17
#define MODBUS_BAUD_RATE 9600

// Create global instances
esp32ModbusRTU modbusMaster(&Serial1);
RYN4* relayController = nullptr;

void setup() {
    // Initialize serial for debugging
    Serial.begin(115200);
    while (!Serial) {
        delay(10);
    }
    
    Serial.println("RYN4 Basic Example Starting...");
    
    // Initialize logger
    logger.setTag("RYN4_EXAMPLE");
    logger.setLogLevel(ESP_LOG_INFO);
    
    // Initialize Modbus serial
    Serial1.begin(MODBUS_BAUD_RATE, SERIAL_8N1, MODBUS_RX_PIN, MODBUS_TX_PIN);
    delay(100); // Allow serial to stabilize
    
    // Create RYN4 instance
    relayController = new RYN4(MODBUS_SLAVE_ID, modbusMaster, "RELAY");
    
    // Initialize the relay controller
    logger.log(ESP_LOG_INFO, "MAIN", "Initializing RYN4 relay controller...");
    relayController->initialize();
    
    // Wait for initialization to complete
    if (relayController->waitForInitializationComplete(pdMS_TO_TICKS(5000))) {
        logger.log(ESP_LOG_INFO, "MAIN", "RYN4 initialized successfully!");
        
        // Print module settings
        const auto& settings = relayController->getModuleSettings();
        logger.log(ESP_LOG_INFO, "MAIN", "Module Settings:");
        logger.log(ESP_LOG_INFO, "MAIN", "  RS485 Address: 0x%02X", settings.rs485Address);
        logger.log(ESP_LOG_INFO, "MAIN", "  Baud Rate: %d", settings.baudRate);
        logger.log(ESP_LOG_INFO, "MAIN", "  Parity: %d", settings.parity);
        logger.log(ESP_LOG_INFO, "MAIN", "  Return Delay: %d units", settings.returnDelay);
    } else {
        logger.log(ESP_LOG_ERROR, "MAIN", "RYN4 initialization failed!");
    }
    
    // Example 1: Control individual relays
    logger.log(ESP_LOG_INFO, "MAIN", "\n=== Example 1: Individual Relay Control ===");
    
    // Turn on relay 1
    auto result = relayController->controlRelay(1, RYN4::RelayAction::OPEN);
    if (result == ryn4::RelayErrorCode::SUCCESS) {
        logger.log(ESP_LOG_INFO, "MAIN", "Relay 1 turned ON");
    } else {
        logger.log(ESP_LOG_ERROR, "MAIN", "Failed to control relay 1: %d", static_cast<int>(result));
    }
    
    delay(1000);
    
    // Turn off relay 1
    result = relayController->controlRelay(1, RYN4::RelayAction::CLOSE);
    if (result == ryn4::RelayErrorCode::SUCCESS) {
        logger.log(ESP_LOG_INFO, "MAIN", "Relay 1 turned OFF");
    }
    
    delay(1000);
    
    // Toggle relay 2
    result = relayController->controlRelay(2, RYN4::RelayAction::TOGGLE);
    if (result == ryn4::RelayErrorCode::SUCCESS) {
        logger.log(ESP_LOG_INFO, "MAIN", "Relay 2 toggled");
    }
    
    delay(1000);
    
    // Example 2: Batch relay control
    logger.log(ESP_LOG_INFO, "MAIN", "\n=== Example 2: Batch Relay Control ===");
    
    // Set relays 1,3,5,7 ON and 2,4,6,8 OFF
    std::vector<bool> pattern1 = {true, false, true, false, true, false, true, false};
    result = relayController->setMultipleRelayStates(pattern1);
    if (result == ryn4::RelayErrorCode::SUCCESS) {
        logger.log(ESP_LOG_INFO, "MAIN", "Pattern 1 set: 10101010");
    }
    
    delay(2000);
    
    // Reverse the pattern
    std::vector<bool> pattern2 = {false, true, false, true, false, true, false, true};
    result = relayController->setMultipleRelayStates(pattern2);
    if (result == ryn4::RelayErrorCode::SUCCESS) {
        logger.log(ESP_LOG_INFO, "MAIN", "Pattern 2 set: 01010101");
    }
    
    delay(2000);
    
    // Example 3: Verified relay control
    logger.log(ESP_LOG_INFO, "MAIN", "\n=== Example 3: Verified Relay Control ===");
    
    // Control relay with verification
    result = relayController->controlRelayVerified(3, RYN4::RelayAction::OPEN);
    if (result == ryn4::RelayErrorCode::SUCCESS) {
        logger.log(ESP_LOG_INFO, "MAIN", "Relay 3 turned ON and verified");
    } else {
        logger.log(ESP_LOG_ERROR, "MAIN", "Relay 3 verification failed: %d", static_cast<int>(result));
    }
    
    delay(1000);
    
    // Example 4: Read relay status
    logger.log(ESP_LOG_INFO, "MAIN", "\n=== Example 4: Reading Relay Status ===");
    
    // Read all relay status
    result = relayController->readAllRelayStatus();
    if (result == ryn4::RelayErrorCode::SUCCESS) {
        // Wait for data
        if (relayController->waitForData()) {
            // Get relay data
            auto dataResult = relayController->getData(IDeviceInstance::DeviceDataType::RELAY_STATUS);
            if (dataResult.success) {
                const uint8_t* states = static_cast<const uint8_t*>(dataResult.data);
                logger.log(ESP_LOG_INFO, "MAIN", "Current relay states:");
                for (int i = 0; i < 8; i++) {
                    logger.log(ESP_LOG_INFO, "MAIN", "  Relay %d: %s", i + 1, states[i] ? "ON" : "OFF");
                }
            }
        }
    }
    
    // Example 5: Using RelayControlModule
    logger.log(ESP_LOG_INFO, "MAIN", "\n=== Example 5: Using RelayControlModule ===");
    
    // Note: RelayControlModule usage would go here if needed
    // RelayControlModule controlModule(relayController);
    // controlModule.processRelayCommand(1, "on");
    // controlModule.processBatchCommand("11001100");
    
    logger.log(ESP_LOG_INFO, "MAIN", "\n=== Setup Complete ===");
}

void loop() {
    static unsigned long lastToggle = 0;
    static uint8_t currentRelay = 1;
    
    // Toggle through relays every 5 seconds
    if (millis() - lastToggle > 5000) {
        lastToggle = millis();
        
        // Toggle current relay
        auto result = relayController->controlRelay(currentRelay, RYN4::RelayAction::TOGGLE);
        if (result == ryn4::RelayErrorCode::SUCCESS) {
            logger.log(ESP_LOG_INFO, "MAIN", "Toggled relay %d", currentRelay);
        }
        
        // Move to next relay
        currentRelay++;
        if (currentRelay > 8) {
            currentRelay = 1;
        }
    }
    
    // Process any pending Modbus responses
    delay(10);
}

// Error handler for demonstration
void handleRelayError(ryn4::RelayErrorCode error) {
    switch (error) {
        case ryn4::RelayErrorCode::SUCCESS:
            // No error
            break;
        case ryn4::RelayErrorCode::INVALID_INDEX:
            logger.log(ESP_LOG_ERROR, "MAIN", "Invalid relay index specified");
            break;
        case ryn4::RelayErrorCode::MODBUS_ERROR:
            logger.log(ESP_LOG_ERROR, "MAIN", "Modbus communication error");
            break;
        case ryn4::RelayErrorCode::TIMEOUT:
            logger.log(ESP_LOG_ERROR, "MAIN", "Operation timeout");
            break;
        case ryn4::RelayErrorCode::MUTEX_ERROR:
            logger.log(ESP_LOG_ERROR, "MAIN", "Mutex acquisition failed");
            break;
        case ryn4::RelayErrorCode::UNKNOWN_ERROR:
            logger.log(ESP_LOG_ERROR, "MAIN", "Unknown error occurred");
            break;
    }
}