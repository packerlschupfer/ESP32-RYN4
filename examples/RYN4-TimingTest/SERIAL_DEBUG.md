# Serial/ModbusRTU Debug Guide

## Issue Identified
- Commands are being sent but no valid responses received
- Garbage data appearing after TX frames
- mbpoll works, so hardware is OK

## Possible Software Issues

### 1. Serial1 Configuration
The ESP32 Serial1 might not be configured correctly for RS485 half-duplex.

**Add this debug code to setup():**
```cpp
// After Serial1.begin()
Serial1.setRxBufferSize(256);
Serial1.setRxTimeout(2);  // 2 symbols timeout
```

### 2. RS485 Direction Control
Check if you need to control DE/RE pins:
```cpp
#define RS485_DE_PIN 4  // Or your actual pin
pinMode(RS485_DE_PIN, OUTPUT);
digitalWrite(RS485_DE_PIN, LOW);  // Receive mode by default
```

### 3. ModbusRTU Echo Issue
The garbage after TX might be echo from the RS485 transceiver.

**Try this in setupModbus():**
```cpp
// Disable local echo if your RS485 transceiver echoes
modbusMaster.setLocalEcho(false);  // If this method exists
```

### 4. Timing Issue
Add delays between TX and expected RX:
```cpp
// In RYN4Control.cpp, after writeSingleRegister
vTaskDelay(pdMS_TO_TICKS(10));  // Give time for response
```

### 5. Debug Serial Data
Add this to see raw serial data:
```cpp
// In mainHandleData callback
void mainHandleData(uint8_t slaveAddress, esp32Modbus::FunctionCode fc, 
                    uint8_t* data, size_t length) {
    LOG_INFO("MODBUS", "RX from slave %d: FC=%02X, len=%d", slaveAddress, fc, length);
    char hex[64];
    int pos = 0;
    for (int i = 0; i < length && i < 20; i++) {
        pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", data[i]);
    }
    LOG_INFO("MODBUS", "Data: %s", hex);
    // ... rest of handler
}
```

## Most Likely Cause
Since mbpoll works but esp32ModbusRTU doesn't, the issue is likely:
1. **Serial buffer/timeout configuration**
2. **RS485 direction control timing**
3. **ModbusRTU library compatibility with your RS485 transceiver**

## Quick Test
Try using raw Serial1 communication to verify:
```cpp
void testRawModbus() {
    uint8_t query[] = {0x02, 0x03, 0x00, 0xFC, 0x00, 0x01, 0x44, 0x09};
    
    Serial1.write(query, 8);
    Serial1.flush();
    
    delay(50);
    
    LOG_INFO("TEST", "Bytes available: %d", Serial1.available());
    while (Serial1.available()) {
        LOG_INFO("TEST", "RX: 0x%02X", Serial1.read());
    }
}
```