# ESP32-RYN4 v0.1.1 - New Features & Documentation

**Release Date**: 2025-12-07
**Previous Version**: v0.1.0 (2025-12-04)

---

## 🎉 What's New

This release adds **advanced configuration features** and **professional-grade hardware documentation** based on official manufacturer manuals from 485io.com.

---

## 📚 New API Methods

### 1. Device Information & Diagnostics

#### `readDeviceInfo()` - Complete Device Identification

```cpp
auto info = ryn4.readDeviceInfo();
if (info.isOk()) {
    Serial.printf("Device Type: 0x%04X\n", info.value.deviceType);
    Serial.printf("Firmware: v%d.%d\n", info.value.firmwareMajor, info.value.firmwareMinor);
    Serial.printf("Slave ID: %d (from DIP switches)\n", info.value.configuredAddress);
    Serial.printf("Baud Rate: %lu BPS (from jumpers)\n", info.value.configuredBaudRate);
    Serial.printf("Parity: %d\n", info.value.configuredParity);
    Serial.printf("Reply Delay: %dms\n", info.value.replyDelayMs);
}
```

**Returns:**
- `DeviceInfo` structure with hardware ID, firmware version, and configuration
- Read from registers 0x00F0-0x00F2 (device ID) and 0x00FC-0x00FF (config)

**Use Cases:**
- Auto-detect hardware version
- Verify firmware compatibility
- Diagnostics and troubleshooting
- Configuration validation

---

#### `verifyHardwareConfig()` - Automatic Configuration Check

```cpp
auto result = ryn4.verifyHardwareConfig();
if (result.isError()) {
    Serial.println("ERROR: Could not verify config");
} else if (!result.value) {
    Serial.println("WARNING: DIP switches don't match software config!");
}
```

**What it checks:**
- DIP switch slave ID vs software slave ID
- Logs warnings if mismatch detected

**Use Cases:**
- Startup validation
- Catch configuration errors early
- Production diagnostics

---

### 2. Efficient Status Reading

#### `readBitmapStatus()` - Batch Status via Bitmap

```cpp
auto bitmap = ryn4.readBitmapStatus();
if (bitmap.isOk()) {
    // Single register read (0x0080) contains all 8 relay states
    for (int i = 0; i < 8; i++) {
        bool relayOn = (bitmap.value >> i) & 0x01;
        Serial.printf("Relay %d: %s\n", i+1, relayOn ? "ON" : "OFF");
    }
}
```

**Performance:**
- **Old method**: 8 separate Modbus reads
- **New method**: 1 Modbus read for all relays
- **~8x faster** for reading all states

---

### 3. Module Configuration

#### `setReplyDelay()` / `getReplyDelay()` - Response Timing

```cpp
// Read current delay
auto delay = ryn4.getReplyDelay();
Serial.printf("Current delay: %dms\n", delay.value);

// Set 200ms delay
ryn4.setReplyDelay(200);
```

**Configuration:**
- Range: 0-1000ms (40ms increments)
- Register: 0x00FC
- Units: 40ms per unit (value 0-25)

**Use Cases:**
- Optimize RS485 bus timing
- Reduce collisions on multi-device bus
- Fine-tune performance vs reliability

---

#### `setParity()` - Parity Configuration

```cpp
// Set to Even parity
auto result = ryn4.setParity(1);  // 0=None, 1=Even, 2=Odd
if (result.isOk()) {
    Serial.println("Parity set - POWER CYCLE module to activate");
}
```

**Important:**
- ⚠️ Requires **power cycle** to take effect
- ⚠️ Update your code's serial config after reboot
- Values >2 reset to 0 (None) on power-up

---

#### `factoryReset()` - Software Factory Reset

```cpp
auto result = ryn4.factoryReset();
if (result.isOk()) {
    Serial.println("Factory reset sent - power cycle module");
}
```

**What gets reset:**
- Reply delay → 0ms
- Parity → None (8N1)

