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
