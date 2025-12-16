// src/ryn4/RelayDefs.h

#pragma once

#include "base/BaseRelayMapping.h"
#include <array>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

namespace ryn4 {

    // ON/OFF command bits (bits 0-15)
    static constexpr uint32_t RELAY1_ON_BIT = (1UL << 0UL);
    static constexpr uint32_t RELAY1_OFF_BIT = (1UL << 1UL);
    static constexpr uint32_t RELAY2_ON_BIT = (1UL << 2UL);
    static constexpr uint32_t RELAY2_OFF_BIT = (1UL << 3UL);
    static constexpr uint32_t RELAY3_ON_BIT = (1UL << 4UL);
    static constexpr uint32_t RELAY3_OFF_BIT = (1UL << 5UL);
    static constexpr uint32_t RELAY4_ON_BIT = (1UL << 6UL);
    static constexpr uint32_t RELAY4_OFF_BIT = (1UL << 7UL);
    static constexpr uint32_t RELAY5_ON_BIT = (1UL << 8UL);
    static constexpr uint32_t RELAY5_OFF_BIT = (1UL << 9UL);
    static constexpr uint32_t RELAY6_ON_BIT = (1UL << 10UL);
    static constexpr uint32_t RELAY6_OFF_BIT = (1UL << 11UL);
    static constexpr uint32_t RELAY7_ON_BIT = (1UL << 12UL);
    static constexpr uint32_t RELAY7_OFF_BIT = (1UL << 13UL);
    static constexpr uint32_t RELAY8_ON_BIT = (1UL << 14UL);
    static constexpr uint32_t RELAY8_OFF_BIT = (1UL << 15UL);

    // Status, Update, and Error bits (bits 0-23)
    static constexpr uint32_t RELAY1_STATUS_BIT = (1UL << 0UL);
    static constexpr uint32_t RELAY1_UPDATE_BIT = (1UL << 1UL);
    static constexpr uint32_t RELAY1_ERROR_BIT = (1UL << 2UL);
    static constexpr uint32_t RELAY2_STATUS_BIT = (1UL << 3UL);
    static constexpr uint32_t RELAY2_UPDATE_BIT = (1UL << 4UL);
    static constexpr uint32_t RELAY2_ERROR_BIT = (1UL << 5UL);
    static constexpr uint32_t RELAY3_STATUS_BIT = (1UL << 6UL);
    static constexpr uint32_t RELAY3_UPDATE_BIT = (1UL << 7UL);
    static constexpr uint32_t RELAY3_ERROR_BIT = (1UL << 8UL);
    static constexpr uint32_t RELAY4_STATUS_BIT = (1UL << 9UL);
    static constexpr uint32_t RELAY4_UPDATE_BIT = (1UL << 10UL);
    static constexpr uint32_t RELAY4_ERROR_BIT = (1UL << 11UL);
    static constexpr uint32_t RELAY5_STATUS_BIT = (1UL << 12UL);
    static constexpr uint32_t RELAY5_UPDATE_BIT = (1UL << 13UL);
    static constexpr uint32_t RELAY5_ERROR_BIT = (1UL << 14UL);
    static constexpr uint32_t RELAY6_STATUS_BIT = (1UL << 15UL);
    static constexpr uint32_t RELAY6_UPDATE_BIT = (1UL << 16UL);
    static constexpr uint32_t RELAY6_ERROR_BIT = (1UL << 17UL);
    static constexpr uint32_t RELAY7_STATUS_BIT = (1UL << 18UL);
    static constexpr uint32_t RELAY7_UPDATE_BIT = (1UL << 19UL);
    static constexpr uint32_t RELAY7_ERROR_BIT = (1UL << 20UL);
    static constexpr uint32_t RELAY8_STATUS_BIT = (1UL << 21UL);
    static constexpr uint32_t RELAY8_UPDATE_BIT = (1UL << 22UL);
    static constexpr uint32_t RELAY8_ERROR_BIT = (1UL << 23UL);

    // Helper arrays for easy indexed access
    static constexpr uint32_t RELAY_UPDATE_BITS[8] = {
        RELAY1_UPDATE_BIT,
        RELAY2_UPDATE_BIT,
        RELAY3_UPDATE_BIT,
        RELAY4_UPDATE_BIT,
        RELAY5_UPDATE_BIT,
        RELAY6_UPDATE_BIT,
        RELAY7_UPDATE_BIT,
        RELAY8_UPDATE_BIT
    };

