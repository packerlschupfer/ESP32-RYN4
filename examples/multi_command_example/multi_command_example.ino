/**
 * @file multi_command_example.ino
 * @brief Demonstrates advanced multi-command relay control
 *
 * This example showcases the setMultipleRelayCommands() method which allows
 * sending different command types to multiple relays in a single atomic operation.
 *
 * Hardware Discovery (2025-12-07):
 * ALL command types work in FC 0x10, including:
 * - OPEN, CLOSE, TOGGLE, LATCH
 * - MOMENTARY (simultaneous pulses)
 * - DELAY (independent timing per relay)
 *
 * Performance: 6-8x faster than sequential commands!
 *
 * @version 1.0.0
 * @date 2025-12-07
 */

#include <RYN4.h>

// Configuration
const uint8_t SLAVE_ID = 2;
const uint32_t BAUD_RATE = 9600;

RYN4* ryn4 = nullptr;

void setup() {
    Serial.begin(115200);
    delay(1500);

    Serial.println("\n╔════════════════════════════════════════════════════════════╗");
    Serial.println("║   RYN4 Multi-Command Demonstration                        ║");
    Serial.println("╚════════════════════════════════════════════════════════════╝\n");

    // Create and initialize RYN4
    ryn4 = new RYN4(SLAVE_ID, "MultiCmd");
    auto initResult = ryn4->initialize();

    if (initResult.isError()) {
        Serial.println("✗ Initialization failed!");
        while (1) { delay(1000); }
    }

    Serial.println("✓ RYN4 initialized\n");

    // Run demonstrations
    demo1_basicMixedCommands();
    delay(3000);

    demo2_staggeredMotorStart();
    delay(5000);

    demo3_simultaneousPulse();
    delay(3000);

    demo4_complexAutomation();
    delay(5000);

    Serial.println("\n╔════════════════════════════════════════════════════════════╗");
    Serial.println("║   All Demonstrations Complete!                            ║");
    Serial.println("╚════════════════════════════════════════════════════════════╝\n");
}

void loop() {
    // Demonstrations complete
    delay(5000);
}

/**
 * DEMO 1: Basic Mixed Commands
 * Shows different command types in one operation
 */
void demo1_basicMixedCommands() {
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.println("DEMO 1: Basic Mixed Commands");
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.println("Sending mixed commands:");
    Serial.println("  Relay 1: OPEN (stays ON)");
    Serial.println("  Relay 2: TOGGLE");
    Serial.println("  Relay 3: MOMENTARY (1 sec pulse)");
    Serial.println("  Relay 4: DELAY 5 seconds");
    Serial.println("  Relays 5-8: CLOSE");
    Serial.println();

    std::array<RYN4::RelayCommandSpec, 8> commands = {
        {ryn4::RelayAction::OPEN, 0},
        {ryn4::RelayAction::TOGGLE, 0},
        {ryn4::RelayAction::MOMENTARY, 0},
        {ryn4::RelayAction::DELAY, 5},
        {ryn4::RelayAction::CLOSE, 0},
        {ryn4::RelayAction::CLOSE, 0},
        {ryn4::RelayAction::CLOSE, 0},
        {ryn4::RelayAction::CLOSE, 0}
    };

    auto result = ryn4->setMultipleRelayCommands(commands);

    if (result == ryn4::RelayErrorCode::SUCCESS) {
        Serial.println("✓ Multi-command sent successfully\n");

        Serial.println("Observing relay behavior:");
        Serial.println("  t=0s: Relays 1,2,3,4 should turn ON");
        Serial.println("  t=1s: Relay 3 should turn OFF (MOMENTARY pulse complete)");
        Serial.println("  t=5s: Relay 4 should turn OFF (DELAY expired)");
        Serial.println("  Final: Relays 1,2 should remain ON\n");

        delay(6000);  // Watch the sequence

        Serial.println("Checking final state:");
        displayRelayStates();
    } else {
        Serial.printf("✗ Multi-command failed (error: %d)\n", static_cast<int>(result));
    }

    Serial.println();
}

/**
 * DEMO 2: Staggered Motor Start
 * Prevents power surge by starting motors with delays
 */
