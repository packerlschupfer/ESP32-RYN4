/**
 * @file RYN4AdvancedConfig.cpp
 * @brief Advanced configuration methods for RYN4 relay module
 *
 * Implements configuration APIs based on official manufacturer documentation:
 * - Device information reading (type, firmware version)
 * - Hardware configuration verification
 * - Bitmap status reading
 * - Factory reset
 * - Parity and reply delay configuration
 *
 * These methods use configuration registers 0x00F0-0x00FF documented in
 * the official RYN404E/RYN408F manual.
 */

#include "RYN4.h"
#include "ryn4/HardwareRegisters.h"

/**
 * @brief Read complete device identification and configuration
 */
ryn4::RelayResult<RYN4::DeviceInfo> RYN4::readDeviceInfo() {
    RYN4_LOG_D("Reading device information...");

    DeviceInfo info = {};

    // Read device identification block (0x00F0-0x00F2: Device Type, FW Major, FW Minor)
    auto deviceIdResult = readHoldingRegisters(ryn4::hardware::REG_DEVICE_TYPE, 3);
    RYN4_TRACK_MODBUS_RESULT(deviceIdResult);
    if (deviceIdResult.isError() || deviceIdResult.value().size() < 3) {
        RYN4_LOG_E("Failed to read device identification");
        return ryn4::RelayResult<DeviceInfo>(ryn4::RelayErrorCode::MODBUS_ERROR);
    }

    const auto& deviceIdData = deviceIdResult.value();
    info.deviceType = deviceIdData[0];
    info.firmwareMajor = static_cast<uint8_t>(deviceIdData[1] & 0xFF);
    info.firmwareMinor = static_cast<uint8_t>(deviceIdData[2] & 0xFF);

    RYN4_LOG_D("Device Type: 0x%04X, Firmware: v%d.%d",
               info.deviceType, info.firmwareMajor, info.firmwareMinor);

    // Read configuration registers (0x00FC-0x00FF: Reply Delay, Address, Baud, Parity)
    auto configResult = readHoldingRegisters(ryn4::hardware::REG_REPLY_DELAY, 4);
    RYN4_TRACK_MODBUS_RESULT(configResult);
    if (configResult.isError() || configResult.value().size() < 4) {
        RYN4_LOG_E("Failed to read configuration registers");
        return ryn4::RelayResult<DeviceInfo>(ryn4::RelayErrorCode::MODBUS_ERROR);
    }

    // Parse configuration data
    const auto& configData = configResult.value();
    info.replyDelayMs = ryn4::hardware::replyDelayToMs(configData[0]);
    info.configuredAddress = static_cast<uint8_t>(configData[1] & 0xFF);

    // Convert baud rate config value to actual baud rate
    uint8_t baudConfig = static_cast<uint8_t>(configData[2] & 0xFF);
    info.configuredBaudRate = ryn4::hardware::baudRateConfigToValue(
        static_cast<ryn4::hardware::BaudRateConfig>(baudConfig)
    );

    info.configuredParity = static_cast<uint8_t>(configData[3] & 0xFF);

    RYN4_LOG_I("Config: Address=%d, Baud=%lu, Parity=%d, Delay=%dms",
               info.configuredAddress, info.configuredBaudRate,
               info.configuredParity, info.replyDelayMs);

    return ryn4::RelayResult<DeviceInfo>(info);
}

/**
 * @brief Verify hardware configuration matches software expectations
 */
ryn4::RelayResult<bool> RYN4::verifyHardwareConfig() {
    RYN4_LOG_D("Verifying hardware configuration...");

    auto infoResult = readDeviceInfo();
    if (infoResult.isError()) {
        return ryn4::RelayResult<bool>(infoResult.error());
    }

    const DeviceInfo& info = infoResult.value();
    bool configMatches = true;

    // Check slave ID
    if (info.configuredAddress != _slaveID) {
        RYN4_LOG_W("Slave ID mismatch: DIP switches=%d, software=%d",
                   info.configuredAddress, _slaveID);
        configMatches = false;
    }

    // Note: We don't have the baud rate stored in this class to compare,
    // but the user can check it from the DeviceInfo structure

    if (configMatches) {
        RYN4_LOG_I("Hardware configuration verified");
    } else {
        RYN4_LOG_W("Hardware configuration mismatch detected!");
    }

    return ryn4::RelayResult<bool>(configMatches);
}

/**
 * @brief Read all relay states as a bitmap
 *
 * @param updateCache If true, updates internal relay state cache and sets event bits
 */
