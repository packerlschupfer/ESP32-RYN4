// src/ryn4/HardwareRegisters.h

#pragma once

#include <cstdint>

/**
 * @file HardwareRegisters.h
 * @brief Hardware register map and protocol constants for RYN404E/RYN408F relay modules
 *
 * This file defines the complete Modbus RTU register map and command encoding
 * for the RYN4 relay modules. See HARDWARE.md for detailed protocol documentation.
 *
 * CRITICAL: The RYN4 hardware uses ASYMMETRIC command/status encoding:
 * - Write commands use 0x0100 (ON) / 0x0200 (OFF)
 * - Read responses use 0x0001 (ON) / 0x0000 (OFF)
 *
 * The library handles this conversion automatically.
 */

namespace ryn4 {
namespace hardware {

    // ========== Modbus Function Codes ==========

    /**
     * @brief Modbus function codes supported by RYN4
     *
     * The RYN4 module supports DUAL addressing systems:
     * - Coil-based: FC 0x01/0x05/0x0F
     * - Holding register-based: FC 0x03/0x06/0x10
     *
     * This library uses the holding register method (0x03/0x06/0x10).
     */
    static constexpr uint8_t FC_READ_COILS = 0x01;              ///< Read relay states (coil format)
    static constexpr uint8_t FC_READ_HOLDING_REGISTERS = 0x03;  ///< Read relay status (register format)
    static constexpr uint8_t FC_WRITE_SINGLE_COIL = 0x05;       ///< Write single relay (coil format)
    static constexpr uint8_t FC_WRITE_SINGLE_REGISTER = 0x06;   ///< Control relay (register format)
    static constexpr uint8_t FC_WRITE_MULTIPLE_COILS = 0x0F;    ///< Write multiple relays (coil format)
    static constexpr uint8_t FC_WRITE_MULTIPLE_REGISTERS = 0x10; ///< Write multiple relays (register format)

    // ========== Modbus Register Addresses ==========

    /**
     * @brief Relay channel register addresses (FC 0x06 write, FC 0x03 read)
     *
     * Valid range: 0x0001 to 0x0008 for individual channels
     * Special address: 0x0000 for ALL_ON/ALL_OFF broadcast commands
     */
    static constexpr uint16_t RELAY_REGISTER_START = 0x0001;
    static constexpr uint16_t RELAY_REGISTER_END = 0x0008;
    static constexpr uint16_t RELAY_ALL_CHANNELS = 0x0000;  ///< Broadcast address

    /**
     * @brief Status bitmap registers (alternative read method)
     *
     * These registers provide relay states as a bitmap where each bit
     * represents one relay (bit 0 = relay 1, bit 7 = relay 8).
     * More efficient than reading individual registers.
     */
    static constexpr uint16_t REG_STATUS_BITMAP = 0x0080;  ///< Relay status bitmap (bits 0-7)

    /**
     * @brief Configuration registers (read/write)
     *
     * Device identification and configurable parameters.
     * All addresses are in Modbus RTU register space.
     */
    static constexpr uint16_t REG_DEVICE_TYPE = 0x00F0;     ///< Hardware model ID (R)
    static constexpr uint16_t REG_FIRMWARE_MAJOR = 0x00F1;  ///< Firmware major version (R)
    static constexpr uint16_t REG_FIRMWARE_MINOR = 0x00F2;  ///< Firmware minor version (R)
    static constexpr uint16_t REG_FACTORY_RESET = 0x00FB;   ///< Factory reset command (W: 0x0000)
    static constexpr uint16_t REG_REPLY_DELAY = 0x00FC;     ///< Response delay (R/W: 0-25, 40ms units)
    static constexpr uint16_t REG_RS485_ADDRESS = 0x00FD;   ///< DIP switch address (R)
    static constexpr uint16_t REG_BAUD_RATE = 0x00FE;       ///< Baud rate config (R: 0-3)
    static constexpr uint16_t REG_PARITY = 0x00FF;          ///< Parity setting (R/W: 0-2, power cycle req'd)

    /**
     * @brief Channel register address mapping
     *
     * Index 0 = Channel 1 (register 0x0001)
     * Index 1 = Channel 2 (register 0x0002)
     * ...
     * Index 7 = Channel 8 (register 0x0008)
     *
     * Example:
     * @code
     * uint16_t reg = CHANNEL_REGISTERS[0];  // 0x0001 (Channel 1)
     * @endcode
     */
    static constexpr uint16_t CHANNEL_REGISTERS[8] = {
        0x0001, 0x0002, 0x0003, 0x0004,
        0x0005, 0x0006, 0x0007, 0x0008
    };

    // ========== Control Command Codes (Write Values) ==========

