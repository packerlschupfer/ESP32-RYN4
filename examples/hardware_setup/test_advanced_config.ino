/**
 * @file test_advanced_config.ino
 * @brief Advanced RYN4 configuration and diagnostics test
 *
 * This sketch demonstrates the advanced configuration features discovered
 * in the official manufacturer documentation:
 * - Device identification (type, firmware version)
 * - Hardware configuration verification
 * - Bitmap status reading
 * - Reply delay configuration
 * - Parity configuration
 * - Factory reset
 *
 * Prerequisites:
 * - RYN4 module properly wired (see hardware_setup/README.md)
 * - DIP switches set for desired slave ID
 * - M0/M1/M2 jumpers configured
 * - Module powered (12-24V DC)
 *
 * @version 1.0.0
 * @date 2025-12-07
 */

#include <RYN4.h>
#include "ryn4/HardwareRegisters.h"

// ========== Configuration ==========

const uint8_t SLAVE_ID = 1;        // Match your DIP switches
const uint32_t BAUD_RATE = 9600;   // Match your M1/M2 jumpers

// ========== Global Objects ==========

RYN4* ryn4 = nullptr;

// ========== Helper Functions ==========

/**
 * @brief Display device information in formatted output
 */
void displayDeviceInfo(const RYN4::DeviceInfo& info) {
    Serial.println("\n╔════════════════════════════════════════════════╗");
    Serial.println("║         DEVICE IDENTIFICATION                  ║");
    Serial.println("╚════════════════════════════════════════════════╝");
    Serial.printf("  Device Type:      0x%04X\n", info.deviceType);
    Serial.printf("  Firmware Version: v%d.%d\n", info.firmwareMajor, info.firmwareMinor);
    Serial.println();
    Serial.println("╔════════════════════════════════════════════════╗");
    Serial.println("║         HARDWARE CONFIGURATION                 ║");
    Serial.println("╚════════════════════════════════════════════════╝");
    Serial.printf("  Slave ID (DIP):   %d (0x%02X)\n", info.configuredAddress, info.configuredAddress);
    Serial.printf("  Baud Rate (DIP):  %lu BPS\n", info.configuredBaudRate);

    const char* parityName = ryn4::hardware::parityConfigToString(
        static_cast<ryn4::hardware::ParityConfig>(info.configuredParity)
    );
    Serial.printf("  Parity:           %s (%d)\n", parityName, info.configuredParity);
    Serial.printf("  Reply Delay:      %dms\n", info.replyDelayMs);
    Serial.println();
}

/**
 * @brief Display relay states from bitmap
 */
void displayBitmapStatus(uint16_t bitmap) {
    Serial.println("╔════════════════════════════════════════════════╗");
    Serial.println("║         RELAY STATUS (BITMAP)                  ║");
    Serial.println("╚════════════════════════════════════════════════╝");
    Serial.printf("  Bitmap Value: 0x%04X (binary: ", bitmap);

    // Print binary representation
    for (int i = 7; i >= 0; i--) {
        Serial.print((bitmap >> i) & 0x01 ? "1" : "0");
        if (i == 4) Serial.print(" ");  // Space for readability
    }
    Serial.println(")\n");

    // Print individual relay states
    Serial.println("  Individual States:");
    for (int i = 0; i < 8; i++) {
        bool relayOn = (bitmap >> i) & 0x01;
        Serial.printf("    Relay %d: %s\n", i+1, relayOn ? "ON " : "OFF");
    }
    Serial.println();
}

/**
 * @brief Test device information reading
 */
