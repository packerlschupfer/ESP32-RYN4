/*
 * BaseRelayMapping.h - part of the ESP32-RYN4 library
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

// src/base/BaseRelayMapping.h
#pragma once

#include <stdint.h>
#include <array>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

namespace base {

/**
 * @brief Hardware configuration for a single relay (constexpr - lives in flash)
 *
 * This struct contains all the static hardware information about a relay
 * that never changes at runtime. It should be instantiated as constexpr
 * arrays in flash memory to save RAM.
 */
struct RelayHardwareConfig {
    uint8_t physicalNumber;    // Physical relay number (1-8)
    EventBits_t onBit;         // Event bit for relay ON
    EventBits_t offBit;        // Event bit for relay OFF
    EventBits_t statusBit;     // Event bit for status
    EventBits_t updateBit;     // Event bit for update notification
    EventBits_t errorBit;      // Event bit for error indication
    bool inverseLogic;         // true = relay OFF means device ON
};

/**
 * @brief Runtime binding for a single relay (lives in RAM)
 *
 * This struct contains only the runtime pointer to the application's
 * state variable. It's initialized once at startup and used for
 * reading/writing relay states.
 */
struct RelayBinding {
    bool* statePtr;            // Pointer to relay state in application
};

} // namespace base