    static constexpr uint32_t RELAY_ERROR_BITS[8] = {
        RELAY1_ERROR_BIT,
        RELAY2_ERROR_BIT,
        RELAY3_ERROR_BIT,
        RELAY4_ERROR_BIT,
        RELAY5_ERROR_BIT,
        RELAY6_ERROR_BIT,
        RELAY7_ERROR_BIT,
        RELAY8_ERROR_BIT
    };

    static constexpr uint32_t RELAY_STATUS_BITS[8] = {
        RELAY1_STATUS_BIT,
        RELAY2_STATUS_BIT,
        RELAY3_STATUS_BIT,
        RELAY4_STATUS_BIT,
        RELAY5_STATUS_BIT,
        RELAY6_STATUS_BIT,
        RELAY7_STATUS_BIT,
        RELAY8_STATUS_BIT
    };

    static constexpr uint32_t RELAY_ON_BITS[8] = {
        RELAY1_ON_BIT,
        RELAY2_ON_BIT,
        RELAY3_ON_BIT,
        RELAY4_ON_BIT,
        RELAY5_ON_BIT,
        RELAY6_ON_BIT,
        RELAY7_ON_BIT,
        RELAY8_ON_BIT
    };

    static constexpr uint32_t RELAY_OFF_BITS[8] = {
        RELAY1_OFF_BIT,
        RELAY2_OFF_BIT,
        RELAY3_OFF_BIT,
        RELAY4_OFF_BIT,
        RELAY5_OFF_BIT,
        RELAY6_OFF_BIT,
        RELAY7_OFF_BIT,
        RELAY8_OFF_BIT
    };

    // IMPORTANT: These enums MUST match the actual bit positions of the constants above!
    enum class RelayUpdateBits {
        RELAY1_UPDATE_BIT = (1UL << 1UL),   // Must match RELAY1_UPDATE_BIT constant
        RELAY2_UPDATE_BIT = (1UL << 4UL),   // Must match RELAY2_UPDATE_BIT constant
        RELAY3_UPDATE_BIT = (1UL << 7UL),   // Must match RELAY3_UPDATE_BIT constant
        RELAY4_UPDATE_BIT = (1UL << 10UL),  // Must match RELAY4_UPDATE_BIT constant
        RELAY5_UPDATE_BIT = (1UL << 13UL),  // Must match RELAY5_UPDATE_BIT constant
        RELAY6_UPDATE_BIT = (1UL << 16UL),  // Must match RELAY6_UPDATE_BIT constant
        RELAY7_UPDATE_BIT = (1UL << 19UL),  // Must match RELAY7_UPDATE_BIT constant
        RELAY8_UPDATE_BIT = (1UL << 22UL)   // Must match RELAY8_UPDATE_BIT constant
    };

    enum class RelayErrorBits {
        RELAY1_ERROR_BIT = (1UL << 2UL),   // Must match RELAY1_ERROR_BIT constant
        RELAY2_ERROR_BIT = (1UL << 5UL),   // Must match RELAY2_ERROR_BIT constant
        RELAY3_ERROR_BIT = (1UL << 8UL),   // Must match RELAY3_ERROR_BIT constant
        RELAY4_ERROR_BIT = (1UL << 11UL),  // Must match RELAY4_ERROR_BIT constant
        RELAY5_ERROR_BIT = (1UL << 14UL),  // Must match RELAY5_ERROR_BIT constant
        RELAY6_ERROR_BIT = (1UL << 17UL),  // Must match RELAY6_ERROR_BIT constant
        RELAY7_ERROR_BIT = (1UL << 20UL),  // Must match RELAY7_ERROR_BIT constant
        RELAY8_ERROR_BIT = (1UL << 23UL)   // Must match RELAY8_ERROR_BIT constant
    };

    enum class RelayMode {
        NORMAL,
        LATCHED,
        MOMENTARY,
        DELAY
    };

    /**
     * @brief Relay control actions
     *
     * IMPORTANT: Command values sent to hardware differ from status values read back:
     * - ON command sends 0x0100, but status reads return 0x0001 for ON
     * - OFF command sends 0x0200, but status reads return 0x0000 for OFF
     */
    enum class RelayAction {
        ON,         // Send 0x0100 to turn relay ON
        OFF,        // Send 0x0200 to turn relay OFF
        TOGGLE,
        LATCH,
        MOMENTARY,
        DELAY,
        ALL_ON,     // Send 0x0700 to turn all relays ON (addr 0x0000 only)
        ALL_OFF,    // Send 0x0800 to turn all relays OFF (addr 0x0000 only)
        NUM_ACTIONS
    };