    /**
     * @brief Relay control command codes (FC 0x06)
     *
     * IMPORTANT: These values are sent TO the device for control.
     * Status reads return DIFFERENT values (see STATUS_VALUES below).
     *
     * Write: 0x0100 (ON command) → Read back: 0x0001 (ON status)
     * Write: 0x0200 (OFF command) → Read back: 0x0000 (OFF status)
     */
    static constexpr uint16_t CMD_ON = 0x0100;      ///< Turn relay ON (energize)
    /**
     * @brief Turn relay OFF (de-energize)
     * @warning DOES NOT cancel active DELAY timers! Use CMD_DELAY_BASE (0x0600) to cancel delays.
     * Hardware verified 2025-12-14: OFF command is ignored while a delay timer is running.
     * To reliably turn OFF a relay with an active delay, send DELAY 0 (0x0600) instead.
     */
    static constexpr uint16_t CMD_OFF = 0x0200;
    static constexpr uint16_t CMD_TOGGLE = 0x0300;    ///< Self-locking toggle
    static constexpr uint16_t CMD_LATCH = 0x0400;     ///< Inter-locking (one relay ON at a time)
    static constexpr uint16_t CMD_MOMENTARY = 0x0500; ///< 1-second pulse then OFF
    /**
     * @brief Delay command base - OR with seconds (0x0600-0x06FF)
     * Relay turns ON immediately, then OFF after specified seconds.
     * Special: DELAY 0 (0x0600) cancels active delay and turns OFF immediately.
     * This is the ONLY reliable way to cancel a running delay timer.
     * Hardware verified 2025-12-14.
     */
    static constexpr uint16_t CMD_DELAY_BASE = 0x0600;
    static constexpr uint16_t CMD_ALL_ON = 0x0700;   ///< Turn all relays ON (addr 0x0000 only)
    static constexpr uint16_t CMD_ALL_OFF = 0x0800;  ///< Turn all relays OFF (addr 0x0000 only)

    /**
     * @brief Create delay command value
     *
     * Delay mode: Relay turns ON for specified duration, then turns OFF automatically.
     *
     * @param seconds Delay duration (0-255 seconds)
     * @return Command value to write (0x0600-0x06FF)
     *
     * Example:
     * @code
     * uint16_t cmd = makeDelayCommand(10);  // 0x060A (10 second delay)
     * @endcode
     */
    inline constexpr uint16_t makeDelayCommand(uint8_t seconds) {
        return CMD_DELAY_BASE | (seconds & 0xFF);
    }

    // ========== Status Response Values (Read Values) ==========

    /**
     * @brief Relay status response values (FC 0x03)
     *
     * CRITICAL: Status read values differ from command write values!
     * - Send CMD_ON (0x0100) → Read back STATUS_ON (0x0001)
     * - Send CMD_OFF (0x0200) → Read back STATUS_OFF (0x0000)
     *
     * This asymmetry is a hardware protocol characteristic of the RYN4 module.
     * The library handles conversion automatically.
     */
    static constexpr uint16_t STATUS_ON = 0x0001;   ///< Relay is ON (contact closed)
    static constexpr uint16_t STATUS_OFF = 0x0000;  ///< Relay is OFF (contact open)

    // ========== Hardware Configuration Limits ==========

    /**
     * @brief Slave ID configuration limits
     *
     * The RYN4 uses a 6-bit DIP switch array (A0-A5) for address selection.
     * Valid range: 0x00 to 0x3F (0 to 63 decimal)
     */
    static constexpr uint8_t MIN_SLAVE_ID = 0x00;
    static constexpr uint8_t MAX_SLAVE_ID = 0x3F;  ///< 6-bit DIP switches (2^6 - 1)

    /**
     * @brief Number of relay channels
     *
     * RYN408F: 8 channels
     * RYN404E: 4 channels (only uses first 4 channels)
     */
    static constexpr uint8_t MAX_CHANNELS_RYN408F = 8;
    static constexpr uint8_t MAX_CHANNELS_RYN404E = 4;

    /**
     * @brief Supported baud rates (configurable via M1/M2 jumper pads)
     *
     * UPDATED: Official manual confirms extended baud rate support
     *
     * M1=OPEN, M2=OPEN: 9600 (default)
     * M1=SHORT, M2=OPEN: 19200
     * M1=OPEN, M2=SHORT: 38400
     * M1=SHORT, M2=SHORT: 115200
     *
     * Note: Earlier documents showed 2400/4800, but official manual uses
     * the DIP switch combinations for higher speeds.
     */
    static constexpr uint32_t SUPPORTED_BAUD_RATES[] = {
        9600, 19200, 38400, 115200
    };
    static constexpr uint32_t DEFAULT_BAUD_RATE = 9600;

