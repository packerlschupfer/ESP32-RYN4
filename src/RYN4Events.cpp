/*
 * RYN4Events.cpp - part of the ESP32-RYN4 library
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
 * @file RYN4Events.cpp
 * @brief Event group management implementation for RYN4 library
 * 
 * This file contains the event group manipulation methods for managing
 * relay update and error notifications.
 */

#include "RYN4.h"

using namespace ryn4;

// Event group bit manipulation methods

void RYN4::setUpdateEventBits(uint32_t bitsToSet) {
    if (xUpdateEventGroup) {
        xEventGroupSetBits(xUpdateEventGroup, bitsToSet);
    }
}

void RYN4::clearUpdateEventBits(uint32_t bitsToClear) {
    if (xUpdateEventGroup) {
        xEventGroupClearBits(xUpdateEventGroup, bitsToClear);
    }
}

void RYN4::setErrorEventBits(uint32_t bitsToSet) {
    if (xErrorEventGroup) {
        xEventGroupSetBits(xErrorEventGroup, bitsToSet);
    }
}

void RYN4::clearErrorEventBits(uint32_t bitsToClear) {
    RYN4_LOG_D("clearErrorEventBits called with bits: 0x%08X", bitsToClear);
    if (xErrorEventGroup) {
        xEventGroupClearBits(xErrorEventGroup, bitsToClear);
    }
}

void RYN4::setInitializationBit(EventBits_t bit) {
    if (!xInitEventGroup) {
        RYN4_LOG_E("xInitEventGroup is NULL - cannot set bit 0x%lx", bit);
        return;
    }
    
    // Get current bits
    EventBits_t currentBits = xEventGroupGetBits(xInitEventGroup);
    
    // Check if bit is already set
    if ((currentBits & bit) == bit) {
        // Bit already set, no need to set again
        RYN4_DEBUG_ONLY(
            RYN4_LOG_D("Init bit 0x%lx already set (current: 0x%lx)", bit, currentBits);
        );
        return;
    }

    // Set the bit
    xEventGroupSetBits(xInitEventGroup, bit);
    
    // Verify it was set
    EventBits_t newBits = xEventGroupGetBits(xInitEventGroup);
    if ((newBits & bit) == bit) {
        RYN4_LOG_D("Set init bit 0x%lx successfully (new bits: 0x%lx)", bit, newBits);
    } else {
        RYN4_LOG_E("Failed to set init bit 0x%lx (bits: 0x%lx)", bit, newBits);
    }
}

EventGroupHandle_t RYN4::getErrorEventGroup() const {
    return xErrorEventGroup;
}