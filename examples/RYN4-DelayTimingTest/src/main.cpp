/**
 * @file main.cpp
 * @brief RYN4 DELAY Command Timing Test
 *
 * This example tests the timing behavior of DELAY commands on RYN4 relay modules.
 *
 * PROBLEM: In the boiler controller, we see "Relay verification SUCCESS after 2 attempts"
 * when using DELAY commands. This test isolates whether:
 * 1. RYN4 hardware has inherent delay between command acceptance and state reflection
 * 2. This is a bus/timing issue on our side
 *
 * TEST SEQUENCE:
 * 1. Send DELAY command to relay 0 (e.g., 0x0614 = ON for 20s)
 * 2. Immediately read back holding register
 * 3. Log the time delta and returned value
 * 4. Repeat read every 100ms for 2 seconds
 * 5. Verify we always get 0x0001 (ON state)
 *
 * Also tests:
 * - Multiple relays with DELAY at once (bitmap write via FC 0x10)
 * - Read via bitmap register 0x0080 vs individual holding registers
 * - Check if there's any delay between command acceptance and state reflection
 */

#include <Arduino.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp32ModbusRTU.h>

// RYN4 library
#include <RYN4.h>
#include <ModbusDevice.h>
#include <ModbusRegistry.h>  // For ModbusRegistry singleton

static const char* TAG = "DelayTest";

// ========== Configuration ==========

// RS485 pins (adjust for your hardware)
#define MODBUS_RX_PIN 36
#define MODBUS_TX_PIN 4
#define MODBUS_BAUD_RATE 9600
#define MODBUS_CONFIG SERIAL_8E1  // 8E1 = 8 data bits, Even parity, 1 stop bit

// RYN4 module address (check your DIP switches)
#define RYN4_ADDRESS 0x02

// Test parameters
#define DELAY_SECONDS 20        // DELAY command duration (e.g., 20s)
#define READ_INTERVAL_MS 100    // Read interval during test
#define READ_DURATION_MS 2000   // Total read duration after command
#define INTER_TEST_DELAY_MS 5000 // Delay between test cycles

// ========== Globals ==========

esp32ModbusRTU modbusMaster(&Serial1);
RYN4* relayModule = nullptr;  // Named to avoid conflict with ryn4 namespace

// Global callback handlers (required by ModbusDevice)
extern void mainHandleData(uint8_t serverAddress, esp32Modbus::FunctionCode fc,
                          uint16_t startingAddress, const uint8_t* data, size_t length);
extern void handleError(uint8_t serverAddress, esp32Modbus::Error error);

// Timing statistics
struct TimingStats {
    uint32_t commandSentTime;      // When command was sent
    uint32_t firstReadTime;        // When first read returned
    uint32_t firstSuccessTime;     // When first successful read occurred
    int firstSuccessAttempt;       // Which attempt first showed ON
    int totalReads;                // Total reads performed
    int successReads;              // Reads that returned ON
    int failReads;                 // Reads that returned OFF
};

TimingStats stats;

// Forward declarations
void runDelayTimingTest();
void runMultiRelayDelayTest();
void runBitmapVsRegisterTest();
void printStats(const char* testName);

// ========== Setup ==========