    /**
     * @brief Baud rate configuration values (read from register 0x00FE)
     */
    enum class BaudRateConfig : uint16_t {
        BAUD_9600 = 0,    ///< 9600 BPS (default, M1=OPEN M2=OPEN)
        BAUD_19200 = 1,   ///< 19200 BPS (M1=SHORT M2=OPEN)
        BAUD_38400 = 2,   ///< 38400 BPS (M1=OPEN M2=SHORT)
        BAUD_115200 = 3   ///< 115200 BPS (M1=SHORT M2=SHORT)
    };

    /**
     * @brief Parity configuration values (read/write register 0x00FF)
     */
    enum class ParityConfig : uint16_t {
        PARITY_NONE = 0,  ///< No parity (8N1) - default
        PARITY_EVEN = 1,  ///< Even parity (8E1)
        PARITY_ODD = 2    ///< Odd parity (8O1)
    };

    /**
     * @brief Timing constraints
     */
    static constexpr uint8_t MAX_DELAY_SECONDS = 255;  ///< Maximum delay command duration
    static constexpr uint8_t MOMENTARY_DURATION_SECONDS = 1;  ///< Fixed momentary mode duration

    /**
     * @brief Reply delay units (register 0x00FC)
     *
     * CORRECTED: Official manual specifies 40ms units (NOT 5ms from cheat sheet)
     * Range: 0-25 (0ms - 1000ms)
     */
    static constexpr uint8_t REPLY_DELAY_UNIT_MS = 40;  ///< 40ms per unit (official manual)
    static constexpr uint16_t MAX_REPLY_DELAY = 25;      ///< Max value (1000ms)

    // ========== Utility Functions ==========

    /**
     * @brief Validate slave ID is in hardware range
     *
     * @param slaveId Slave ID to validate
     * @return true if valid (0x00-0x3F), false otherwise
     */
    inline constexpr bool isValidSlaveId(uint8_t slaveId) {
        return (slaveId >= MIN_SLAVE_ID) && (slaveId <= MAX_SLAVE_ID);
    }

    /**
     * @brief Validate channel index (0-based)
     *
     * @param channelIndex Channel index (0-7 for RYN408F, 0-3 for RYN404E)
     * @param maxChannels Maximum channels (8 for RYN408F, 4 for RYN404E)
     * @return true if valid, false otherwise
     */
    inline constexpr bool isValidChannelIndex(uint8_t channelIndex, uint8_t maxChannels = MAX_CHANNELS_RYN408F) {
        return channelIndex < maxChannels;
    }

    /**
     * @brief Validate relay number (1-based)
     *
     * @param relayNumber Relay number (1-8 for RYN408F, 1-4 for RYN404E)
     * @param maxChannels Maximum channels (8 for RYN408F, 4 for RYN404E)
     * @return true if valid, false otherwise
     */
    inline constexpr bool isValidRelayNumber(uint8_t relayNumber, uint8_t maxChannels = MAX_CHANNELS_RYN408F) {
        return (relayNumber >= 1) && (relayNumber <= maxChannels);
    }

    /**
     * @brief Convert 0-based channel index to 1-based relay number
     *
     * @param channelIndex Channel index (0-7)
     * @return Relay number (1-8)
     */
    inline constexpr uint8_t indexToRelayNumber(uint8_t channelIndex) {
        return channelIndex + 1;
    }

    /**
     * @brief Convert 1-based relay number to 0-based channel index
     *
     * @param relayNumber Relay number (1-8)
     * @return Channel index (0-7)
     */
    inline constexpr uint8_t relayNumberToIndex(uint8_t relayNumber) {
        return relayNumber - 1;
    }

    /**
     * @brief Get Modbus register address for relay number (1-based)
     *
     * @param relayNumber Relay number (1-8)
     * @return Register address (0x0001-0x0008)
     */
    inline constexpr uint16_t relayNumberToRegister(uint8_t relayNumber) {
        return CHANNEL_REGISTERS[relayNumberToIndex(relayNumber)];
    }

