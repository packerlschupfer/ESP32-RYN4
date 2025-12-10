// BYPASS_INIT_TEST.cpp
// Minimal test to verify ModbusRTU communication without RYN4 initialization

#include <Arduino.h>
#include <esp32ModbusRTU.h>

esp32ModbusRTU modbus(&Serial1, 4);  // DE/RE pin = 4

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n=== Bypass Init Test ===");
    
    // Initialize Serial1 for RS485
    Serial1.begin(9600, SERIAL_8N1, 36, 4);  // RX=36, TX=4
    Serial1.setRxBufferSize(256);
    
    // Initialize Modbus
    modbus.onData([](uint8_t slaveAddress, esp32Modbus::FunctionCode fc, 
                     uint8_t* data, size_t length) {
        Serial.printf("Response from slave %d, FC=%02X, len=%d\n", 
                      slaveAddress, fc, length);
        for (size_t i = 0; i < length; i++) {
            Serial.printf("%02X ", data[i]);
        }
        Serial.println();
    });
    
    modbus.onError([](esp32Modbus::Error error) {
        Serial.printf("Modbus error: %d\n", static_cast<int>(error));
    });
    
    modbus.begin();
    delay(100);
    
    Serial.println("Sending test read to address 2...");
}

void loop() {
    static unsigned long lastTest = 0;
    
    if (millis() - lastTest > 5000) {
        lastTest = millis();
        
        // Test read register 0xFC (return delay)
        Serial.println("\nReading register 0xFC from slave 2...");
        modbus.readHoldingRegisters(2, 0xFC, 1);
    }
    
    delay(10);
}

// To use this test:
// 1. Create new PlatformIO project
// 2. Copy this code to src/main.cpp
// 3. Add to platformio.ini:
//    lib_deps = emelianov/modbus-esp8266
// 4. Upload and monitor
// 5. Should see responses if hardware is working