    enum class RelayErrorCode {
        SUCCESS,
        INVALID_INDEX,
        MODBUS_ERROR,
        TIMEOUT,
        MUTEX_ERROR,
        NOT_INITIALIZED,
        UNKNOWN_ERROR
    };

    // Helper functions for type conversion
    inline int toUnderlyingType(RelayAction action) {
        return static_cast<int>(action);
    }

    inline EventBits_t toUnderlyingType(RelayUpdateBits bit) {
        return static_cast<EventBits_t>(bit);
    }

    // The relay state struct (optimized with bit fields)
    struct Relay {
        TickType_t lastUpdateTime{0};         // Track when state was last updated (4 bytes)
        uint8_t flags{0x04};                  // Bit field for state tracking (1 byte)
                                             // Bit 0: isOn (default: false)
                                             // Bit 1-2: mode (default: NORMAL = 0)
                                             // Bit 3: lastCommandSuccess (default: true = 1)
                                             // Bit 4: isStateConfirmed (default: false)
                                             // Bit 5-7: reserved

        // Helper methods for bit field access
        bool isOn() const { return flags & 0x01; }
        void setOn(bool on) {
            if (on) flags |= 0x01;
            else flags &= ~0x01;
        }

        RelayMode getMode() const { return static_cast<RelayMode>((flags >> 1) & 0x03); }
        void setMode(RelayMode mode) {
            flags = (flags & ~0x06) | ((static_cast<uint8_t>(mode) & 0x03) << 1);
        }

        bool lastCommandSuccess() const { return flags & 0x08; }
        void setLastCommandSuccess(bool success) {
            if (success) flags |= 0x08;
            else flags &= ~0x08;
        }

        bool isStateConfirmed() const { return flags & 0x10; }
        void setStateConfirmed(bool confirmed) {
            if (confirmed) flags |= 0x10;
            else flags &= ~0x10;
        }
    };

    /**
     * @brief Default hardware configuration for all 8 relays
     *
     * This constexpr array lives in flash memory and provides the default
     * hardware configuration for a standard RYN4 relay module. Applications
     * can reference this or create their own constexpr config.
     */
    static constexpr std::array<base::RelayHardwareConfig, 8> DEFAULT_HARDWARE_CONFIG = {{
        {1, RELAY_ON_BITS[0], RELAY_OFF_BITS[0], RELAY_STATUS_BITS[0], RELAY_UPDATE_BITS[0], RELAY_ERROR_BITS[0], false},
        {2, RELAY_ON_BITS[1], RELAY_OFF_BITS[1], RELAY_STATUS_BITS[1], RELAY_UPDATE_BITS[1], RELAY_ERROR_BITS[1], false},
        {3, RELAY_ON_BITS[2], RELAY_OFF_BITS[2], RELAY_STATUS_BITS[2], RELAY_UPDATE_BITS[2], RELAY_ERROR_BITS[2], false},
        {4, RELAY_ON_BITS[3], RELAY_OFF_BITS[3], RELAY_STATUS_BITS[3], RELAY_UPDATE_BITS[3], RELAY_ERROR_BITS[3], false},
        {5, RELAY_ON_BITS[4], RELAY_OFF_BITS[4], RELAY_STATUS_BITS[4], RELAY_UPDATE_BITS[4], RELAY_ERROR_BITS[4], false},
        {6, RELAY_ON_BITS[5], RELAY_OFF_BITS[5], RELAY_STATUS_BITS[5], RELAY_UPDATE_BITS[5], RELAY_ERROR_BITS[5], false},
        {7, RELAY_ON_BITS[6], RELAY_OFF_BITS[6], RELAY_STATUS_BITS[6], RELAY_UPDATE_BITS[6], RELAY_ERROR_BITS[6], false},
        {8, RELAY_ON_BITS[7], RELAY_OFF_BITS[7], RELAY_STATUS_BITS[7], RELAY_UPDATE_BITS[7], RELAY_ERROR_BITS[7], false}
    }};

} // namespace ryn4