ryn4::RelayResult<uint16_t> RYN4::readBitmapStatus(bool updateCache) {
    RYN4_LOG_D("Reading relay status bitmap%s...", updateCache ? " (updating cache)" : "");

    // Use STATUS priority for status reads - lowest priority to avoid blocking sensor reads
    auto bitmapResult = readHoldingRegistersWithPriority(ryn4::hardware::REG_STATUS_BITMAP, 1, esp32Modbus::STATUS);
    RYN4_TRACK_MODBUS_RESULT(bitmapResult);
    if (bitmapResult.isError() || bitmapResult.value().empty()) {
        RYN4_LOG_E("Failed to read status bitmap");
        return ryn4::RelayResult<uint16_t>(ryn4::RelayErrorCode::MODBUS_ERROR);
    }

    uint16_t bitmap = bitmapResult.value()[0];
    RYN4_LOG_D("Status bitmap: 0x%04X", bitmap);

    // Update internal state cache if requested (for verification)
    if (updateCache) {
        MutexGuard lock(instanceMutex, mutexTimeout);
        if (lock) {
            for (int i = 0; i < NUM_RELAYS; i++) {
                bool state = (bitmap >> i) & 0x01;
                bool previousState = relays[i].isOn();

                relays[i].setOn(state);
                relays[i].setStateConfirmed(true);
                relays[i].lastUpdateTime = xTaskGetTickCount();

                if (previousState != state) {
                    setUpdateEventBits(ryn4::RELAY_UPDATE_BITS[i]);
                    RYN4_LOG_I("Relay %d state changed: %s", i + 1, state ? "ON" : "OFF");
                }
            }
            // Signal that relay config/status has been read successfully
            setInitializationBit(InitBits::RELAY_CONFIG);
        } else {
            RYN4_LOG_E("Failed to acquire mutex for cache update");
        }
    }

    // Log individual relay states if debug enabled
#ifdef RYN4_DEBUG
    for (int i = 0; i < 8; i++) {
        bool relayOn = (bitmap >> i) & 0x01;
        RYN4_LOG_V("  Relay %d: %s", i+1, relayOn ? "ON" : "OFF");
    }
#endif

    return ryn4::RelayResult<uint16_t>(bitmap);
}

/**
 * @brief Perform software factory reset
 */
ryn4::RelayResult<void> RYN4::factoryReset() {
    RYN4_LOG_W("Performing factory reset...");

    // Write 0x0000 to factory reset register (0x00FB)
    auto resetResult = writeSingleRegister(ryn4::hardware::REG_FACTORY_RESET, 0x0000);
    RYN4_TRACK_MODBUS_RESULT(resetResult);
    if (resetResult.isError()) {
        RYN4_LOG_E("Factory reset command failed");
        return ryn4::RelayResult<void>(ryn4::RelayErrorCode::MODBUS_ERROR);
    }

    RYN4_LOG_W("Factory reset command sent - POWER CYCLE module to complete reset");
    RYN4_LOG_I("Reset will clear: Reply delay, Parity");
    RYN4_LOG_I("Reset will NOT clear: Slave ID (DIP), Baud rate (DIP)");

    return ryn4::RelayResult<void>(ryn4::RelayErrorCode::SUCCESS);
}

/**
 * @brief Get current reply delay setting
 */
ryn4::RelayResult<uint16_t> RYN4::getReplyDelay() {
    RYN4_LOG_D("Reading reply delay...");

    auto delayResult = readHoldingRegisters(ryn4::hardware::REG_REPLY_DELAY, 1);
    RYN4_TRACK_MODBUS_RESULT(delayResult);
    if (delayResult.isError() || delayResult.value().empty()) {
        RYN4_LOG_E("Failed to read reply delay");
        return ryn4::RelayResult<uint16_t>(ryn4::RelayErrorCode::MODBUS_ERROR);
    }

    uint16_t delayReg = delayResult.value()[0];
    uint16_t delayMs = ryn4::hardware::replyDelayToMs(delayReg);
    RYN4_LOG_D("Reply delay: %dms (register value: %d)", delayMs, delayReg);

    return ryn4::RelayResult<uint16_t>(delayMs);
}

/**
 * @brief Set reply delay
 */
ryn4::RelayResult<void> RYN4::setReplyDelay(uint16_t delayMs) {
    RYN4_LOG_I("Setting reply delay to %dms...", delayMs);

    // Validate and convert to register value
    if (delayMs > 1000) {
        RYN4_LOG_W("Delay %dms exceeds max 1000ms, will reset to 0ms on power-up", delayMs);
    }

    uint16_t regValue = ryn4::hardware::msToReplyDelay(delayMs);
    uint16_t actualDelayMs = ryn4::hardware::replyDelayToMs(regValue);

    if (actualDelayMs != delayMs) {
        RYN4_LOG_D("Delay rounded from %dms to %dms (40ms increments)", delayMs, actualDelayMs);
    }

    // Write to register 0x00FC
    auto writeResult = writeSingleRegister(ryn4::hardware::REG_REPLY_DELAY, regValue);
    RYN4_TRACK_MODBUS_RESULT(writeResult);
    if (writeResult.isError()) {
        RYN4_LOG_E("Failed to set reply delay");
        return ryn4::RelayResult<void>(ryn4::RelayErrorCode::MODBUS_ERROR);
    }

    RYN4_LOG_I("Reply delay set to %dms (register value: %d)", actualDelayMs, regValue);

    return ryn4::RelayResult<void>(ryn4::RelayErrorCode::SUCCESS);
}
