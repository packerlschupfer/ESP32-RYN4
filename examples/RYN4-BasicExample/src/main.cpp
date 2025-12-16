/**
 * @file main.cpp
 * @brief RYN4 Basic Example - Simple relay control demonstration
 *
 * This example demonstrates basic usage of the RYN4 library:
 * - Modbus RTU setup with correct parity (8E1)
 * - RYN4 initialization
 * - Individual relay control (ON/OFF)
 * - Reading relay states
 * - Timed relay operations (DELAY command)
 *
 * Hardware Requirements:
 * - ESP32 development board
 * - RYN404E (4-channel) or RYN408F (8-channel) relay module
 * - RS485 transceiver (e.g., MAX485)
 * - Connections: TX->DI, RX->RO, DE/RE directly driven by esp32ModbusRTU
 */

#include <Arduino.h>
#include <esp32ModbusRTU.h>
#include <ModbusRegistry.h>
#include <ModbusDevice.h>
#include <RYN4.h>

// =============================================================================
// Configuration
// =============================================================================

// Modbus RTU pins - adjust for your hardware
#define MODBUS_RX_PIN       16      // RO pin of MAX485
#define MODBUS_TX_PIN       17      // DI pin of MAX485
#define MODBUS_BAUD_RATE    9600
#define MODBUS_CONFIG       SERIAL_8N1  // 8 data bits, No parity, 1 stop bit (factory default)

// RYN4 configuration
#define RYN4_ADDRESS        0x01    // Modbus slave address (set by DIP switches)

// Test timing
#define TEST_DELAY_MS       2000    // Delay between test steps

// =============================================================================
// Global Objects
// =============================================================================

esp32ModbusRTU modbusMaster(&Serial1);
RYN4* relay = nullptr;

// =============================================================================
// Modbus Callbacks (required for response routing)
// =============================================================================

// Forward declarations from ModbusDevice.h
extern void mainHandleData(uint8_t serverAddress, esp32Modbus::FunctionCode fc,
                          uint16_t startingAddress, const uint8_t* data, size_t length);
extern void handleError(uint8_t serverAddress, esp32Modbus::Error error);

// =============================================================================
// Helper Functions
// =============================================================================

void printRelayStates() {
    auto result = relay->getData(IDeviceInstance::DeviceDataType::RELAY_STATE);

    if (result.isOk()) {
        Serial.print("Relay States: ");
        for (size_t i = 0; i < result.value().size(); i++) {
            Serial.printf("R%d:%s ", i + 1, result.value()[i] > 0.5f ? "ON" : "OFF");
        }
        Serial.println();
    } else {
        Serial.printf("Failed to read relay states (error: %d)\n",
                     static_cast<int>(result.error()));
    }
}

// =============================================================================
// Setup
// =============================================================================

void setup() {
    // Initialize debug serial
    Serial.begin(115200);
    while (!Serial && millis() < 3000);

    Serial.println("\n========================================");
    Serial.println("RYN4 Basic Example");
    Serial.println("========================================\n");

    // Initialize Modbus RTU on Serial1
    Serial.println("Initializing Modbus RTU...");
    Serial1.begin(MODBUS_BAUD_RATE, MODBUS_CONFIG, MODBUS_RX_PIN, MODBUS_TX_PIN);

    // Register Modbus master with the registry (required by RYN4)
    modbus::ModbusRegistry::getInstance().setModbusRTU(&modbusMaster);

    // Setup Modbus callbacks for response routing
    modbusMaster.onData([](uint8_t serverAddress, esp32Modbus::FunctionCode fc,
                           uint16_t address, const uint8_t* data, size_t length) {
        mainHandleData(serverAddress, fc, address, data, length);
    });

    modbusMaster.onError([](esp32Modbus::Error error) {
        handleError(0xFF, error);
    });

    // Start Modbus on core 1
    modbusMaster.begin(1);
    Serial.println("Modbus RTU initialized (9600 baud, 8N1)");

    // Create RYN4 instance
    Serial.printf("Creating RYN4 device at address 0x%02X...\n", RYN4_ADDRESS);
    relay = new RYN4(RYN4_ADDRESS, "RYN4");

    // Apply default hardware configuration (stored in flash)
    relay->setHardwareConfig(ryn4::DEFAULT_HARDWARE_CONFIG.data());

    // Register device with Modbus registry for response routing
    modbus::ModbusRegistry::getInstance().registerDevice(RYN4_ADDRESS, relay);

    // Initialize the device
    Serial.println("Initializing RYN4...");
    auto initResult = relay->initialize();

    if (initResult.isError()) {
        Serial.printf("ERROR: RYN4 initialization failed (error: %d)\n",
                     static_cast<int>(initResult.error()));
        Serial.println("Check: wiring, power, slave address, baud rate");
        return;
    }

    // Wait for initialization to complete
    auto waitResult = relay->waitForInitializationComplete(pdMS_TO_TICKS(5000));
    if (waitResult.isError()) {
        Serial.println("WARNING: Initialization wait timed out");
    }

    Serial.println("RYN4 initialized successfully!\n");

    // Print initial relay states
    Serial.println("Initial relay states:");
    printRelayStates();
    Serial.println();
}

