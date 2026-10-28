/*
 * RYN4MultiCommand.cpp - part of the ESP32-RYN4 library
 *
 * Copyright (C) 2025-2026 packerlschupfer
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

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

namespace {

// Encode one RelayCommandSpec to its FC 0x10 register value.
// Returns false for broadcast actions (ALL_ON/ALL_OFF: addr 0x0000 only) and
// unknown actions — callers choose their own fallback policy.
bool encodeRelayCommand(const RYN4::RelayCommandSpec& spec, uint16_t& out) {
    switch (spec.action) {
        case ryn4::RelayAction::ON:        out = ryn4::hardware::CMD_ON;        return true;
        case ryn4::RelayAction::OFF:       out = ryn4::hardware::CMD_OFF;       return true;
        case ryn4::RelayAction::TOGGLE:    out = ryn4::hardware::CMD_TOGGLE;    return true;
        case ryn4::RelayAction::LATCH:     out = ryn4::hardware::CMD_LATCH;     return true;
        case ryn4::RelayAction::MOMENTARY: out = ryn4::hardware::CMD_MOMENTARY; return true;
        case ryn4::RelayAction::DELAY:
            out = ryn4::hardware::makeDelayCommand(spec.delaySeconds);
            return true;
        case ryn4::RelayAction::ALL_ON:
        case ryn4::RelayAction::ALL_OFF:
        default:
            return false;
    }
}

} // namespace

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
        if (encodeRelayCommand(commands[i], commandValue)) {
            RYN4_LOG_D("  Relay %zu: action %d (0x%04X)",
                       i + 1, static_cast<int>(commands[i].action), commandValue);
        } else {
            // ALL_ON/ALL_OFF are broadcast-only; unknown actions should never
            // appear. Preserve historical fallback-to-OFF so a stray enum value
            // can't energize relays.
            RYN4_LOG_W("  Relay %zu: action %d not valid in multi-command, using OFF",
                       i + 1, static_cast<int>(commands[i].action));
            commandValue = ryn4::hardware::CMD_OFF;
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

/**
 * @brief Set a contiguous range of relays to the same command (single FC 0x10)
 */
ryn4::RelayErrorCode RYN4::setRelayCommandRange(uint8_t startRelay, uint8_t count,
                                                const RelayCommandSpec& spec) {
    RYN4_TIME_START();

    // Validate range (0-based: startRelay in [0, NUM_RELAYS-1], count in [1, NUM_RELAYS]).
    if (count == 0 || count > NUM_RELAYS) {
        RYN4_LOG_W("setRelayCommandRange: invalid count %u", static_cast<unsigned>(count));
        return RelayErrorCode::INVALID_INDEX;
    }
    if (startRelay >= NUM_RELAYS ||
        static_cast<uint16_t>(startRelay) + static_cast<uint16_t>(count) > NUM_RELAYS) {
        RYN4_LOG_W("setRelayCommandRange: range start=%u count=%u out of bounds",
                   static_cast<unsigned>(startRelay),
                   static_cast<unsigned>(count));
        return RelayErrorCode::INVALID_INDEX;
    }

    // Encode once — same command for every register in the range.
    uint16_t commandValue = 0;
    if (!encodeRelayCommand(spec, commandValue)) {
        RYN4_LOG_W("setRelayCommandRange: action %d not valid for a range (broadcast or unknown)",
                   static_cast<int>(spec.action));
        return RelayErrorCode::INVALID_INDEX;
    }

    if (statusFlags.moduleOffline) {
        RYN4_LOG_E("Module is offline - cannot execute range command");
        return RelayErrorCode::MODBUS_ERROR;
    }

    RYN4_LOG_D("setRelayCommandRange: R%u..R%u = 0x%04X",
               static_cast<unsigned>(startRelay + 1),
               static_cast<unsigned>(startRelay + count),
               commandValue);

    // Build the register payload on the stack — the public writeMultipleRegisters()
    // overload takes std::vector, but this path runs at the DELAY watchdog cadence
    // on safety-critical consumers and must not touch the heap. count is bounded
    // by NUM_RELAYS, so a fixed-size buffer always suffices.
    uint16_t regs[NUM_RELAYS];
    for (uint8_t i = 0; i < count; ++i) {
        regs[i] = commandValue;
    }

    RetryPolicy retryPolicy = RetryPolicies::modbusDefault();

    // Replicate ModbusDevice::writeMultipleRegisters() with a stack buffer:
    // acquireBusMutex -> ensureSyncReady -> sendRequestInternal(0x10) ->
    // waitForResponse -> releaseBusMutex. Each retry re-acquires the mutex so
    // concurrent bus users on the same RS485 don't starve.
    auto result = retryPolicy.execute<bool>([&]() {
        if (!acquireBusMutex(MODBUS_MUTEX_TIMEOUT_MS)) {
            RYN4_LOG_W("setRelayCommandRange: bus mutex timeout");
            return false;
        }
        ensureSyncReady();
        if (sendRequestInternal(0x10, startRelay, count, regs) != ESP_OK) {
            releaseBusMutex();
            RYN4_LOG_D("Range request send failed");
            return false;
        }
        auto writeResult = waitForResponse();
        releaseBusMutex();
        if (!writeResult.isOk()) {
            RYN4_LOG_D("Range write failed: %d",
                       static_cast<int>(writeResult.error()));
            return false;
        }
        return true;
    });

    if (result.attemptsMade > 1) {
        RYN4_LOG_I("Range command succeeded after %d attempts (delay: %lu ms)",
                   result.attemptsMade, result.totalDelayMs);
    }

    if (!result.success) {
        RYN4_LOG_E("Range command failed after %d attempts", result.attemptsMade);
        RYN4_TIME_END("setRelayCommandRange");
        return RelayErrorCode::MODBUS_ERROR;
    }

    RYN4_TIME_END("setRelayCommandRange");
    return RelayErrorCode::SUCCESS;
}