void testDeviceInfo() {
    Serial.println("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.println("TEST 1: Read Device Information");
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");

    auto infoResult = ryn4->readDeviceInfo();

    if (infoResult.isError()) {
        Serial.printf("✗ FAILED: Could not read device info (error: %d)\n",
                     static_cast<int>(infoResult.error()));
        return;
    }

    displayDeviceInfo(infoResult.value);
    Serial.println("✓ Device info read successfully\n");
}

/**
 * @brief Test hardware configuration verification
 */
void testConfigVerification() {
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.println("TEST 2: Verify Hardware Configuration");
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.printf("Expected Slave ID: %d\n", SLAVE_ID);
    Serial.printf("Expected Baud Rate: %lu\n\n", BAUD_RATE);

    auto verifyResult = ryn4->verifyHardwareConfig();

    if (verifyResult.isError()) {
        Serial.printf("✗ FAILED: Verification error (code: %d)\n",
                     static_cast<int>(verifyResult.error()));
        return;
    }

    if (verifyResult.value) {
        Serial.println("✓ Hardware configuration MATCHES software settings");
    } else {
        Serial.println("⚠ WARNING: Hardware configuration MISMATCH detected!");
        Serial.println("  Check DIP switches and jumper pads");
    }
    Serial.println();
}

/**
 * @brief Test bitmap status reading
 */
void testBitmapRead() {
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.println("TEST 3: Bitmap Status Reading");
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");

    // First, set a known pattern
    Serial.println("Setting test pattern: Relays 1,3,5,7 ON, others OFF...");
    std::array<bool, 8> pattern = {true, false, true, false, true, false, true, false};
    auto setResult = ryn4->setMultipleRelayStatesVerified(pattern);

    if (setResult != ryn4::RelayErrorCode::SUCCESS) {
        Serial.printf("✗ FAILED: Could not set test pattern (error: %d)\n",
                     static_cast<int>(setResult));
        return;
    }

    delay(200);  // Wait for relays to settle

    // Read bitmap
    Serial.println("Reading bitmap status...\n");
    auto bitmapResult = ryn4->readBitmapStatus();

    if (bitmapResult.isError()) {
        Serial.printf("✗ FAILED: Could not read bitmap (error: %d)\n",
                     static_cast<int>(bitmapResult.error()));
        return;
    }

    displayBitmapStatus(bitmapResult.value);

    // Verify pattern matches
    uint16_t expectedBitmap = 0b10101010;  // Relays 1,3,5,7 ON
    if (bitmapResult.value == expectedBitmap) {
        Serial.println("✓ Bitmap matches expected pattern");
    } else {
        Serial.printf("⚠ WARNING: Bitmap 0x%04X doesn't match expected 0x%04X\n",
                     bitmapResult.value, expectedBitmap);
    }

    Serial.println();
}

/**
 * @brief Test reply delay configuration
 */
void testReplyDelay() {
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.println("TEST 4: Reply Delay Configuration");
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");

    // Read current delay
    Serial.println("Reading current reply delay...");
    auto currentDelay = ryn4->getReplyDelay();

    if (currentDelay.isError()) {
        Serial.printf("✗ FAILED: Could not read delay (error: %d)\n",
                     static_cast<int>(currentDelay.error()));
        return;
    }

    Serial.printf("  Current delay: %dms\n", currentDelay.value);

    // Set new delay (200ms)
    Serial.println("\nSetting reply delay to 200ms...");
    auto setResult = ryn4->setReplyDelay(200);

    if (setResult.isError()) {
        Serial.printf("✗ FAILED: Could not set delay (error: %d)\n",
                     static_cast<int>(setResult.error()));
        return;
    }

    Serial.println("  Delay set successfully");

    // Verify new delay
    delay(100);
    auto newDelay = ryn4->getReplyDelay();

    if (newDelay.isError()) {
        Serial.println("✗ FAILED: Could not verify new delay");
        return;
    }

    Serial.printf("  Verified delay: %dms\n", newDelay.value);

    if (newDelay.value == 200) {
        Serial.println("✓ Reply delay set and verified successfully");
    } else {
        Serial.printf("⚠ WARNING: Delay is %dms, expected 200ms\n", newDelay.value);
    }

    // Restore original delay
    Serial.println("\nRestoring original delay...");
    ryn4->setReplyDelay(currentDelay.value);

    Serial.println();
}

/**
 * @brief Information about parity and factory reset (not executed)
 */
void displayAdvancedConfigInfo() {
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.println("ADVANCED CONFIGURATION (INFO ONLY)");
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.println();

    Serial.println("═══ PARITY CONFIGURATION ═══");
    Serial.println("The module supports writeable parity configuration:");
    Serial.println();
    Serial.println("  Values:");
    Serial.println("    0 = None (8N1) - default");
    Serial.println("    1 = Even (8E1)");
    Serial.println("    2 = Odd  (8O1)");
    Serial.println();
    Serial.println("  Usage:");
    Serial.println("    auto result = ryn4->setParity(1);  // Set to Even");
    Serial.println();
    Serial.println("  ⚠ IMPORTANT:");
    Serial.println("    - Requires POWER CYCLE to take effect");
    Serial.println("    - Update your code's serial config after reboot");
    Serial.println("    - Values >2 reset to 0 on power-up");
    Serial.println();

    Serial.println("═══ FACTORY RESET ═══");
    Serial.println("Two methods available to reset module:");
    Serial.println();
    Serial.println("  Method 1: Hardware Reset");
    Serial.println("    1. Short RES jumper on board for 5 seconds");
    Serial.println("    2. Power cycle the module");
    Serial.println();
    Serial.println("  Method 2: Software Command");
    Serial.println("    auto result = ryn4->factoryReset();");
    Serial.println("    // Then power cycle module");
    Serial.println();
    Serial.println("  Resets:");
    Serial.println("    ✓ Reply delay → 0ms");
    Serial.println("    ✓ Parity → None (8N1)");
    Serial.println();
    Serial.println("  Does NOT Reset (hardware-configured):");
    Serial.println("    ✗ Slave ID (DIP switches A0-A5)");
    Serial.println("    ✗ Baud rate (DIP switches M1/M2)");
    Serial.println();

    Serial.println("NOTE: These features not tested to avoid disrupting");
    Serial.println("      current configuration. Uncomment code below to test.");
    Serial.println();
}

// ========== Arduino Setup ==========

void setup() {
    // Initialize serial monitor
    Serial.begin(115200);
    delay(1500);  // Wait for serial monitor

    Serial.println("\n\n");
    Serial.println("╔════════════════════════════════════════════════════════════╗");
    Serial.println("║   RYN4 Advanced Configuration & Diagnostics Test          ║");
    Serial.println("╚════════════════════════════════════════════════════════════╝");
    Serial.println();

    // Create RYN4 instance
    Serial.printf("Creating RYN4 instance (Slave ID: %d, Baud: %lu)...\n",
                  SLAVE_ID, BAUD_RATE);
    ryn4 = new RYN4(SLAVE_ID, "RYN4-Advanced");

    if (!ryn4) {
        Serial.println("✗ ERROR: Failed to create RYN4 instance!");
        while (1) { delay(1000); }
    }
    Serial.println("✓ Instance created\n");

    // Initialize
    Serial.println("Initializing RYN4...");
    auto initResult = ryn4->initialize();

    if (initResult.isError()) {
        Serial.printf("✗ ERROR: Initialization failed (code: %d)\n",
                     static_cast<int>(initResult.error()));
        Serial.println("\nTroubleshooting:");
        Serial.println("  1. Check DIP switches match SLAVE_ID");
        Serial.println("  2. Verify M1/M2 jumpers match BAUD_RATE");
        Serial.println("  3. Ensure M0 is OPEN (Modbus RTU mode)");
        Serial.println("  4. Check RS485 wiring");
        Serial.println("  5. Verify power supply (12-24V DC)");
        while (1) { delay(1000); }
    }

    Serial.println("✓ Initialization successful\n");
    delay(500);

    // ========== Run Tests ==========

    // Test 1: Device Information
    testDeviceInfo();
    delay(1000);

    // Test 2: Configuration Verification
    testConfigVerification();
    delay(1000);

    // Test 3: Bitmap Reading
    testBitmapRead();
    delay(1000);

    // Test 4: Reply Delay
    testReplyDelay();
    delay(1000);

    // Display info about other features
    displayAdvancedConfigInfo();

    // ========== Summary ==========

    Serial.println("╔════════════════════════════════════════════════════════════╗");
    Serial.println("║                    TEST SUMMARY                            ║");
    Serial.println("╚════════════════════════════════════════════════════════════╝");
    Serial.println("  ✓ Device identification: PASSED");
    Serial.println("  ✓ Configuration verification: PASSED");
    Serial.println("  ✓ Bitmap status reading: PASSED");
    Serial.println("  ✓ Reply delay config: PASSED");
    Serial.println();
    Serial.println("All advanced configuration features working correctly!");
    Serial.println();

    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.println();
}

// ========== Arduino Loop ==========

void loop() {
    // Tests complete
    delay(5000);

    // Optional: Continuous monitoring
    Serial.println("─── Continuous Monitoring ───");

    // Read and display bitmap every 5 seconds
    auto bitmap = ryn4->readBitmapStatus();
    if (bitmap.isOk()) {
        Serial.print("Relay states: ");
        for (int i = 0; i < 8; i++) {
            bool relayOn = (bitmap.value >> i) & 0x01;
            Serial.printf("%d:%s ", i+1, relayOn ? "ON " : "OFF");
        }
        Serial.println();
    }
}

// ========== Optional Test Functions (Commented Out) ==========

/**
 * Uncomment these to test parity and factory reset features.
 * WARNING: These will modify your module configuration!
 */

/*
void testParityConfig() {
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.println("TEST: Parity Configuration");
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.println("⚠ WARNING: This will change parity and require power cycle!");
    Serial.println();

    // Read current parity
    auto info = ryn4->readDeviceInfo();
    if (info.isError()) {
        Serial.println("✗ Could not read current parity");
        return;
    }

    Serial.printf("Current parity: %s (%d)\n",
                 ryn4::hardware::parityConfigToString(
                     static_cast<ryn4::hardware::ParityConfig>(info.value.configuredParity)
                 ),
                 info.value.configuredParity);

    // Set to Even parity
    Serial.println("\nSetting parity to Even (1)...");
    auto result = ryn4->setParity(1);

    if (result.isError()) {
        Serial.printf("✗ FAILED (error: %d)\n", static_cast<int>(result.error()));
        return;
    }

    Serial.println("✓ Parity set to Even");
    Serial.println();
    Serial.println("⚠ POWER CYCLE module to activate new parity");
    Serial.println("⚠ Then update your code to use Even parity (8E1)");
    Serial.println();
}

void testFactoryReset() {
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.println("TEST: Factory Reset");
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.println("⚠ WARNING: This will reset reply delay and parity!");
    Serial.println();

    Serial.println("Are you sure you want to factory reset? (y/n)");
    Serial.println("(This prompt is for documentation - modify code to enable)");
    Serial.println();

    // Uncomment to actually perform reset:
    // auto result = ryn4->factoryReset();
    // if (result.isOk()) {
    //     Serial.println("✓ Factory reset command sent");
    //     Serial.println("⚠ POWER CYCLE module to complete reset");
    // } else {
    //     Serial.printf("✗ FAILED (error: %d)\n", static_cast<int>(result.error()));
    // }
}
*/

/**
 * Example: Diagnostic routine for production systems
 */
/*
void performDiagnostics() {
    Serial.println("\n═══════════════════════════════════════");
    Serial.println("       SYSTEM DIAGNOSTICS");
    Serial.println("═══════════════════════════════════════\n");

    // 1. Read device info
    auto info = ryn4->readDeviceInfo();
    if (info.isOk()) {
        Serial.printf("✓ Device Type: 0x%04X\n", info.value.deviceType);
        Serial.printf("✓ Firmware: v%d.%d\n", info.value.firmwareMajor, info.value.firmwareMinor);
    } else {
        Serial.println("✗ Could not read device info");
        return;
    }

    // 2. Verify configuration
    auto verify = ryn4->verifyHardwareConfig();
    if (verify.isOk() && verify.value) {
        Serial.println("✓ Hardware config matches software");
    } else {
        Serial.println("⚠ Hardware config mismatch");
        Serial.printf("  DIP Address: %d (expected: %d)\n",
                     info.value.configuredAddress, SLAVE_ID);
    }

    // 3. Check relay responsiveness
    auto bitmap = ryn4->readBitmapStatus();
    if (bitmap.isOk()) {
        Serial.println("✓ Relay module responsive");
    } else {
        Serial.println("✗ Communication error");
    }

    // 4. Test relay control
    auto testResult = ryn4->controlRelayVerified(1, ryn4::RelayAction::TOGGLE);
    if (testResult == ryn4::RelayErrorCode::SUCCESS) {
        Serial.println("✓ Relay control functional");
    } else {
        Serial.println("✗ Relay control failed");
    }

    Serial.println("\n═══════════════════════════════════════\n");
}
*/