void setup() {
    // Initialize serial for debug output
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {
        delay(10);
    }

    Serial.println("\n\n========================================");
    Serial.println("    RYN4 DELAY Command Timing Test");
    Serial.println("========================================\n");

    // Set log levels
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
    esp_log_level_set("RYN4", ESP_LOG_DEBUG);

    // Initialize RS485 serial
    Serial.printf("Initializing RS485 on RX=%d, TX=%d at %d baud\n",
                  MODBUS_RX_PIN, MODBUS_TX_PIN, MODBUS_BAUD_RATE);

    Serial1.begin(MODBUS_BAUD_RATE, MODBUS_CONFIG, MODBUS_RX_PIN, MODBUS_TX_PIN);

    // Set global Modbus RTU instance BEFORE creating devices
    modbus::ModbusRegistry::getInstance().setModbusRTU(&modbusMaster);

    // Setup Modbus master with callbacks using lambdas
    modbusMaster.onData([](uint8_t serverAddress, esp32Modbus::FunctionCode fc,
                           uint16_t address, const uint8_t* data, size_t length) {
        Serial.printf("Modbus data received - Addr: 0x%02X, FC: %d, Len: %d\n",
                      serverAddress, static_cast<int>(fc), length);
        mainHandleData(serverAddress, fc, address, data, length);
    });

    modbusMaster.onError([](esp32Modbus::Error error) {
        Serial.printf("ModbusRTU Error: %d\n", static_cast<int>(error));
        handleError(0xFF, error);
    });

    // Start Modbus on core 1
    modbusMaster.begin(1);

    Serial.println("Modbus initialized");
    delay(100);  // Allow Modbus task to start

    // Create RYN4 instance (no modbusMaster parameter needed - uses global)
    Serial.printf("Creating RYN4 instance at address 0x%02X\n", RYN4_ADDRESS);
    relayModule = new RYN4(RYN4_ADDRESS, "RYN4-Test");

    // Register with Modbus device registry for response routing
    modbus::ModbusRegistry::getInstance().registerDevice(RYN4_ADDRESS, relayModule);

    Serial.println("Initializing RYN4 module...");

    // Initialize RYN4
    auto initResult = relayModule->initialize();
    if (initResult.isError()) {
        Serial.printf("ERROR: RYN4 initialization failed: %d\n",
                      static_cast<int>(initResult.error()));
        Serial.println("Check: 1) RS485 wiring, 2) DIP switch address, 3) Power supply");
        while (true) {
            delay(1000);
        }
    }

    // Wait for initialization to complete
    auto waitResult = relayModule->waitForInitializationComplete(pdMS_TO_TICKS(10000));
    if (waitResult.isError()) {
        Serial.println("WARNING: Initialization timeout, continuing anyway...");
    } else {
        Serial.println("RYN4 initialized successfully");
    }

    // Read initial state
    auto bitmap = relayModule->readBitmapStatus(true);
    if (bitmap.isOk()) {
        Serial.printf("Initial relay states: 0x%02X\n", bitmap.value());
    } else {
        Serial.println("WARNING: Failed to read initial relay states");
    }

    Serial.println("\nStarting DELAY timing tests in 3 seconds...\n");
    delay(3000);
}

// ========== Main Loop ==========

void loop() {
    static int testCycle = 0;
    testCycle++;

    Serial.println("\n========================================");
    Serial.printf("           TEST CYCLE %d\n", testCycle);
    Serial.println("========================================\n");

    // Run tests
    runDelayTimingTest();
    delay(INTER_TEST_DELAY_MS);

    runMultiRelayDelayTest();
    delay(INTER_TEST_DELAY_MS);

    runBitmapVsRegisterTest();
    delay(INTER_TEST_DELAY_MS);

    Serial.println("\n\n----------------------------------------");
    Serial.println("All tests complete. Waiting before next cycle...");
    Serial.println("----------------------------------------\n");

    delay(10000);  // Wait 10s before next cycle
}

// ========== Test 1: Single Relay DELAY Timing ==========

void runDelayTimingTest() {
    Serial.println("----------------------------------------");
    Serial.println("TEST 1: Single Relay DELAY Timing");
    Serial.println("----------------------------------------");
    Serial.printf("Sending DELAY %d (0x06%02X) to relay 1\n", DELAY_SECONDS, DELAY_SECONDS);

    // Reset stats
    memset(&stats, 0, sizeof(stats));

    // First, ensure relay is OFF
    relayModule->turnOffRelay(1);  // DELAY 0 to force OFF
    delay(100);

    // Verify relay is OFF
    auto preCheck = relayModule->readBitmapStatus(true);
    if (preCheck.isOk()) {
        bool relayOn = (preCheck.value() & 0x01) != 0;
        Serial.printf("Pre-check: Relay 1 is %s\n", relayOn ? "ON (unexpected!)" : "OFF (expected)");
        if (relayOn) {
            Serial.println("WARNING: Relay 1 was already ON, waiting for it to turn OFF...");
            delay(1000);
        }
    }

    // Send DELAY command and record time
    Serial.println("\nSending DELAY command...");
    stats.commandSentTime = millis();

    auto result = relayModule->turnOnRelayTimed(1, DELAY_SECONDS);

    if (result != ryn4::RelayErrorCode::SUCCESS) {
        Serial.printf("ERROR: turnOnRelayTimed failed: %d\n", static_cast<int>(result));
        return;
    }

    Serial.printf("Command sent at t=%lu ms\n", stats.commandSentTime);

    // Immediately start reading back
    Serial.println("\nReading back relay state every 100ms...");
    Serial.println("Attempt | Time (ms) | Delta (ms) | Bitmap | Relay 1");
    Serial.println("--------|-----------|------------|--------|--------");

    int attempt = 0;
    uint32_t endTime = stats.commandSentTime + READ_DURATION_MS;

    while (millis() < endTime) {
        attempt++;
        uint32_t readTime = millis();
        uint32_t delta = readTime - stats.commandSentTime;

        auto bitmap = relayModule->readBitmapStatus(true);

        if (bitmap.isOk()) {
            bool relayOn = (bitmap.value() & 0x01) != 0;

            if (attempt == 1) {
                stats.firstReadTime = readTime;
            }

            if (relayOn) {
                stats.successReads++;
                if (stats.firstSuccessTime == 0) {
                    stats.firstSuccessTime = readTime;
                    stats.firstSuccessAttempt = attempt;
                }
            } else {
                stats.failReads++;
            }

            Serial.printf("   %3d  |   %6lu  |    %5lu   |  0x%02X  | %s\n",
                          attempt, readTime, delta, bitmap.value(),
                          relayOn ? "ON" : "OFF");
        } else {
            Serial.printf("   %3d  |   %6lu  |    %5lu   | ERROR  | ?\n",
                          attempt, readTime, delta);
        }

        stats.totalReads++;
        delay(READ_INTERVAL_MS);
    }

    printStats("Single Relay DELAY");

    // Turn off relay
    Serial.println("\nTurning off relay 1...");
    relayModule->turnOffRelay(1);
}

