/**
 * @file RYN4MultiCommand.cpp
 * @brief Multi-command relay control implementation
 *
 * Implements setMultipleRelayCommands() which allows sending different command
 * types (ON, OFF, TOGGLE, LATCH, MOMENTARY, DELAY) to multiple relays in
 * a single FC 0x10 (Write Multiple Registers) operation.
 *
 * Hardware testing confirmed (2025-12-07) that ALL command types work in
 * FC 0x10 batch operations, including:
 * - Mixed command types in single operation
 * - Independent DELAY timing per relay
 * - MOMENTARY pulses on multiple relays
 * - LATCH inter-locking behavior maintained
 *
 * This provides 6-8x performance improvement over sequential commands and
 * enables atomic execution for complex automation sequences.
 */

#include "RYN4.h"
#include "ryn4/HardwareRegisters.h"
#include "RetryPolicy.h"

/**
 * @brief Set multiple relays with different commands in single atomic operation
 */
ryn4::RelayErrorCode RYN4::setMultipleRelayCommands(const std::array<RelayCommandSpec, 8>& commands) {
    RYN4_TIME_START();

    RYN4_LOG_D("setMultipleRelayCommands called with mixed command types");

    // Check if module is offline
    if (statusFlags.moduleOffline) {
        RYN4_LOG_E("Module is offline - cannot execute multi-command");
        return RelayErrorCode::MODBUS_ERROR;
    }

    // Prepare command data for all 8 relays
    std::vector<uint16_t> data(NUM_RELAYS);

    RYN4_DEBUG_ONLY(
        RYN4_LOG_D("Preparing multi-command batch:");
    );

    for (size_t i = 0; i < NUM_RELAYS; i++) {
        uint16_t commandValue = 0;

        switch (commands[i].action) {
            case RelayAction::ON:
                commandValue = ryn4::hardware::CMD_ON;  // 0x0100
                RYN4_LOG_D("  Relay %d: ON (0x%04X)", i+1, commandValue);
                break;

            case RelayAction::OFF:
                commandValue = ryn4::hardware::CMD_OFF;  // 0x0200
                RYN4_LOG_D("  Relay %d: OFF (0x%04X)", i+1, commandValue);
                break;

            case RelayAction::TOGGLE:
                commandValue = ryn4::hardware::CMD_TOGGLE;  // 0x0300
                RYN4_LOG_D("  Relay %d: TOGGLE (0x%04X)", i+1, commandValue);
                break;

            case RelayAction::LATCH:
                commandValue = ryn4::hardware::CMD_LATCH;  // 0x0400
                RYN4_LOG_D("  Relay %d: LATCH (0x%04X)", i+1, commandValue);
                break;

            case RelayAction::MOMENTARY:
                commandValue = ryn4::hardware::CMD_MOMENTARY;  // 0x0500
                RYN4_LOG_D("  Relay %d: MOMENTARY (0x%04X)", i+1, commandValue);
                break;

            case RelayAction::DELAY:
                commandValue = ryn4::hardware::makeDelayCommand(commands[i].delaySeconds);
                RYN4_LOG_D("  Relay %d: DELAY %ds (0x%04X)",
                           i+1, commands[i].delaySeconds, commandValue);
                break;

            case RelayAction::ALL_ON:
            case RelayAction::ALL_OFF:
                // These are broadcast commands, not valid in multi-command
                RYN4_LOG_W("  Relay %d: ALL_ON/ALL_OFF not valid in multi-command, using OFF",
                           i+1);
                commandValue = ryn4::hardware::CMD_OFF;
                break;

            default:
                RYN4_LOG_W("  Relay %d: Unknown action %d, using OFF",
                           i+1, static_cast<int>(commands[i].action));
                commandValue = ryn4::hardware::CMD_OFF;
                break;
        }

        data[i] = commandValue;
    }

    // Create retry policy for batch operations
    RetryPolicy retryPolicy = RetryPolicies::modbusDefault();

    // Execute with retry
    auto result = retryPolicy.execute<bool>([&]() {
        RYN4_LOG_D("Sending multi-command batch (FC 0x10)");

        // writeMultipleRegisters handles mutex internally
        auto writeResult = writeMultipleRegisters(
            0,      // Starting register address (0x0000 for relays 1-8)
            data    // Command data vector
        );

        if (!writeResult.isOk()) {
            RYN4_LOG_D("Multi-command write failed: %d",
                       static_cast<int>(writeResult.error()));
        }
        return writeResult.isOk();
    });

    if (result.attemptsMade > 1) {
        RYN4_LOG_I("Multi-command succeeded after %d attempts (delay: %lu ms)",
                   result.attemptsMade, result.totalDelayMs);
    }

    if (!result.success) {
        RYN4_LOG_E("Multi-command failed after %d attempts", result.attemptsMade);
        RYN4_TIME_END("setMultipleRelayCommands");
        return RelayErrorCode::MODBUS_ERROR;
    }

    RYN4_LOG_D("Multi-command batch sent successfully");

    // Note: State updates will come from hardware response
    // For DELAY/MOMENTARY, states change asynchronously

    RYN4_TIME_END("setMultipleRelayCommands");
    return RelayErrorCode::SUCCESS;
}
