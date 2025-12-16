# RYN4 Modbus Communication Troubleshooting

## Problem Identified
The 12-second delay is caused by Modbus timeouts. The ESP32 is sending commands but the RYN4 module is not responding.

## Diagnostic Log Analysis
```
TX: 02 06 00 00 02 00 88 99  - Write relay 1 (no response)
TX: 02 03 00 FC 00 01 44 09  - Read register 0xFC (no response)
```

Each command times out after 1 second. With ~12 commands during initialization, this explains the 12-second delay.

## Most Common Causes

### 1. Wrong Slave Address (90% of cases)
- Code is using address `0x02` (decimal 2)
- Module might be set to `0x01` (default) or another address
- **Fix**: Check DIP switches on RYN4 module

### 2. Baud Rate Mismatch
- ESP32 configured for one rate, module for another
- Common rates: 9600, 19200, 38400, 115200
- **Fix**: Verify DIP switch baud rate setting

### 3. Wiring Issues
- TX/RX swapped (ESP32 TX → RYN4 RX, ESP32 RX → RYN4 TX)
- Missing ground connection
- Poor RS485 termination
- **Fix**: Double-check all connections

### 4. Power Issues
- RYN4 module not powered properly
- Insufficient power supply current
- **Fix**: Verify module LED indicators

## Quick Test Code

Add this to your setup() to test different addresses:

```cpp
void scanForRYN4() {
    LOG_INFO("SCAN", "Scanning for RYN4 modules...");
    
    for (uint8_t addr = 1; addr <= 10; addr++) {
        LOG_INFO("SCAN", "Testing address 0x%02X...", addr);
        
        // Create temporary RYN4 instance
        RYN4* testDevice = new RYN4(addr, "TEST");
        
        // Quick responsive check
        if (testDevice->isModuleResponsive()) {
            LOG_INFO("SCAN", ">>> FOUND RYN4 at address 0x%02X <<<", addr);
            delete testDevice;
            return;
        }
        
        delete testDevice;
        delay(100);
    }
    
    LOG_ERROR("SCAN", "No RYN4 module found on addresses 1-10");
}
```

## RYN4 DIP Switch Reference

```
DIP 1-6: Slave Address (binary)
  000001 = Address 1 (0x01)
  000010 = Address 2 (0x02)
  000011 = Address 3 (0x03)
  etc.

DIP 7-8: Baud Rate
  00 = 9600
  01 = 19200
  10 = 38400
  11 = 115200
```

## Immediate Solution

1. Check physical module for DIP switch settings
2. Update code to match:
   ```cpp
   #define RYN4_ADDRESS 0x01  // Change to match DIP switches
   ```
3. Or use the scan function above to find the module

Once communication is established, initialization will complete in <500ms instead of 12 seconds.