    /**
     * @brief Validate baud rate is supported by hardware
     *
     * @param baudRate Baud rate to validate
     * @return true if supported (9600, 19200, 38400, 115200), false otherwise
     */
    inline bool isValidBaudRate(uint32_t baudRate) {
        for (const auto& rate : SUPPORTED_BAUD_RATES) {
            if (rate == baudRate) {
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Convert baud rate configuration value to actual baud rate
     *
     * @param configValue Value read from register 0x00FE
     * @return Baud rate in BPS (9600, 19200, 38400, or 115200)
     */
    inline constexpr uint32_t baudRateConfigToValue(BaudRateConfig config) {
        switch (config) {
            case BaudRateConfig::BAUD_9600: return 9600;
            case BaudRateConfig::BAUD_19200: return 19200;
            case BaudRateConfig::BAUD_38400: return 38400;
            case BaudRateConfig::BAUD_115200: return 115200;
            default: return 9600; // Default fallback
        }
    }

    /**
     * @brief Convert parity configuration value to string
     *
     * @param config Parity configuration value
     * @return Parity name ("None", "Even", "Odd")
     */
    inline constexpr const char* parityConfigToString(ParityConfig config) {
        switch (config) {
            case ParityConfig::PARITY_NONE: return "None";
            case ParityConfig::PARITY_EVEN: return "Even";
            case ParityConfig::PARITY_ODD: return "Odd";
            default: return "Unknown";
        }
    }

    /**
     * @brief Convert reply delay register value to milliseconds
     *
     * @param regValue Value from register 0x00FC (0-25)
     * @return Delay in milliseconds (0-1000ms)
     */
    inline constexpr uint16_t replyDelayToMs(uint16_t regValue) {
        return (regValue & 0xFF) * REPLY_DELAY_UNIT_MS;  // 40ms units
    }

    /**
     * @brief Convert milliseconds to reply delay register value
     *
     * @param delayMs Delay in milliseconds (0-1000)
     * @return Register value (0-25), clamped to maximum
     */
    inline constexpr uint16_t msToReplyDelay(uint16_t delayMs) {
        uint16_t value = (delayMs + (REPLY_DELAY_UNIT_MS / 2)) / REPLY_DELAY_UNIT_MS; // Round
        return (value > MAX_REPLY_DELAY) ? MAX_REPLY_DELAY : value;
    }

    /**
     * @brief Extract delay value from delay command
     *
     * @param delayCommand Delay command value (0x0600-0x06FF)
     * @return Delay in seconds (0-255)
     */
    inline constexpr uint8_t extractDelaySeconds(uint16_t delayCommand) {
        return static_cast<uint8_t>(delayCommand & 0xFF);
    }

    /**
     * @brief Check if command is a delay command
     *
     * @param command Command value
     * @return true if delay command (0x0600-0x06FF)
     */
    inline constexpr bool isDelayCommand(uint16_t command) {
        return (command & 0xFF00) == CMD_DELAY_BASE;
    }

    /**
     * @brief Convert status value to boolean
     *
     * @param statusValue Status value from read (0x0001 or 0x0000)
     * @return true if ON (0x0001), false if OFF (0x0000)
     */
    inline constexpr bool statusToBool(uint16_t statusValue) {
        return statusValue == STATUS_ON;
    }

    /**
     * @brief Convert boolean to command value
     *
     * @param state Desired state (true=ON, false=OFF)
     * @return Command value (0x0100 for ON, 0x0200 for OFF)
     */
    inline constexpr uint16_t boolToCommand(bool state) {
        return state ? CMD_ON : CMD_OFF;
    }

    // ========== Protocol Documentation References ==========

    /**
     * Hardware documentation references:
     *
     * - Complete protocol specification: HARDWARE.md
     * - Manufacturer datasheet: /old/Documents/R4D8A08-Relay/8 Channel Rail RS485 Relay commamd.pdf
     * - Library usage examples: examples/ directory
     * - DIP switch configuration: HARDWARE.md section "DIP Switch Address Selection"
     * - Jumper pad configuration: HARDWARE.md section "Baud Rate Selection"
     * - Wiring diagrams: HARDWARE.md section "Wiring Diagram"
     *
     * Key Protocol Characteristics:
     *
     * 1. ASYMMETRIC ENCODING:
     *    Write: 0x0100 (ON), 0x0200 (OFF)
     *    Read:  0x0001 (ON), 0x0000 (OFF)
     *
     * 2. FUNCTION CODES:
     *    FC 0x06: Write Single Register (control)
     *    FC 0x03: Read Holding Registers (status)
     *    (Does NOT use FC 0x01/0x05 coil functions)
     *
     * 3. ADDRESSING:
     *    Individual: 0x0001-0x0008 (channels 1-8)
     *    Broadcast:  0x0000 (ALL_ON/ALL_OFF only)
     *
     * 4. COMMAND MODES:
     *    - ON/OFF: Basic ON/OFF control
     *    - TOGGLE: Self-locking toggle
     *    - LATCH: Inter-locking (mutual exclusion)
     *    - MOMENTARY: 1-second pulse
     *    - DELAY: Timed ON (0-255 seconds)
     *
     * 5. HARDWARE CONFIGURATION:
     *    - Slave ID: 6-bit DIP switches A0-A5 (0x00-0x3F)
     *    - Baud rate: Jumper pads M1/M2 (9600/19200/38400/115200)
     *    - Command mode: Jumper pad M0 (OPEN=Modbus, SHORT=AT)
     *    - Library requires M0=OPEN (Modbus RTU mode)
     */

} // namespace hardware
} // namespace ryn4