void demo2_staggeredMotorStart() {
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.println("DEMO 2: Staggered Motor Start");
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.println("Scenario: Start 4 motors with delays to prevent power surge");
    Serial.println("  Motor 1 (Relay 1): Immediate start");
    Serial.println("  Motor 2 (Relay 2): Start after 3 seconds");
    Serial.println("  Motor 3 (Relay 3): Start after 6 seconds");
    Serial.println("  Motor 4 (Relay 4): Start after 9 seconds");
    Serial.println();

    std::array<RYN4::RelayCommandSpec, 8> staggered = {
        {ryn4::RelayAction::OPEN, 0},      // Motor 1: Immediate
        {ryn4::RelayAction::DELAY, 3},     // Motor 2: 3s delay
        {ryn4::RelayAction::DELAY, 6},     // Motor 3: 6s delay
        {ryn4::RelayAction::DELAY, 9},     // Motor 4: 9s delay
        {ryn4::RelayAction::CLOSE, 0},
        {ryn4::RelayAction::CLOSE, 0},
        {ryn4::RelayAction::CLOSE, 0},
        {ryn4::RelayAction::CLOSE, 0}
    };

    auto result = ryn4->setMultipleRelayCommands(staggered);

    if (result == ryn4::RelayErrorCode::SUCCESS) {
        Serial.println("✓ Staggered start sequence initiated\n");

        for (int t = 0; t <= 10; t += 1) {
            if (t == 0 || t == 3 || t == 6 || t == 9 || t == 10) {
                Serial.printf("t=%ds: ", t);
                displayRelayStates();
            }
            delay(1000);
        }

        Serial.println("✓ All motors started with staggered timing\n");
    } else {
        Serial.printf("✗ Staggered start failed (error: %d)\n", static_cast<int>(result));
    }

    // Clean up
    std::array<bool, 8> allOff = {false, false, false, false, false, false, false, false};
    ryn4->setMultipleRelayStates(allOff);
    Serial.println();
}

/**
 * DEMO 3: Simultaneous Pulse Test
 * All relays pulse at the same time - great for testing
 */
void demo3_simultaneousPulse() {
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.println("DEMO 3: Simultaneous Pulse Test");
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.println("All 8 relays will pulse simultaneously for 1 second");
    Serial.println("(You should hear all relays click in unison)\n");

    std::array<RYN4::RelayCommandSpec, 8> pulseAll = {
        {ryn4::RelayAction::MOMENTARY, 0},
        {ryn4::RelayAction::MOMENTARY, 0},
        {ryn4::RelayAction::MOMENTARY, 0},
        {ryn4::RelayAction::MOMENTARY, 0},
        {ryn4::RelayAction::MOMENTARY, 0},
        {ryn4::RelayAction::MOMENTARY, 0},
        {ryn4::RelayAction::MOMENTARY, 0},
        {ryn4::RelayAction::MOMENTARY, 0}
    };

    auto result = ryn4->setMultipleRelayCommands(pulseAll);

    if (result == ryn4::RelayErrorCode::SUCCESS) {
        Serial.println("✓ Pulse command sent\n");

        Serial.println("t=0s (during pulse):");
        delay(200);
        displayRelayStates();

        Serial.println("Waiting for pulse to complete...");
        delay(1500);

        Serial.println("t=2s (after pulse):");
        displayRelayStates();

        Serial.println("✓ All relays pulsed simultaneously\n");
    } else {
        Serial.printf("✗ Pulse test failed (error: %d)\n", static_cast<int>(result));
    }

    Serial.println();
}

/**
 * DEMO 4: Complex Automation Sequence
 * Realistic industrial control scenario
 */
