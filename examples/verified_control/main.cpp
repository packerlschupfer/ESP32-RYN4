/**
 * @file verified_control.cpp
 * @brief Example demonstrating RYN4 verified relay control methods
 * 
 * This example shows how to use the new public verified methods that
 * automatically confirm relay state changes without manual verification.
 * 
 * NOTE: Requires RYN4 library version with status read fix (0x0001 vs 0x0100)
 */

#include <Arduino.h>
#include <RYN4.h>
#include <esp32ModbusRTU.h>

// Hardware configuration
#define RX_PIN 16
#define TX_PIN 17
#define RTS_PIN -1  // Not used
#define RYN4_SLAVE_ID 1

// Create Modbus RTU instance
esp32ModbusRTU modbus(&Serial2, RTS_PIN);

// Create RYN4 instance
RYN4* ryn4 = nullptr;

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n=== RYN4 Verified Control Example ===");
    
    // Initialize Serial2 for Modbus
    Serial2.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
    
    // Start Modbus RTU
    modbus.begin();
    
    // Create RYN4 instance
    ryn4 = new RYN4(RYN4_SLAVE_ID, "RYN4", 5);
    if (!ryn4) {
        Serial.println("Failed to create RYN4 instance!");
        while (1) { delay(1000); }
    }
    
    // Initialize RYN4
    Serial.println("Initializing RYN4...");
    auto result = ryn4->initialize();
    if (result.error != IDeviceInstance::DeviceError::SUCCESS) {
        Serial.println("Failed to initialize RYN4!");
        while (1) { delay(1000); }
    }
    
    // Wait for initialization to complete
    ryn4->waitForInitializationComplete(pdMS_TO_TICKS(5000));
    
    Serial.println("RYN4 initialized successfully!");
    Serial.println("\nDemonstrating verified relay control methods...\n");
}

void loop() {
    static uint32_t lastDemo = 0;
    static uint8_t demoStep = 0;
    
    if (millis() - lastDemo > 3000) {
        lastDemo = millis();
        
        switch (demoStep) {
            case 0: {
                Serial.println("1. Single relay control with verification:");
                Serial.println("   Turning ON relay 1...");
                
                // Old way (manual verification)
                // ryn4->controlRelay(1, ryn4::RelayAction::CLOSE);
                // delay(100);
                // bool state;
                // ryn4->readRelayStatus(1, state);
                // if (state) Serial.println("   Relay 1 is ON");
                
                // New way (automatic verification)
                auto result = ryn4->controlRelayVerified(1, ryn4::RelayAction::CLOSE);
                if (result == ryn4::RelayErrorCode::SUCCESS) {
                    Serial.println("   ✓ Relay 1 successfully turned ON (verified)");
                } else {
                    Serial.printf("   ✗ Failed to turn ON relay 1, error: %d\n", 
                                 static_cast<int>(result));
                }
                break;
            }
            
            case 1: {
                Serial.println("\n2. Convenience method for ON/OFF:");
                Serial.println("   Turning OFF relay 1...");
                
                // Using setRelayStateVerified for simple ON/OFF
                auto result = ryn4->setRelayStateVerified(1, false);
                if (result == ryn4::RelayErrorCode::SUCCESS) {
                    Serial.println("   ✓ Relay 1 successfully turned OFF (verified)");
                } else {
                    Serial.printf("   ✗ Failed to turn OFF relay 1, error: %d\n", 
                                 static_cast<int>(result));
                }
                break;
            }
            
            case 2: {
                Serial.println("\n3. Multiple relay control with verification:");
                Serial.println("   Setting relays 1-4 to pattern ON,OFF,ON,OFF...");
                
                std::vector<bool> pattern = {true, false, true, false, false, false, false, false};
                auto result = ryn4->setMultipleRelayStatesVerified(pattern);
                if (result == ryn4::RelayErrorCode::SUCCESS) {
                    Serial.println("   ✓ All relays successfully set to pattern (verified)");
                } else {
                    Serial.printf("   ✗ Failed to set relay pattern, error: %d\n", 
                                 static_cast<int>(result));
                }
                break;
            }
            
            case 3: {
                Serial.println("\n4. Emergency shutdown (all relays OFF):");
                Serial.println("   Turning OFF all relays...");
                
                auto result = ryn4->setAllRelaysVerified(false);
                if (result == ryn4::RelayErrorCode::SUCCESS) {
                    Serial.println("   ✓ All relays successfully turned OFF (verified)");
                } else {
                    Serial.printf("   ✗ Failed to turn OFF all relays, error: %d\n", 
                                 static_cast<int>(result));
                }
                break;
            }
            
            case 4: {
                Serial.println("\n5. Toggle with verification:");
                Serial.println("   Toggling relay 2...");
                
                auto result = ryn4->controlRelayVerified(2, ryn4::RelayAction::TOGGLE);
                if (result == ryn4::RelayErrorCode::SUCCESS) {
                    Serial.println("   ✓ Relay 2 successfully toggled (verified)");
                } else {
                    Serial.printf("   ✗ Failed to toggle relay 2, error: %d\n", 
                                 static_cast<int>(result));
                }
                break;
            }
            
            case 5: {
                Serial.println("\n6. Demonstrating error handling:");
                Serial.println("   Attempting to control invalid relay 9...");
                
                auto result = ryn4->controlRelayVerified(9, ryn4::RelayAction::CLOSE);
                switch (result) {
                    case ryn4::RelayErrorCode::SUCCESS:
                        Serial.println("   Unexpected success?!");
                        break;
                    case ryn4::RelayErrorCode::INVALID_INDEX:
                        Serial.println("   ✓ Correctly returned INVALID_INDEX error");
                        break;
                    default:
                        Serial.printf("   Got error code: %d\n", static_cast<int>(result));
                        break;
                }
                
                Serial.println("\n=== Demo complete, restarting... ===\n");
                break;
            }
        }
        
        demoStep++;
        if (demoStep > 5) {
            demoStep = 0;
        }
    }
    
    delay(10);
}