// =============================================================================
// Main Loop - Demonstration Sequence
// =============================================================================

void loop() {
    static int step = 0;
    static unsigned long lastStepTime = 0;

    if (!relay || !relay->isInitialized()) {
        delay(1000);
        return;
    }

    if (millis() - lastStepTime < TEST_DELAY_MS) {
        return;
    }
    lastStepTime = millis();

    switch (step) {
        case 0:
            Serial.println("=== Test 1: Turn Relay 1 ON ===");
            {
                auto result = relay->turnOnRelay(1);
                Serial.printf("Result: %s\n",
                    result == ryn4::RelayErrorCode::SUCCESS ? "SUCCESS" : "FAILED");
                printRelayStates();
            }
            break;

        case 1:
            Serial.println("\n=== Test 2: Turn Relay 2 ON ===");
            {
                auto result = relay->turnOnRelay(2);
                Serial.printf("Result: %s\n",
                    result == ryn4::RelayErrorCode::SUCCESS ? "SUCCESS" : "FAILED");
                printRelayStates();
            }
            break;

        case 2:
            Serial.println("\n=== Test 3: Turn Relay 1 OFF ===");
            {
                auto result = relay->turnOffRelay(1);
                Serial.printf("Result: %s\n",
                    result == ryn4::RelayErrorCode::SUCCESS ? "SUCCESS" : "FAILED");
                printRelayStates();
            }
            break;

        case 3:
            Serial.println("\n=== Test 4: Toggle Relay 2 ===");
            {
                auto result = relay->toggleRelay(2);
                Serial.printf("Result: %s\n",
                    result == ryn4::RelayErrorCode::SUCCESS ? "SUCCESS" : "FAILED");
                printRelayStates();
            }
            break;

        case 4:
            Serial.println("\n=== Test 5: Turn All Relays ON ===");
            {
                std::array<bool, 8> allOn = {true, true, true, true, true, true, true, true};
                auto result = relay->setMultipleRelayStates(allOn);
                Serial.printf("Result: %s\n",
                    result == ryn4::RelayErrorCode::SUCCESS ? "SUCCESS" : "FAILED");
                printRelayStates();
            }
            break;

        case 5:
            Serial.println("\n=== Test 6: Turn All Relays OFF ===");
            {
                std::array<bool, 8> allOff = {false, false, false, false, false, false, false, false};
                auto result = relay->setMultipleRelayStates(allOff);
                Serial.printf("Result: %s\n",
                    result == ryn4::RelayErrorCode::SUCCESS ? "SUCCESS" : "FAILED");
                printRelayStates();
            }
            break;

        case 6:
            Serial.println("\n=== Test 7: Timed Relay (Relay 1 ON for 5 seconds) ===");
            {
                auto result = relay->turnOnRelayTimed(1, 5);  // 5 second delay
                Serial.printf("Result: %s (relay will turn OFF after 5 seconds)\n",
                    result == ryn4::RelayErrorCode::SUCCESS ? "SUCCESS" : "FAILED");
                printRelayStates();
            }
            break;

        case 7:
            Serial.println("\n=== Test 8: Pattern Test (Relays 1,3,5,7 ON) ===");
            {
                std::array<bool, 8> pattern = {true, false, true, false, true, false, true, false};
                auto result = relay->setMultipleRelayStates(pattern);
                Serial.printf("Result: %s\n",
                    result == ryn4::RelayErrorCode::SUCCESS ? "SUCCESS" : "FAILED");
                printRelayStates();
            }
            break;

        case 8:
            Serial.println("\n=== Cleanup: Turn All Relays OFF ===");
            {
                std::array<bool, 8> allOff = {false, false, false, false, false, false, false, false};
                relay->setMultipleRelayStates(allOff);
                printRelayStates();
            }
            Serial.println("\n========================================");
            Serial.println("Test sequence complete!");
            Serial.println("Restarting in 10 seconds...");
            Serial.println("========================================\n");
            break;

        default:
            // Wait 10 seconds then restart the sequence
            if (step >= 13) {
                step = -1;
                Serial.println("\n--- Restarting test sequence ---\n");
            }
            break;
    }

    step++;
}