**What does NOT reset:**
- Slave ID (DIP switches)
- Baud rate (jumpers)

**Alternative**: Short RES jumper on board for 5 seconds, then power cycle

---

## 📖 Documentation Improvements

### New Documentation Files

1. **HARDWARE.md** (~800 lines)
   - Complete hardware specifications
   - Power consumption, relay ratings
   - DIP switch configuration tables
   - Jumper pad settings
   - Complete register map (0x0000-0x00FF)
   - Modbus protocol details
   - Wiring diagrams
   - Troubleshooting guide
   - Manufacturer links

2. **src/ryn4/HardwareRegisters.h** (~400 lines)
   - All register address constants
   - Configuration register enums
   - Utility functions for conversions
   - Extensive inline documentation
   - Protocol reference comments

3. **examples/hardware_setup/README.md** (~600 lines)
   - Step-by-step setup guide
   - DIP switch calculator
   - Hardware configuration checklist
   - Testing procedures
   - Troubleshooting flowchart

4. **examples/hardware_setup/test_communication.ino** (~400 lines)
   - Hardware validation sketch
   - Auto-detects and displays device info
   - Tests all relay channels
   - Configuration verification

5. **examples/hardware_setup/test_advanced_config.ino** (~350 lines)
   - Demonstrates all new API methods
   - Device info reading
   - Bitmap status reading
   - Reply delay configuration
   - Commented parity/factory reset examples

6. **DOCUMENTATION_UPDATES.md**
   - Complete changelog of documentation improvements
   - Before/after comparison
   - Sources and credits

### Updated Documentation

1. **README.md**
   - Added "Hardware Configuration" section
   - DIP switch and jumper setup instructions
   - Protocol asymmetry warning
   - Links to HARDWARE.md

2. **CLAUDE.md**
   - Added "Hardware Protocol Documentation" section
   - Register map reference
   - Critical protocol details

---

## 🔍 Key Discoveries from Official Manuals

### Hardware Specifications
- **Power**: 5.5-6.5mA standby, 31mA/relay @ 12V
- **Relays**: 10A @ 250VAC, **pluggable** T73 type
- **Protection**: Reverse polarity, RS485 TVS surge
- **Dimensions**: Complete physical specs for both models

### Register Map Expanded
- **0x00F0-0x00F2**: Device type, firmware version
- **0x00FB**: Factory reset command
- **0x00FC**: Reply delay (R/W, 40ms units)
- **0x00FD**: RS485 address readback (R)
- **0x00FE**: Baud rate readback (R)
- **0x00FF**: Parity config (R/W, power cycle required)
- **0x0080-0x0081**: Status bitmap (R/W)

### Protocol Details
- **Dual addressing**: Coil-based (FC 01/05/0F) + Register-based (FC 03/06/10)
- **Extended baud rates**: 38400 and 115200 BPS supported
- **Parity is writeable**: Can be configured via Modbus (not just DIP)
- **Reply delay correction**: 40ms units (not 5ms)

---

## 🚀 Migration Guide

### From v0.1.0 to v0.1.1

**No breaking changes!** All existing code continues to work.

**Optional enhancements:**

#### Add startup diagnostics:

```cpp
void setup() {
    RYN4 ryn4(1, "RYN4");
    ryn4.initialize();

    // NEW: Verify hardware config
    auto verify = ryn4.verifyHardwareConfig();
    if (verify.isOk() && !verify.value) {
        Serial.println("WARNING: DIP switches don't match software!");
    }

    // NEW: Display device info
    auto info = ryn4.readDeviceInfo();
    if (info.isOk()) {
        Serial.printf("Running firmware v%d.%d\n",
                     info.value.firmwareMajor,
                     info.value.firmwareMinor);
    }
}
```

#### Optimize status reading:

```cpp
// OLD: Read all relays individually (8 Modbus transactions)
for (int i = 1; i <= 8; i++) {
    bool state;
    ryn4.readRelayStatus(i, state);
}

// NEW: Read all relays at once (1 Modbus transaction)
auto bitmap = ryn4.readBitmapStatus();
if (bitmap.isOk()) {
    for (int i = 0; i < 8; i++) {
        bool relayOn = (bitmap.value >> i) & 0x01;
        // Process relay state...
    }
}
```

---

## 📊 Documentation Quality Comparison

| Feature | v0.1.0 | v0.1.1 |
|---------|--------|--------|
| **Hardware specs** | Basic | ✅ Professional-grade |
| **Register map** | Partial (0x0001-0x0008) | ✅ Complete (0x0000-0x00FF) |
| **Setup guide** | None | ✅ Step-by-step with checklist |
| **Test sketches** | Basic examples | ✅ Validation + diagnostics |
| **Protocol docs** | Inline comments | ✅ Dedicated HARDWARE.md |
| **Config registers** | None | ✅ 9 new registers documented |
| **Wiring diagrams** | None | ✅ ASCII diagrams |
| **Manufacturer links** | None | ✅ Product page + docs |

**Result:** Now **matches ESP32-MB8ART** documentation sophistication! 🎯

---

## 🛠️ Development Notes

### New Source Files

- `src/RYN4AdvancedConfig.cpp` - Implementation of new configuration methods
- Added to `library.json` srcFilter for compilation

### Code Organization

The library is now split into **8 focused modules**:

1. **RYN4.cpp** (362 lines) - Core
2. **RYN4Device.cpp** (339 lines) - IDeviceInstance interface
3. **RYN4Modbus.cpp** (268 lines) - Modbus communication
4. **RYN4State.cpp** (143 lines) - State queries
5. **RYN4Config.cpp** (270 lines) - Configuration
6. **RYN4Control.cpp** (647 lines) - Relay control
7. **RYN4Events.cpp** (71 lines) - Event management
8. **RYN4AdvancedConfig.cpp** (NEW - 240 lines) - Advanced configuration

**Total**: ~2,340 lines of well-organized code

---

## 📦 What's Included

### API Methods (6 new)
✅ `readDeviceInfo()` - Device identification
✅ `verifyHardwareConfig()` - Config validation
✅ `readBitmapStatus()` - Efficient batch read
✅ `factoryReset()` - Software reset
✅ `setParity()` - Parity configuration
✅ `getReplyDelay()` / `setReplyDelay()` - Timing config

### Documentation (7 new/updated files)
✅ HARDWARE.md - Complete hardware reference
✅ HardwareRegisters.h - Register constants
✅ hardware_setup/README.md - Setup guide
✅ test_communication.ino - Basic validation
✅ test_advanced_config.ino - Feature demo
✅ CHANGELOG.md - Version history
✅ DOCUMENTATION_UPDATES.md - Change tracking

### Discovered Features
✅ Extended baud rates (38400, 115200)
✅ Status bitmap register (0x0080)
✅ Configuration registers (0x00F0-0x00FF)
✅ Writeable parity
✅ Factory reset command
✅ Device identification
✅ Pluggable relays

---

## 🙏 Credits

- **Official Documentation**: 485io.com manufacturer manuals
- **Community**: User-contributed Modbus RTU cheat sheet
- **Testing**: Production deployment feedback

---

## 📞 Support

- **Product Page**: https://485io.com/rs485-relays
- **Documentation**: https://485io.com/eletechsup/RYN404E-RYN408F-1.rar
- **Library Issues**: https://github.com/packerlschupfer/ESP32-RYN4/issues
- **Examples**: See `examples/` directory

---

## 🔜 Future Enhancements

Possible features for future releases:

1. **Auto-detection** - Automatically detect RYN404E vs RYN408F
2. **Multi-device support** - Manage multiple RYN4 modules on same bus
3. **Watchdog integration** - Track communication health
4. **Persistent configuration** - Save/restore relay states
5. **Advanced scheduling** - Time-based relay control

---

**Upgrade today for enhanced diagnostics and professional documentation!** 🚀