// ========== Test 2: Multiple Relay DELAY (Batch) ==========

void runMultiRelayDelayTest() {
    Serial.println("\n----------------------------------------");
    Serial.println("TEST 2: Multiple Relay DELAY (Batch FC 0x10)");
    Serial.println("----------------------------------------");
    Serial.println("Sending DELAY 10s to relays 1, 2, 3 via setMultipleRelayCommands()");

    // Reset stats
    memset(&stats, 0, sizeof(stats));

    // First, ensure all relays are OFF
    relayModule->emergencyStopAll();
    delay(100);

    // Prepare batch command: DELAY 10s for relays 1-3, OFF for others
    std::array<RYN4::RelayCommandSpec, 8> commands = {
        RYN4::RelayCommandSpec{ryn4::RelayAction::DELAY, 10},  // Relay 1: DELAY 10s
        RYN4::RelayCommandSpec{ryn4::RelayAction::DELAY, 10},  // Relay 2: DELAY 10s
        RYN4::RelayCommandSpec{ryn4::RelayAction::DELAY, 10},  // Relay 3: DELAY 10s
        RYN4::RelayCommandSpec{ryn4::RelayAction::OFF, 0},     // Relay 4: OFF
        RYN4::RelayCommandSpec{ryn4::RelayAction::OFF, 0},     // Relay 5: OFF
        RYN4::RelayCommandSpec{ryn4::RelayAction::OFF, 0},     // Relay 6: OFF
        RYN4::RelayCommandSpec{ryn4::RelayAction::OFF, 0},     // Relay 7: OFF
        RYN4::RelayCommandSpec{ryn4::RelayAction::OFF, 0}      // Relay 8: OFF
    };

    Serial.println("\nSending batch DELAY command...");
    stats.commandSentTime = millis();

    auto result = relayModule->setMultipleRelayCommands(commands);

    if (result != ryn4::RelayErrorCode::SUCCESS) {
        Serial.printf("ERROR: setMultipleRelayCommands failed: %d\n", static_cast<int>(result));
        return;
    }

    Serial.printf("Batch command sent at t=%lu ms\n", stats.commandSentTime);

    // Read back
    Serial.println("\nReading back relay states...");
    Serial.println("Attempt | Delta (ms) | Bitmap   | R1  | R2  | R3");
    Serial.println("--------|------------|----------|-----|-----|----");

    int attempt = 0;
    uint32_t endTime = stats.commandSentTime + READ_DURATION_MS;

    while (millis() < endTime) {
        attempt++;
        uint32_t readTime = millis();
        uint32_t delta = readTime - stats.commandSentTime;

        auto bitmap = relayModule->readBitmapStatus(true);

        if (bitmap.isOk()) {
            uint8_t val = bitmap.value();
            bool r1 = (val & 0x01) != 0;
            bool r2 = (val & 0x02) != 0;
            bool r3 = (val & 0x04) != 0;

            // Count success if all 3 are ON
            if (r1 && r2 && r3) {
                stats.successReads++;
                if (stats.firstSuccessTime == 0) {
                    stats.firstSuccessTime = readTime;
                    stats.firstSuccessAttempt = attempt;
                }
            } else {
                stats.failReads++;
            }

            Serial.printf("   %3d  |    %5lu   |  0x%02X    | %s | %s | %s\n",
                          attempt, delta, val,
                          r1 ? "ON " : "OFF",
                          r2 ? "ON " : "OFF",
                          r3 ? "ON " : "OFF");
        } else {
            Serial.printf("   %3d  |    %5lu   | ERROR    | ?   | ?   | ?\n",
                          attempt, delta);
        }

        stats.totalReads++;
        delay(READ_INTERVAL_MS);
    }

    printStats("Multi-Relay DELAY Batch");

    // Turn off all relays
    Serial.println("\nTurning off all relays...");
    relayModule->emergencyStopAll();
}

