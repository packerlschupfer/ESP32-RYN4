# IPv6 Configuration Notes

## Current Status
IPv6 cannot be disabled via build flags in the Arduino-ESP32 framework v3.x because:
1. The framework has IPv6 enabled in its precompiled libraries
2. Setting `-D LWIP_IPV6=0` or `-D CONFIG_LWIP_IPV6=0` causes redefinition errors
3. The sdkconfig.h and lwipopts.h files have hardcoded IPv6 settings

## Alternative Approaches

### 1. Use ESP-IDF Instead of Arduino
ESP-IDF allows full control over IPv6 via menuconfig:
```
CONFIG_LWIP_IPV6=n
```

### 2. Custom Arduino-ESP32 Build
Build your own Arduino-ESP32 framework with IPv6 disabled:
1. Clone https://github.com/espressif/arduino-esp32
2. Modify tools/sdk/sdkconfig
3. Set `CONFIG_LWIP_IPV6=n`
4. Run build scripts to generate new framework

### 3. Runtime Optimization (Current Approach)
Since we can't disable IPv6 at compile time, we can:
- Not use any IPv6 features
- Not initialize IPv6 addresses
- Focus on other optimizations

## Flash Impact
Based on ESP-IDF projects, disabling IPv6 typically saves:
- 30-50KB of flash space
- Some RAM for IPv6 address structures
- Reduced code paths in network stack

## Current Optimizations Applied
1. mDNS disabled (`CONFIG_MDNS_DISABLE=1`)
2. WiFi completely disabled
3. Bluetooth disabled
4. USB features disabled
5. Link-time optimization enabled
6. Dead code elimination active

## Future Considerations
If flash space becomes critical:
1. Switch to ESP-IDF framework
2. Build custom Arduino framework
3. Remove OTA support (~50KB)
4. Use minimal printf implementation