/**
 * @file test_communication.ino
 * @brief RYN4 hardware communication test sketch
 *
 * This sketch verifies basic communication with the RYN4 relay module.
 * Use this to confirm proper hardware configuration before integration.
 *
 * Hardware Configuration Required:
 * - DIP switches A0-A5 set for desired slave ID
 * - M0 jumper OPEN (Modbus RTU mode)
 * - M1/M2 jumpers match baud rate selection
 * - RS485 connected: A-to-A, B-to-B
 * - 12-24V power supply connected
 *
 * See examples/hardware_setup/README.md for detailed setup instructions.
 */

#include <RYN4.h>

// ========== Configuration ==========
// Adjust these to match your hardware DIP switch and jumper settings

const uint8_t SLAVE_ID = 1;        // Match your DIP switches (A0-A5)
const uint32_t BAUD_RATE = 9600;   // Match your M1/M2 jumpers (2400/4800/9600/19200)

// RS485 pins (adjust if using different GPIOs)
const int RS485_RX_PIN = 17;       // GPIO17 → RS485 RO
const int RS485_TX_PIN = 18;       // GPIO18 → RS485 DI
const int RS485_DE_PIN = 16;       // GPIO16 → RS485 DE/RE

// Test configuration
const int TEST_DELAY_MS = 1000;    // Delay between relay toggles
const int NUM_CHANNELS = 8;        // Test first 8 channels (use 4 for RYN404E)

// ========== Global Objects ==========

RYN4* ryn4 = nullptr;

// ========== Helper Functions ==========

/**
 * @brief Print DIP switch configuration for given slave ID
 */
void printDipSwitchConfig(uint8_t slaveId) {
    Serial.println("\n=== DIP Switch Configuration ===");
    Serial.printf("Slave ID: 0x%02X (%d decimal)\n\n", slaveId, slaveId);
    Serial.println("Switch positions for A0-A5:");
    Serial.printf("  A0 (bit 0, value 1):  %s\n", (slaveId & 0x01) ? "ON " : "OFF");
    Serial.printf("  A1 (bit 1, value 2):  %s\n", (slaveId & 0x02) ? "ON " : "OFF");
    Serial.printf("  A2 (bit 2, value 4):  %s\n", (slaveId & 0x04) ? "ON " : "OFF");
    Serial.printf("  A3 (bit 3, value 8):  %s\n", (slaveId & 0x08) ? "ON " : "OFF");
    Serial.printf("  A4 (bit 4, value 16): %s\n", (slaveId & 0x10) ? "ON " : "OFF");
    Serial.printf("  A5 (bit 5, value 32): %s\n", (slaveId & 0x20) ? "ON " : "OFF");
    Serial.println("================================\n");
}

/**
 * @brief Print jumper configuration for baud rate
 */
void printJumperConfig(uint32_t baudRate) {
    Serial.println("=== Jumper Configuration ===");
    Serial.printf("Baud Rate: %lu\n\n", baudRate);

    Serial.println("Required jumper settings:");
    switch (baudRate) {
        case 9600:
            Serial.println("  M0: OPEN  (Modbus RTU mode)");
            Serial.println("  M1: OPEN  (9600 baud)");
            Serial.println("  M2: OPEN  (9600 baud)");
            break;
        case 2400:
            Serial.println("  M0: OPEN  (Modbus RTU mode)");
            Serial.println("  M1: SHORT (2400 baud)");
            Serial.println("  M2: OPEN  (2400 baud)");
            break;
        case 4800:
            Serial.println("  M0: OPEN  (Modbus RTU mode)");
            Serial.println("  M1: OPEN  (4800 baud)");
            Serial.println("  M2: SHORT (4800 baud)");
            break;
        case 19200:
            Serial.println("  M0: OPEN  (Modbus RTU mode)");
            Serial.println("  M1: SHORT (19200 baud)");
            Serial.println("  M2: SHORT (19200 baud)");
            break;
        default:
            Serial.println("  WARNING: Unsupported baud rate!");
            Serial.println("  Use 2400, 4800, 9600, or 19200");
            break;
    }
    Serial.println("============================\n");
}

/**
 * @brief Test individual relay ON/OFF
 */