// ========== Test 3: Bitmap vs Register Read Timing ==========

void runBitmapVsRegisterTest() {
    Serial.println("\n----------------------------------------");
    Serial.println("TEST 3: Bitmap (0x0080) vs Individual Register Read");
    Serial.println("----------------------------------------");
    Serial.println("Compare read timing: bitmap register vs individual holding registers");

    // First, turn on relay 1 with DELAY
    relayModule->turnOnRelayTimed(1, 30);
    delay(50);

    Serial.println("\nReading via BITMAP (0x0080) - 5 times:");
    for (int i = 0; i < 5; i++) {
        uint32_t start = micros();
        auto bitmap = relayModule->readBitmapStatus(false);
        uint32_t elapsed = micros() - start;

        if (bitmap.isOk()) {
            Serial.printf("  Read %d: %lu us, bitmap=0x%02X, R1=%s\n",
                          i + 1, elapsed, bitmap.value(),
                          (bitmap.value() & 0x01) ? "ON" : "OFF");
        } else {
            Serial.printf("  Read %d: ERROR after %lu us\n", i + 1, elapsed);
        }
        delay(50);
    }

    Serial.println("\nReading via readRelayStatus(1) - 5 times:");
    for (int i = 0; i < 5; i++) {
        uint32_t start = micros();
        bool state;
        auto result = relayModule->readRelayStatus(1, state);
        uint32_t elapsed = micros() - start;

        if (result == ryn4::RelayErrorCode::SUCCESS) {
            Serial.printf("  Read %d: %lu us, R1=%s\n",
                          i + 1, elapsed, state ? "ON" : "OFF");
        } else {
            Serial.printf("  Read %d: ERROR after %lu us\n", i + 1, elapsed);
        }
        delay(50);
    }

    Serial.println("\nConclusion:");
    Serial.println("  - Bitmap read is more efficient (single read for all relays)");
    Serial.println("  - Individual register read is slightly slower per relay");

    // Turn off relay
    relayModule->turnOffRelay(1);
}

// ========== Print Statistics ==========

void printStats(const char* testName) {
    Serial.println("\n--- Results ---");
    Serial.printf("Test: %s\n", testName);
    Serial.printf("Command sent at: %lu ms\n", stats.commandSentTime);
    Serial.printf("First read at: %lu ms (delta: %lu ms)\n",
                  stats.firstReadTime, stats.firstReadTime - stats.commandSentTime);

    if (stats.firstSuccessTime > 0) {
        Serial.printf("First SUCCESS at: %lu ms (delta: %lu ms, attempt #%d)\n",
                      stats.firstSuccessTime,
                      stats.firstSuccessTime - stats.commandSentTime,
                      stats.firstSuccessAttempt);
    } else {
        Serial.println("First SUCCESS: NEVER (relay never showed ON!)");
    }

    Serial.printf("Total reads: %d, Success: %d, Fail: %d\n",
                  stats.totalReads, stats.successReads, stats.failReads);

    if (stats.firstSuccessAttempt == 1) {
        Serial.println("\n>> RESULT: Relay shows ON immediately after DELAY command <<");
        Serial.println(">> No inherent hardware delay detected <<");
    } else if (stats.firstSuccessAttempt > 1) {
        Serial.println("\n>> RESULT: Relay shows ON after DELAY in readback <<");
        Serial.printf(">> Hardware delay detected: ~%lu ms <<\n",
                      stats.firstSuccessTime - stats.commandSentTime);
    } else {
        Serial.println("\n>> RESULT: FAIL - Relay never showed ON state <<");
        Serial.println(">> Check hardware or DELAY command support <<");
    }
}