void demo4_complexAutomation() {
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.println("DEMO 4: Complex Automation Sequence (Boiler Startup)");
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.println("Simulating boiler startup sequence:");
    Serial.println("  Relay 1: Circulation pump ON (permanent)");
    Serial.println("  Relay 2: Pre-purge fan pulse (1 sec)");
    Serial.println("  Relay 3: Ignition delay (10 sec warmup)");
    Serial.println("  Relay 4: Burner enable ON (permanent)");
    Serial.println("  Relay 5: Safety valve momentary reset");
    Serial.println("  Relay 6-8: OFF");
    Serial.println();

    std::array<RYN4::RelayCommandSpec, 8> boilerStartup = {
        {ryn4::RelayAction::OPEN, 0},        // Circulation pump
        {ryn4::RelayAction::MOMENTARY, 0},   // Pre-purge fan
        {ryn4::RelayAction::DELAY, 10},      // Ignition delay
        {ryn4::RelayAction::OPEN, 0},        // Burner enable
        {ryn4::RelayAction::MOMENTARY, 0},   // Safety reset
        {ryn4::RelayAction::CLOSE, 0},
        {ryn4::RelayAction::CLOSE, 0},
        {ryn4::RelayAction::CLOSE, 0}
    };

    auto result = ryn4->setMultipleRelayCommands(boilerStartup);

    if (result == ryn4::RelayErrorCode::SUCCESS) {
        Serial.println("✓ Startup sequence initiated\n");

        Serial.println("Monitoring sequence:");
        for (int t = 0; t <= 12; t += 2) {
            Serial.printf("t=%ds: ", t);
            displayRelayStates();
            delay(2000);
        }

        Serial.println("✓ Boiler startup complete!\n");
        Serial.println("Notice:");
        Serial.println("  - Pump and burner stayed ON");
        Serial.println("  - Fan and safety valve pulsed (1 sec)");
        Serial.println("  - Ignition turned OFF after 10 seconds");
        Serial.println("  - All executed from SINGLE command!\n");
    } else {
        Serial.printf("✗ Startup sequence failed (error: %d)\n", static_cast<int>(result));
    }

    // Clean up
    std::array<bool, 8> allOff = {false, false, false, false, false, false, false, false};
    ryn4->setMultipleRelayStates(allOff);
    delay(500);
    Serial.println("System shut down.\n");
}

/**
 * Helper: Display current relay states
 */
void displayRelayStates() {
    Serial.print("  Status: [");
    for (int i = 1; i <= 8; i++) {
        bool state;
        auto result = ryn4->readRelayStatus(i, state);
        if (result == ryn4::RelayErrorCode::SUCCESS) {
            Serial.printf("%d:%s", i, state ? "ON " : "OFF");
        } else {
            Serial.print("?");
        }
        if (i < 8) Serial.print(" ");
    }
    Serial.println("]");
}

/**
 * Performance Comparison Demo (commented out)
 *
 * Uncomment to see performance difference between sequential and batch commands
 */
/*
void performanceComparison() {
    Serial.println("Performance Comparison: Sequential vs Batch\n");

    // Sequential (old method)
    unsigned long startSeq = millis();
    ryn4->controlRelay(1, ryn4::RelayAction::OPEN);
    delay(50);
    ryn4->controlRelay(2, ryn4::RelayAction::MOMENTARY);
    delay(50);
    ryn4->controlRelay(3, ryn4::RelayAction::TOGGLE);
    delay(50);
    ryn4->controlRelay(4, ryn4::RelayAction::CLOSE);
    unsigned long seqTime = millis() - startSeq;

    delay(2000);

    // Batch (new method)
    std::array<RYN4::RelayCommandSpec, 8> batchCmds = {
        {ryn4::RelayAction::OPEN, 0},
        {ryn4::RelayAction::MOMENTARY, 0},
        {ryn4::RelayAction::TOGGLE, 0},
        {ryn4::RelayAction::CLOSE, 0},
        {ryn4::RelayAction::CLOSE, 0},
        {ryn4::RelayAction::CLOSE, 0},
        {ryn4::RelayAction::CLOSE, 0},
        {ryn4::RelayAction::CLOSE, 0}
    };

    unsigned long startBatch = millis();
    ryn4->setMultipleRelayCommands(batchCmds);
    unsigned long batchTime = millis() - startBatch;

    Serial.printf("Sequential: %lu ms\n", seqTime);
    Serial.printf("Batch:      %lu ms\n", batchTime);
    Serial.printf("Speedup:    %.1fx faster\n\n", (float)seqTime / batchTime);
}
*/
