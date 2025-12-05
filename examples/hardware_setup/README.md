# RYN4 Hardware Setup Guide

Step-by-step guide for configuring and testing your RYN404E/RYN408F relay module.

## Table of Contents

1. [Before You Begin](#before-you-begin)
2. [Hardware Configuration](#hardware-configuration)
3. [Wiring the Module](#wiring-the-module)
4. [Testing Communication](#testing-communication)
5. [Troubleshooting](#troubleshooting)

---

## Before You Begin

### What You Need

**Hardware:**
- RYN404E (4-channel) or RYN408F (8-channel) relay module
- ESP32 development board
- RS485 transceiver module (MAX485, SP485, or similar)
- 12-24V DC power supply for relay module
- Jumper wires and breadboard
- Small screwdriver for DIP switches and terminal blocks
- USB cable for ESP32 programming

**Software:**
- Arduino IDE or PlatformIO
- ESP32-RYN4 library (this library)
- Required dependencies (installed automatically):
  - ESP32-ModbusDevice
  - ESP32-MutexGuard
  - ESP32-LibraryCommon

**Optional Tools:**
- Multimeter for continuity testing
- `mbpoll` utility for command-line testing
- Logic analyzer for debugging (if needed)

---

## Hardware Configuration

### Step 1: Set the Slave ID (DIP Switches A0-A5)

The RYN4 module uses 6 DIP switches to set the Modbus slave address.

**Switch Position Reference:**
```
ON position:  Switch pressed DOWN (connected)
OFF position: Switch pressed UP (disconnected)

┌────┬────┬────┬────┬────┬────┐
│ A0 │ A1 │ A2 │ A3 │ A4 │ A5 │  ← Switch labels
└────┴────┴────┴────┴────┴────┘
 2^0  2^1  2^2  2^3  2^4  2^5   ← Bit values
  1    2    4    8   16   32
```

**Common Configurations:**

| Desired Slave ID | A0 | A1 | A2 | A3 | A4 | A5 | Calculation |
|------------------|----|----|----|----|----|----|-------------|
| **1** (0x01) | ON | OFF| OFF| OFF| OFF| OFF| 1 |
| **2** (0x02) | OFF| ON | OFF| OFF| OFF| OFF| 2 |
| **3** (0x03) | ON | ON | OFF| OFF| OFF| OFF| 1+2=3 |
| **5** (0x05) | ON | OFF| ON | OFF| OFF| OFF| 1+4=5 |
| **10** (0x0A)| OFF| ON | OFF| ON | OFF| OFF| 2+8=10 |
| **16** (0x10)| OFF| OFF| OFF| OFF| ON | OFF| 16 |

**For your first test, use Slave ID = 1:**
- A0: **ON** (pressed down)
- A1-A5: **OFF** (pressed up)

**Calculation Tool:**
```cpp
// Include in your sketch to calculate DIP switch positions
void printDipSwitchConfig(uint8_t slaveId) {
    Serial.printf("Slave ID: 0x%02X (%d)\n", slaveId, slaveId);
    Serial.println("DIP Switch Configuration:");
    Serial.printf("  A0 (bit 0, val 1):  %s\n", (slaveId & 0x01) ? "ON " : "OFF");
    Serial.printf("  A1 (bit 1, val 2):  %s\n", (slaveId & 0x02) ? "ON " : "OFF");
    Serial.printf("  A2 (bit 2, val 4):  %s\n", (slaveId & 0x04) ? "ON " : "OFF");
    Serial.printf("  A3 (bit 3, val 8):  %s\n", (slaveId & 0x08) ? "ON " : "OFF");
    Serial.printf("  A4 (bit 4, val 16): %s\n", (slaveId & 0x10) ? "ON " : "OFF");
    Serial.printf("  A5 (bit 5, val 32): %s\n", (slaveId & 0x20) ? "ON " : "OFF");
}
```

---

### Step 2: Set the Baud Rate (Jumper Pads M1/M2)

The module has solder pads M0, M1, M2 on the PCB.

**Jumper Configuration:**
```
Pad Labels:  M0   M1   M2
            [  ] [  ] [  ]  ← Solder pads on PCB
            [  ] [  ] [  ]
```

**Baud Rate Settings:**

| M0 | M1 | M2 | Baud Rate |
|----|----|----|-----------|
| OPEN | OPEN | OPEN | **9600** (default, recommended) |
| OPEN | SHORT| OPEN | 2400 |
| OPEN | OPEN | SHORT| 4800 |
| OPEN | SHORT| SHORT| 19200 |

**For your first test, use 9600 BPS (default):**
- M0: **OPEN** (no solder bridge)
- M1: **OPEN** (no solder bridge)
- M2: **OPEN** (no solder bridge)

**⚠️ CRITICAL: M0 Must Remain OPEN**

The M0 pad selects command mode:
- **M0 OPEN**: Modbus RTU mode ✅ (required by this library)
- **M0 SHORT**: AT Command mode ❌ (NOT supported)

**Never short M0** when using this library!

---

### Step 3: Verify Physical Configuration

Before powering on, double-check:

✅ **DIP Switches:**
- [ ] Slave ID matches your sketch (default: 0x01)
- [ ] Only intended switches are ON

✅ **Jumper Pads:**
- [ ] M0 is OPEN (not shorted)
- [ ] M1/M2 match baud rate (default: both OPEN for 9600)

✅ **Power:**
- [ ] 12-24V DC power supply ready
- [ ] Correct polarity (check terminal labels)

---

## Wiring the Module

### Step 1: RS485 Connection (ESP32 to RYN4)

```
ESP32 Pin          RS485 Module          RYN4 Module
─────────          ────────────          ───────────
GPIO 17 (RX) ────► RO (Receiver Out)
GPIO 18 (TX) ────► DI (Driver In)
GPIO 16 (DIR)────► DE/RE (Direction)
                                            A ◄────► A (RS485+)
                   A ◄──────────────────► B ◄────► B (RS485-)

GND ────────────► GND ──────────────────► GND

3.3V ───────────► VCC (RS485 module)

                                         +12-24V DC
                                         GND
```

**Important Notes:**

1. **RS485 Polarity:**
   - Connect A-to-A and B-to-B
   - If communication fails, try swapping A and B (some modules use reversed labeling)

2. **Direction Control:**
   - GPIO16 controls transmit/receive direction on RS485
   - Some RS485 modules have automatic direction control (no DE/RE needed)

3. **Termination Resistor:**
   - For cable runs >10m, install 120Ω resistor between A and B at BOTH ends of bus
   - Not required for short connections with 1-2 devices

4. **Multiple Devices:**
   - Connect all A terminals together (parallel)
   - Connect all B terminals together (parallel)
   - Use different slave IDs for each device
   - Maximum ~32 devices on one RS485 bus

---

### Step 2: Relay Output Wiring

Each relay channel has 3 screw terminals:

```
Channel Terminal Layout:
┌─────┬─────┬─────┐
│ COM │ NO  │ NC  │
└─────┴─────┴─────┘

COM = Common (switches between NO and NC)
NO  = Normally Open (connected when relay is ON)
NC  = Normally Closed (connected when relay is OFF)
```

**Example: Controlling an LED (24V)**

```
       +24V Power
         │
         └────► COM (Relay 1)
                 │
                 ├─ NO ─────► LED (+) ─────► Resistor ─────► GND
                 │
                 └─ NC ─────► (not connected)

When relay OFF: COM ↔ NC (LED off)
When relay ON:  COM ↔ NO (LED on)
```

**Example: Controlling a Pump (120VAC)**

```
⚠️ WARNING: Mains voltage! Use proper insulation and follow electrical codes!

       120VAC Hot
         │
         └────► COM (Relay 1)
                 │
                 └─ NO ─────► Pump Hot Wire

       120VAC Neutral ─────► Pump Neutral Wire

When relay ON: Pump runs
When relay OFF: Pump stops
```

**Relay Ratings:**
- **Contact Current**: Check datasheet (typically 5-10A)
- **Voltage Rating**: AC 250V / DC 30V (typical)
- **For high-power loads**: Use relay to switch a contactor

---

### Step 3: Power Connections

**RYN4 Module Power:**
```
Power Supply (12-24V DC)
         │
         ├────► +12-24V terminal
         └────► GND terminal
```

**ESP32 Power:**
```
USB Cable or separate 5V supply
```

**⚠️ Important:**
- ESP32 and RYN4 module have separate power supplies
- Common ground connection required for RS485 communication
- Relay coils are powered by module's 12-24V supply
- Relay contacts can switch different voltage (up to rating)

---

## Testing Communication

### Option 1: Using the Test Sketch (Recommended)

1. **Open the test sketch:**
   - File: `examples/hardware_setup/test_communication.ino`
   - Or create a new sketch with the code below

2. **Configure parameters:**
```cpp
const uint8_t SLAVE_ID = 1;        // Match your DIP switches
const uint32_t BAUD_RATE = 9600;   // Match your M1/M2 jumpers
```

3. **Upload and monitor:**
   - Upload sketch to ESP32
   - Open Serial Monitor (9600 baud)
   - Watch for initialization messages

4. **Expected output:**
```
=== RYN4 Hardware Communication Test ===
Slave ID: 1
Baud Rate: 9600

Initializing RYN4...
[RYN4] Initialization started
[RYN4] Module responded - ONLINE
[RYN4] Initialization complete ✓

Testing individual relay control...
Channel 1: ON ... OK
Channel 1: OFF ... OK
Channel 2: ON ... OK
Channel 2: OFF ... OK

All tests passed! ✓
```

---

### Option 2: Using mbpoll (Command Line)

If you have `mbpoll` installed:

```bash
# Read relay 1 status (slave ID 1, baud 9600)
mbpoll -q -1 -m rtu -a 1 -r 1 -t 4:hex -b 9600 -P none /dev/ttyUSB0

# Toggle relay 1 ON
mbpoll -q -m rtu -a 1 -r 1 -t 4:hex -b 9600 -P none /dev/ttyUSB0 0x0100

# Toggle relay 1 OFF
mbpoll -q -m rtu -a 1 -r 1 -t 4:hex -b 9600 -P none /dev/ttyUSB0 0x0200

# Read all 8 relay states
mbpoll -q -1 -m rtu -a 1 -r 1 -c 8 -t 4:hex -b 9600 -P none /dev/ttyUSB0
```

Replace `/dev/ttyUSB0` with your serial port (Windows: `COM3`, etc.)

---

## Troubleshooting

### Problem: No Response from Module

**Check Slave ID:**
```
✓ Verify DIP switch calculation
✓ Ensure switch positions match physical hardware
✓ Try slave ID 1 (only A0 ON) as test
```

**Check Baud Rate:**
```
✓ Verify M1/M2 jumper configuration
✓ Try 9600 (all OPEN) as default
✓ Match sketch baud rate to hardware
```

**Check Command Mode:**
```
✓ Verify M0 is OPEN (not shorted)
✓ If M0 shorted, module uses AT commands (not Modbus)
```

**Check RS485 Wiring:**
```
✓ Swap A and B if no communication
✓ Check DE/RE connected to GPIO16
✓ Verify common ground between ESP32 and RYN4
✓ Measure voltage on A/B lines (should idle around 2.5V differential)
```

**Check Power:**
```
✓ Verify 12-24V on power terminals (use multimeter)
✓ Check power LED is lit on module
```

---

### Problem: Relay Doesn't Click

**Verify Command Sent:**
```cpp
// Add debug logging
auto result = ryn4.controlRelay(1, RelayAction::OPEN);
Serial.printf("Control result: %d\n", static_cast<int>(result));
```

**Check Status Read:**
```cpp
bool state;
ryn4.readRelayStatus(1, state);
Serial.printf("Relay 1 state: %s\n", state ? "ON" : "OFF");
```

**Physical Check:**
```
✓ Listen for relay click (audible)
✓ LED indicator on module (if present)
✓ Measure continuity across COM-NO/NC with multimeter
```

---

### Problem: Relay Stuck ON/OFF

**Hardware Issue:**
```
✓ Relay may be mechanically stuck (power cycle module)
✓ Check if relay is damaged (replace module)
```

**Software Issue:**
```cpp
// Force all relays OFF
ryn4.controlRelay(0, RelayAction::CLOSE_ALL);  // Address 0 = broadcast

// Verify with status read
for (int i = 1; i <= 8; i++) {
    bool state;
    ryn4.readRelayStatus(i, state);
    Serial.printf("Relay %d: %s\n", i, state ? "ON" : "OFF");
}
```

---

### Problem: CRC Errors

**Symptoms:**
```
[RYN4] Modbus error: CRC_ERROR
[RYN4] Timeout waiting for response
```

**Solutions:**
```
✓ Check RS485 cable quality (use shielded twisted pair)
✓ Reduce baud rate (try 9600 or 4800)
✓ Add termination resistor for long cables
✓ Check for electrical noise (motors, relays nearby)
✓ Verify ground connection
```

---

### Problem: Intermittent Communication

**Environmental:**
```
✓ Move away from sources of EMI (motors, fluorescent lights)
✓ Use shielded cable for RS485
✓ Keep RS485 cable away from AC power lines
```

**Timing:**
```cpp
// Increase delay between commands
vTaskDelay(pdMS_TO_TICKS(100));  // 100ms delay
```

**Bus Loading:**
```
✓ Reduce number of devices on bus
✓ Use shorter cables
✓ Add termination resistors at both ends
```

---

## Next Steps

Once communication is working:

1. **Verify all channels:**
   - Test each relay individually
   - Confirm click sound and LED indication
   - Measure contact continuity with multimeter

2. **Test advanced features:**
   - Batch relay control (`setMultipleRelayStates`)
   - Verified control (`controlRelayVerified`)
   - Momentary and delay modes

3. **Integrate into your project:**
   - See `examples/verified_control/` for production patterns
   - Review USAGE.md for API reference
   - Check HARDWARE.md for protocol details

---

## Additional Resources

- **Complete Hardware Guide**: [HARDWARE.md](../../HARDWARE.md)
- **Library Usage Guide**: [USAGE.md](../../USAGE.md)
- **API Examples**: `examples/` directory
- **Manufacturer Datasheet**: `/old/Documents/R4D8A08-Relay/8 Channel Rail RS485 Relay commamd.pdf`
- **RYN4 Library Docs**: [README.md](../../README.md)

---

## Hardware Configuration Checklist

Print this checklist for initial setup:

```
[ ] RYN4 Module Configuration
    [ ] DIP switches set for slave ID: _____
    [ ] M0 jumper: OPEN (Modbus RTU mode)
    [ ] M1 jumper: OPEN (for 9600 baud)
    [ ] M2 jumper: OPEN (for 9600 baud)
    [ ] Power supply: 12-24V DC connected
    [ ] Power LED: ON

[ ] RS485 Wiring
    [ ] ESP32 GPIO17 → RS485 RO
    [ ] ESP32 GPIO18 → RS485 DI
    [ ] ESP32 GPIO16 → RS485 DE/RE
    [ ] RS485 A → RYN4 A
    [ ] RS485 B → RYN4 B
    [ ] Common GND connected
    [ ] Termination resistor (if cable >10m)

[ ] ESP32 Configuration
    [ ] Slave ID in sketch matches DIP switches
    [ ] Baud rate in sketch matches M1/M2
    [ ] Correct GPIO pins defined
    [ ] Library dependencies installed

[ ] Initial Test
    [ ] Upload test sketch
    [ ] Serial monitor shows initialization
    [ ] Relay clicks when commanded
    [ ] Status reads confirm state
    [ ] All channels tested
```

---

**Last Updated:** 2025-12-07
**Library Version:** v0.1.0