bool testRelay(uint8_t relayNum) {
    Serial.printf("Testing Relay %d...\n", relayNum);

    // Turn ON
    Serial.printf("  Turning ON... ");
    auto result = ryn4->controlRelayVerified(relayNum, ryn4::RelayAction::OPEN);
    if (result != ryn4::RelayErrorCode::SUCCESS) {
        Serial.printf("FAILED (error: %d)\n", static_cast<int>(result));
        return false;
    }
    Serial.println("OK");
    delay(TEST_DELAY_MS);

    // Turn OFF
    Serial.printf("  Turning OFF... ");
    result = ryn4->controlRelayVerified(relayNum, ryn4::RelayAction::CLOSE);
    if (result != ryn4::RelayErrorCode::SUCCESS) {
        Serial.printf("FAILED (error: %d)\n", static_cast<int>(result));
        return false;
    }
    Serial.println("OK");
    delay(TEST_DELAY_MS);

    return true;
}

/**
 * @brief Read and display all relay states
 */
void displayRelayStates() {
    Serial.println("\n=== Current Relay States ===");

    for (int i = 1; i <= NUM_CHANNELS; i++) {
        bool state;
        auto result = ryn4->readRelayStatus(i, state);

        if (result == ryn4::RelayErrorCode::SUCCESS) {
            Serial.printf("Relay %d: %s\n", i, state ? "ON " : "OFF");
        } else {
            Serial.printf("Relay %d: ERROR (code %d)\n", i, static_cast<int>(result));
        }
    }
    Serial.println("============================\n");
}

// ========== Arduino Setup ==========

void setup() {
    // Initialize serial monitor
    Serial.begin(115200);
    delay(1000);  // Wait for serial monitor to open

    Serial.println("\n\n");
    Serial.println("=====================================");
    Serial.println("  RYN4 Hardware Communication Test");
    Serial.println("=====================================\n");

    // Display configuration
    printDipSwitchConfig(SLAVE_ID);
    printJumperConfig(BAUD_RATE);

    // Initialize RS485 (if your library requires it)
    // Note: Some setups handle this automatically
    Serial.println("Initializing RS485 interface...");
    Serial.printf("  RX Pin: GPIO%d\n", RS485_RX_PIN);
    Serial.printf("  TX Pin: GPIO%d\n", RS485_TX_PIN);
    Serial.printf("  DE Pin: GPIO%d\n", RS485_DE_PIN);
    Serial.println();

    // Create RYN4 instance
    Serial.printf("Creating RYN4 instance (Slave ID: %d)...\n", SLAVE_ID);
    ryn4 = new RYN4(SLAVE_ID, "RYN4-Test");

    if (!ryn4) {
        Serial.println("ERROR: Failed to create RYN4 instance!");
        Serial.println("Check memory/heap availability");
        while (1) {
            delay(1000);
        }
    }
    Serial.println("OK\n");

    // Initialize RYN4
    Serial.println("Initializing RYN4 module...");
    Serial.println("(This will reset all relays to OFF)");

    auto initResult = ryn4->initialize();

    if (initResult.isError()) {
        Serial.printf("ERROR: Initialization failed (code: %d)\n",
                     static_cast<int>(initResult.error()));
        Serial.println("\nTroubleshooting steps:");
        Serial.println("1. Check DIP switch settings match SLAVE_ID");
        Serial.println("2. Verify M1/M2 jumpers match BAUD_RATE");
        Serial.println("3. Ensure M0 is OPEN (Modbus RTU mode)");
        Serial.println("4. Check RS485 wiring (A-to-A, B-to-B)");
        Serial.println("5. Verify 12-24V power supply connected");
        Serial.println("6. Try swapping A and B connections");
        while (1) {
            delay(1000);
        }
    }

    Serial.println("✓ Initialization successful!\n");

    // Check if module is responsive
    if (ryn4->isModuleOffline()) {
        Serial.println("WARNING: Module detected as offline during init");
        Serial.println("Communication may be unreliable");
        Serial.println();
    } else {
        Serial.println("✓ Module is online and responsive\n");
    }

    // Read and display device information
    Serial.println("Reading device information...");
    auto deviceInfo = ryn4->readDeviceInfo();

    if (deviceInfo.isOk()) {
        Serial.println("\n--- Device Information ---");
        Serial.printf("  Device Type: 0x%04X\n", deviceInfo.value.deviceType);
        Serial.printf("  Firmware: v%d.%d\n",
                     deviceInfo.value.firmwareMajor,
                     deviceInfo.value.firmwareMinor);
        Serial.printf("  Configured Address: %d (from DIP switches)\n",
                     deviceInfo.value.configuredAddress);
        Serial.printf("  Configured Baud Rate: %lu BPS (from jumpers)\n",
                     deviceInfo.value.configuredBaudRate);

        const char* parityName[] = {"None", "Even", "Odd"};
        if (deviceInfo.value.configuredParity <= 2) {
            Serial.printf("  Parity: %s\n", parityName[deviceInfo.value.configuredParity]);
        } else {
            Serial.printf("  Parity: Unknown (%d)\n", deviceInfo.value.configuredParity);
        }

        Serial.printf("  Reply Delay: %dms\n", deviceInfo.value.replyDelayMs);
        Serial.println("--------------------------\n");

        // Verify configuration matches
        if (deviceInfo.value.configuredAddress != SLAVE_ID) {
            Serial.println("⚠ WARNING: DIP switch address doesn't match SLAVE_ID!");
            Serial.printf("  Expected: %d, DIP switches set to: %d\n",
                         SLAVE_ID, deviceInfo.value.configuredAddress);
            Serial.println();
        } else {
            Serial.println("✓ DIP switch address matches software configuration\n");
        }
    } else {
        Serial.printf("⚠ Could not read device info (error: %d)\n",
                     static_cast<int>(deviceInfo.error()));
        Serial.println();
    }

    // Display initial relay states
    displayRelayStates();

    // Run tests
    Serial.println("=====================================");
    Serial.println("  Starting Relay Tests");
    Serial.println("  (You should hear relays clicking)");
    Serial.println("=====================================\n");

    bool allPassed = true;

    // Test each relay
    for (int i = 1; i <= NUM_CHANNELS; i++) {
        if (!testRelay(i)) {
            allPassed = false;
            Serial.printf("⚠ Relay %d test FAILED\n\n", i);
        } else {
            Serial.printf("✓ Relay %d test PASSED\n\n", i);
        }
    }

    // Display final states
    displayRelayStates();

    // Summary
    Serial.println("=====================================");
    if (allPassed) {
        Serial.println("  ✓ ALL TESTS PASSED");
        Serial.println("  Hardware is properly configured!");
    } else {
        Serial.println("  ⚠ SOME TESTS FAILED");
        Serial.println("  Check relay connections and power");
    }
    Serial.println("=====================================\n");

    Serial.println("Test complete. See examples/verified_control for");
    Serial.println("production usage patterns.\n");
}

