// CommonModbusDefinitions.h
#ifndef COMMON_MODBUS_DEFINITIONS_H
#define COMMON_MODBUS_DEFINITIONS_H

#include <functional>
#include <cstdint>

// Generic sensor event bits (used for general notifications)
static constexpr uint32_t SENSOR_UPDATE_BIT = (1UL << 0UL);
static constexpr uint32_t SENSOR_ERROR_BIT = (1UL << 1UL);

enum class BaudRate {
    BAUD_1200 = 0,
    BAUD_2400,
    BAUD_4800,
    BAUD_9600,
    BAUD_19200,
    BAUD_38400,
    BAUD_57600,
    BAUD_115200,
    BAUD_FACTORY_RESET,
    ERROR
};

enum class Parity {
    NONE = 0,
    ODD,
    EVEN,
    ERROR
};

struct ModuleSettings {
    bool autoReportEnabled;
    uint8_t autoReportValue;
    uint8_t baudRate;
    uint8_t parity;
    uint8_t rs485Address;
    uint8_t returnDelay;
};

#endif // COMMON_MODBUS_DEFINITIONS_H