// ========== Arduino Loop ==========

void loop() {
    // Test completed in setup()
    // You can add continuous monitoring here if desired
    delay(1000);
}

/**
 * Additional test functions you can uncomment and try:
 */

/*
// Test batch relay control
void testBatchControl() {
    Serial.println("\n=== Testing Batch Control ===");

    // Turn all relays ON
    std::array<bool, 8> allOn = {true, true, true, true, true, true, true, true};
    auto result = ryn4->setMultipleRelayStatesVerified(allOn);
    Serial.printf("All ON: %s\n", result == ryn4::RelayErrorCode::SUCCESS ? "OK" : "FAILED");
    delay(2000);

    // Turn all relays OFF
    std::array<bool, 8> allOff = {false, false, false, false, false, false, false, false};
    result = ryn4->setMultipleRelayStatesVerified(allOff);
    Serial.printf("All OFF: %s\n", result == ryn4::RelayErrorCode::SUCCESS ? "OK" : "FAILED");
    delay(2000);

    // Alternating pattern
    std::array<bool, 8> pattern = {true, false, true, false, true, false, true, false};
    result = ryn4->setMultipleRelayStatesVerified(pattern);
    Serial.printf("Pattern: %s\n", result == ryn4::RelayErrorCode::SUCCESS ? "OK" : "FAILED");
    delay(2000);

    // All OFF
    result = ryn4->setMultipleRelayStatesVerified(allOff);
    Serial.printf("Cleanup: %s\n", result == ryn4::RelayErrorCode::SUCCESS ? "OK" : "FAILED");
}
*/

/*
// Test momentary mode (1-second pulse)
void testMomentary() {
    Serial.println("\n=== Testing Momentary Mode ===");
    Serial.println("Relay will pulse ON for 1 second...");

    auto result = ryn4->controlRelay(1, ryn4::RelayAction::MOMENTARY);
    Serial.printf("Momentary command: %s\n",
                 result == ryn4::RelayErrorCode::SUCCESS ? "OK" : "FAILED");

    delay(2000);  // Wait for pulse to complete
}
*/

/*
// Test delay mode (ON for N seconds, then auto-OFF)
void testDelay() {
    Serial.println("\n=== Testing Delay Mode ===");
    Serial.println("Relay will turn ON for 5 seconds...");

    // Note: Use raw Modbus command for delay
    // Delay command: 0x06XX where XX = seconds
    // Library may need specific API for this

    Serial.println("(Delay test requires raw Modbus - see HARDWARE.md)");
}
*/
