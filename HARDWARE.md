# RYN4 Hardware Documentation

Complete hardware reference for RYN404E (4-channel) and RYN408F (8-channel) relay modules.

## Table of Contents

- [Hardware Overview](#hardware-overview)
- [Hardware Configuration](#hardware-configuration)
  - [DIP Switch Address Selection](#dip-switch-address-selection)
  - [Baud Rate Selection](#baud-rate-selection)
  - [Command Mode Selection](#command-mode-selection)
- [Modbus Register Map](#modbus-register-map)
- [Command Encoding](#command-encoding)
- [Protocol Details](#protocol-details)
- [Wiring Diagram](#wiring-diagram)
- [Example Modbus Packets](#example-modbus-packets)
- [Troubleshooting](#troubleshooting)

---

## Hardware Overview

### Specifications

**Models:**
- **RYN404E**: 4-channel relay module
- **RYN408F**: 8-channel relay module

**Power:**
- Operating Voltage: DC 12V or 24V (two versions available)
- Standby Current: 5.5mA @ 12V (RYN404E), 6.5mA @ 12V (RYN408F)
- Per-Relay Current: 31mA @ 12V / 16mA @ 24V
- Total Current (all ON): 129mA @ 12V (RYN404E), 254mA @ 12V (RYN408F)
- Protection: Reverse polarity, RS485 TVS surge protection

**Communication:**
- Protocol: Modbus RTU over RS485
- Default Settings: 9600 baud, 8 data bits, No parity, 1 stop bit (8N1)
- Baud Rates: 9600/19200/38400/115200 BPS (DIP switch configurable)
- Slave ID: 0x00-0x3F (1-63 decimal) via 6-bit DIP switches (A0-A5)
- Parity: None (default), Even, Odd (software configurable, requires power cycle)
- Function Codes: Write 05/06/15/16, Read 01/03

**Relay Specifications:**
- Type: SPDT (NC, COM, NO) - T73 model (19×15.4×15mm)
- **Pluggable**: Yes, user-replaceable
- Contact Rating: 10A @ 250VAC, 15A @ 125VAC, 10A @ 28VDC
- Mechanical Life: 10,000,000 operations
- Electrical Life: 100,000 operations

**Compatible Replacement Relays:**
- G5LB-14
- JQC-3FF
- SRD-12VDC-SL-C
- SRD-24VDC-SL-C

**Physical:**
- Dimensions (board only): 69×72×28mm (RYN404E), 123×72×28mm (RYN408F)
- Dimensions (with DIN box): 73×82×50mm (RYN404E), 135×82×50mm (RYN408F)
- Weight (board only): 80g (RYN404E), 150g (RYN408F)
- Weight (with DIN box): 137g (RYN404E), 242g (RYN408F)
- DIN Rail: Optional DIN box mounting available

**Command Modes:**
- **Modbus RTU** (default): Supported by this library ✅
- **AT Command**: Via M0 jumper (NOT supported by this library ❌)

### Supported by This Library
✅ **Modbus RTU mode** (Function Codes 0x01/0x03/0x05/0x06/0x0F/0x10)
✅ **All 8 relay control modes** (Open, Close, Toggle, Latch, Momentary, Delay, Open All, Close All)
✅ **Individual and batch relay control**
✅ **State verification** (verified control methods)
✅ **Configuration read** (address, baud rate, parity, firmware version)
✅ **Factory reset** command
❌ **AT Command mode** (not supported - use Modbus RTU only)

### Manufacturer Information
- **Manufacturer**: 485io.com
- **Product Page**: https://485io.com/rs485-relays-c-2_3_19/ryn408f-24v-8ch-relay-pluggable-modbus-remote-io-module-baud-rate-slave-id-dip-switch-selection-easy-to-use-multifunction-switch-board-p-1660.html
- **Documentation**: https://485io.com/eletechsup/RYN404E-RYN408F-1.rar
- **Price**: ~$22 USD (RYN408F)

---

## Hardware Configuration

### DIP Switch Address Selection (A0-A5)

The RYN4 module uses a 6-bit DIP switch array (A0-A5) to set the Modbus slave ID.

```
DIP Switch Position:  ON    OFF
                      ▄▄    ▄▄
                      ██    ░░
                      ▀▀    ▀▀
Bit Value:            1     0

Switch Array (LSB to MSB):
┌────┬────┬────┬────┬────┬────┐
│ A0 │ A1 │ A2 │ A3 │ A4 │ A5 │
└────┴────┴────┴────┴────┴────┘
 2^0  2^1  2^2  2^3  2^4  2^5
  1    2    4    8   16   32
```

#### Common Slave ID Configurations

| Slave ID | Binary  | A5 | A4 | A3 | A2 | A1 | A0 |
|----------|---------|----|----|----|----|----|----|
| 0x01 (1) | 000001 | OFF| OFF| OFF| OFF| OFF| ON |
| 0x02 (2) | 000010 | OFF| OFF| OFF| OFF| ON | OFF|
| 0x03 (3) | 000011 | OFF| OFF| OFF| OFF| ON | ON |
| 0x10 (16)| 010000 | OFF| ON | OFF| OFF| OFF| OFF|
| 0x3F (63)| 111111 | ON | ON | ON | ON | ON | ON |

**Example Calculation:**
```
Desired Slave ID = 5 (0x05)
Binary: 000101
A5=OFF, A4=OFF, A3=OFF, A2=ON, A1=OFF, A0=ON
```

**Visual Example from Manufacturer:**
```
  A0 A1 A2 A3 A4 A5        A0 A1 A2 A3 A4 A5        A0 A1 A2 A3 A4 A5
  ON                       ON                       ON ON
  ▓▓ ░░ ░░ ░░ ░░ ░░       ▓▓ ▓▓ ░░ ░░ ░░ ░░       ▓▓ ▓▓ ▓▓ ▓▓ ▓▓ ▓▓
  ▀▀ ▀▀ ▀▀ ▀▀ ▀▀ ▀▀       ▀▀ ▀▀ ▀▀ ▀▀ ▀▀ ▀▀       ▀▀ ▀▀ ▀▀ ▀▀ ▀▀ ▀▀
Slave ID = 0x01           Slave ID = 0x03          Slave ID = 0x3F
```

---

### Baud Rate Selection (M0/M1/M2 Jumper Pads)

The RYN4 module provides jumper pads M0, M1, M2 for baud rate configuration.

```
Jumper Pad Configuration:
┌────────────┐
│ M0  M1  M2 │  M0: Command mode (OPEN=Modbus, SHORT=AT)
│ ██  ██  ██ │  M1: Baud rate bit 0
│ ██  ██  ██ │  M2: Baud rate bit 1
└────────────┘
```

#### Baud Rate Selection Table

| M0 | M1 | M2 | Baud Rate | Notes |
|----|----|----|-----------|-----------------------|
| OPEN | OPEN | OPEN | **9600** | **Default** |
| OPEN | SHORT| OPEN | 2400 | |
| OPEN | OPEN | SHORT| 4800 | |
| OPEN | SHORT| SHORT| 19200 | |

**IMPORTANT:** M0 must remain **OPEN** for Modbus RTU mode (required by this library).

#### Visual Representation

```
M0  M1  M2           M0  M1  M2           M0  M1  M2           M0  M1  M2
██  ██  ██          ██  ██  ██          ██  ██  ██          ██  ██  ██
██  ██  ██          ██  ▓▓  ██          ██  ██  ▓▓          ██  ▓▓  ▓▓
                       (shorted)               (shorted)       (both shorted)

9600 BPS            2400 BPS            4800 BPS            19200 BPS
(default)
```

---

### Command Mode Selection (M0 Pad)

The M0 jumper pad selects between two command protocols:

| M0 State | Protocol | Supported by Library |
|----------|----------|---------------------|
| **OPEN** (default) | **Modbus RTU** | ✅ **YES** |
| **SHORTED** | AT Command (ASCII) | ❌ NO |

**Configuration for This Library:**
```
M0  M1  M2
██  ██  ██   ← M0 must be OPEN (disconnected)
██  ██  ██
```

**⚠️ WARNING:** Shorting M0 switches to AT Command mode, which is NOT supported by this library. Keep M0 pads **disconnected** (OPEN).

---

## Modbus Register Map

### Control Registers (Function Code 0x06 - Write Single Register)

Individual relay control and broadcast operations.

| Register Address | Channel | Description | Valid Commands |
|------------------|---------|-------------|----------------|
| 0x0001 | 1 | Channel 1 control | 0x01-0x06 |
| 0x0002 | 2 | Channel 2 control | 0x01-0x06 |
| 0x0003 | 3 | Channel 3 control | 0x01-0x06 |
| 0x0004 | 4 | Channel 4 control | 0x01-0x06 |
| 0x0005 | 5 | Channel 5 control | 0x01-0x06 |
| 0x0006 | 6 | Channel 6 control | 0x01-0x06 |
| 0x0007 | 7 | Channel 7 control | 0x01-0x06 |
| 0x0008 | 8 | Channel 8 control | 0x01-0x06 |
| 0x0000 | All | Broadcast control | 0x07-0x08 only |

**Note:** RYN404E (4-channel) uses registers 0x0001-0x0004 only.

---

### Status Registers (Function Code 0x03 - Read Holding Registers)

Read relay states (ON/OFF) individually or in batches.

| Register Address | Channel | Returns |
|------------------|---------|---------|
| 0x0001 | 1 | 0x0001 (ON) / 0x0000 (OFF) |
| 0x0002 | 2 | 0x0001 (ON) / 0x0000 (OFF) |
| 0x0003 | 3 | 0x0001 (ON) / 0x0000 (OFF) |
| 0x0004 | 4 | 0x0001 (ON) / 0x0000 (OFF) |
| 0x0005 | 5 | 0x0001 (ON) / 0x0000 (OFF) |
| 0x0006 | 6 | 0x0001 (ON) / 0x0000 (OFF) |
| 0x0007 | 7 | 0x0001 (ON) / 0x0000 (OFF) |
| 0x0008 | 8 | 0x0001 (ON) / 0x0000 (OFF) |

**Batch Reads:**
- Read 2 consecutive: Start=0x0001, Length=0x0002
- Read 8 channels: Start=0x0001, Length=0x0008

---

### Status Bitmap Register (Function Code 0x03 - Alternative Read Method)

**Efficient batch status read using a single 16-bit register:**

| Register Address | Description | Format |
|------------------|-------------|--------|
| 0x0080-0x0081 | All relay states as bitmap | Bits 0-7 = Relays 1-8 (1=ON, 0=OFF) |

**Example:**
```bash
# Read status bitmap
mbpoll -1 -q -m rtu -a 1 -r 128 -t 4:hex -0 -b 9600 -P none /dev/ttyUSB485

# Response: 0x007C = 0000 0000 0111 1100 (binary)
# Interpretation: Relays 3, 4, 5, 6, 7 are ON
```

**Bitmap Control (Write):**

The same register can be written to control multiple relays via bitmap:

```bash
# Turn ON relays 1 and 4, others OFF (bitmap 0x0009 = 0000 1001)
mbpoll -1 -q -m rtu -a 1 -r 128 -t 4:hex -0 -b 9600 -P none /dev/ttyUSB485 0x0009
```

---

### Complete Register Map (Hardware Verified 2025-12-14)

Full register scan results showing all accessible registers and their R/W capabilities:

| Address Range | Name | R/W | Values | Notes |
|---------------|------|-----|--------|-------|
| **0x0000-0x0007** | Relay 1-8 Control | **R/W** | Write: 0x01XX-0x08XX, Read: 0x0000/0x0001 | See command asymmetry below |
| 0x0008-0x001F | Reserved | R | 0x0000 | Write rejected, readable as 0 |
| 0x0020-0x007F | Unused | R | 0x0000 | Write rejected |
| **0x0080** | Relay Bitmap | **R/W** | Bits 0-7 = Relays 1-8 | Atomic multi-relay control |
| 0x0081-0x00EF | Unused | R | 0x0000 | Write rejected |
| 0x00F0 | Device Type | R | 0x0000 | Hardware model identifier |
| 0x00F1 | Firmware Major | R | 0x0000 | Firmware version |
| 0x00F2 | Firmware Minor | R | 0x0000 | Firmware version |
| 0x00F3-0x00FA | Reserved | R | 0x0000 | Write rejected |
| **0x00FB** | Factory Reset | **W** | Write 0x0000 | Resets to defaults |
| **0x00FC** | Reply Delay | **R/W** | 0-25 | Units: 40ms |
| 0x00FD | RS485 Address | R | 0x00-0x3F | From DIP switches |
| 0x00FE | Baud Rate | R | 0-3 | From DIP switches |
| **0x00FF** | Parity | **R/W** | 0-2 | Requires power cycle |

**Key Findings:**
- Only **4 writable areas**: Relay registers (0x00-0x07), Bitmap (0x80), Reply Delay (0xFC), Parity (0xFF)
- Factory Reset (0xFB) is write-only
- All other registers are read-only

---

### Configuration Registers (Function Code 0x03/0x06 - Read/Write)

**Device identification and configurable parameters:**

| Address | Name | R/W | Description | Values |
|---------|------|-----|-------------|--------|
| **0x00F0** (240) | Device Type | R | Hardware model identifier | Device-specific |
| **0x00F1** (241) | Firmware Major | R | Firmware major version | e.g., 1, 2, 3... |
| **0x00F2** (242) | Firmware Minor | R | Firmware minor version | e.g., 0, 1, 2... |
| **0x00FB** (251) | Factory Reset | W | Reset to factory defaults | Write 0x0000 |
| **0x00FC** (252) | Reply Delay | R/W | Response delay (**40ms units**) | 0-25 (0-1000ms) |
| **0x00FD** (253) | RS485 Address | R | DIP switch address setting | 0x00-0x3F |
| **0x00FE** (254) | Baud Rate | R | DIP switch baud rate config | 0-3 (see below) |
| **0x00FF** (255) | Parity | R/W | Parity setting (**requires power cycle**) | 0-2 (see below) |

#### Baud Rate Values (0x00FE)

| Value | Baud Rate | DIP Configuration |
|-------|-----------|-------------------|
| 0 | 9600 | M1=OPEN, M2=OPEN (default) |
| 1 | 19200 | M1=SHORT, M2=OPEN |
| 2 | 38400 | M1=OPEN, M2=SHORT |
| 3 | 115200 | M1=SHORT, M2=SHORT |

**Note:** Cheat sheet shows value 3 as 57600, but official manual shows 115200 for this DIP setting.

#### Parity Values (0x00FF)

| Value | Parity | Frame Format |
|-------|--------|--------------|
| 0 | None | 8N1 (default) |
| 1 | Even | 8E1 |
| 2 | Odd | 8O1 |

**⚠️ Important:** Writing parity register requires a **power cycle** to take effect. Values >2 reset to 0 (None) on power-up.

#### Reply Delay Configuration (0x00FC)

**Units: 40ms** (corrected from earlier 5ms assumption in cheat sheet)

```cpp
delay_ms = register_value × 40ms

Examples:
  Value 0  = 0ms (immediate response)
  Value 5  = 200ms
  Value 10 = 400ms
  Value 25 = 1000ms (maximum)
```

**Set 200ms reply delay:**
```bash
mbpoll -1 -q -m rtu -a 1 -r 252 -t 4:hex -0 -b 9600 -P none /dev/ttyUSB485 0x0005
```

**Note:** Setting value >25 resets delay to 0ms on next power-up.

#### Reading Device Information

**Read complete device identification block:**
```bash
# Read Device Type, FW Major, FW Minor (3 registers starting at 240)
mbpoll -1 -q -m rtu -a 1 -r 240 -c 3 -t 4:hex -0 -b 9600 -P none /dev/ttyUSB485

# Example response: 0x0001 0x0001 0x0000
# Device Type: 0x0001, Firmware: v1.0
```

**Read current hardware configuration:**
```bash
# Read RS485 Address (from DIP switches)
mbpoll -1 -q -m rtu -a 1 -r 253 -t 4:hex -0 -b 9600 -P none /dev/ttyUSB485

# Read Baud Rate (from M1/M2 jumpers)
mbpoll -1 -q -m rtu -a 1 -r 254 -t 4:hex -0 -b 9600 -P none /dev/ttyUSB485

# Read Parity setting
mbpoll -1 -q -m rtu -a 1 -r 255 -t 4:hex -0 -b 9600 -P none /dev/ttyUSB485
```

#### Factory Reset

**Two methods available:**

**Method 1: Hardware Reset**
1. Short the RES jumper on board for 5 seconds
2. Power cycle the module

**Method 2: Software Command**
```bash
# Broadcast factory reset command (address 0xFF)
# Note: Use current baud rate for this command
echo "FF 06 00 FB 00 00 ED E5" | xxd -r -p > /dev/ttyUSB485
```

**What gets reset:**
- Reply delay → 0ms
- Parity → None (8N1)
- All software-configurable settings

**What does NOT reset (hardware-configured):**
- RS485 Address (DIP switches A0-A5)
- Baud Rate (DIP switches M1/M2)

---

### Function Code Support Matrix

The RYN4 module supports **dual addressing systems** for compatibility:

| Function Code | Name | Purpose | Supported |
|---------------|------|---------|-----------|
| **0x01** | Read Coils | Read relay states | ✅ Yes |
| **0x02** | Read Discrete Inputs | Read digital inputs | ❌ No (Illegal function) |
| **0x03** | Read Holding Registers | Read relay states & config | ✅ Yes |
| **0x04** | Read Input Registers | Read analog inputs | ❌ No (Illegal function) |
| **0x05** | Write Single Coil | Control single relay | ✅ Yes |
| **0x06** | Write Single Register | Control single relay & config | ✅ Yes |
| **0x0F (15)** | Write Multiple Coils | Control multiple relays | ✅ Yes |
| **0x10 (16)** | Write Multiple Registers | Control multiple relays | ✅ Yes |

**This library uses FC 0x03/0x06/0x10** (holding register format) for all operations.

**Valid Address Range:**
- **0x0000 - 0x00FF** (0-255 decimal) - All valid addresses
- Addresses 256+ return "Illegal data address" error

**Addressing Note:**
- **PLC notation**: 00001-00032 (coils), 40001-40256 (holding registers)
- **Modbus RTU addresses**: 0x0000-0x001F (relays), 0x0080-0x00FF (config)
- When using `mbpoll` with `-0` flag, use decimal addresses (e.g., `-r 128` for 0x0080)

---

## Command Encoding

### Control Commands (Write Values - Function Code 0x06)

Commands sent TO the device for relay control.

| Command Code | Name | Description | Delay Parameter |
|--------------|------|-------------|-----------------|
| 0x0100 | ON | Turn relay ON (energize) | 0x00 |
| 0x0200 | OFF | Turn relay OFF (de-energize) | 0x00 |
| 0x0300 | TOGGLE | Self-locking toggle | 0x00 |
| 0x0400 | LATCH | Inter-locking (only one relay ON) | 0x00 |
| 0x0500 | MOMENTARY | 1-second pulse then OFF | 0x00 |
| 0x06XX | DELAY | ON for XX seconds (0-255), then OFF | 0x00-0xFF |
| 0x0700 | ALL_ON | Turn all relays ON (addr 0x0000 only) | 0x00 |
| 0x0800 | ALL_OFF | Turn all relays OFF (addr 0x0000 only) | 0x00 |

**Delay Command Details:**
```
Command: 0x06XX where XX = delay in seconds
Examples:
  0x060A = 10 seconds delay
  0x0664 = 100 seconds delay
  0x06FF = 255 seconds delay
```

#### DELAY Command Behavior (Hardware Verified 2025-12-14)

**⚠️ IMPORTANT: Registers show PHYSICAL STATE only, never command value!**

When a DELAY command is sent:
1. Relay turns **ON immediately**
2. Register reads **0x0001** (ON) during the delay period
3. After delay expires, relay turns **OFF**
4. Register reads **0x0000** (OFF)

**Test Results (10-second delay):**
```
Command sent: 0x060A (DELAY 10 seconds)

Time | Register | Physical State
-----|----------|---------------
t=0s | 0x0000   | OFF (before command)
t=1s | 0x0001   | ON  ← Relay ON immediately
t=2s | 0x0001   | ON
...  | 0x0001   | ON
t=9s | 0x0001   | ON
t=10s| 0x0000   | OFF ← Timer expired
t=11s| 0x0000   | OFF
```

**Key Points:**
- **No "pending" state register** - hardware doesn't store original command
- **Cannot detect delay in progress** by reading registers alone
- **Software must track delays** if countdown status is needed
- Same applies to MOMENTARY command (1-second pulse)

#### Cancelling Active DELAY Timers (Hardware Verified 2025-12-14)

**⚠️ IMPORTANT: OFF command (0x0200) does NOT cancel active delays!**

To cancel a running DELAY timer, you must send **DELAY 0 (0x0600)**:

| Command | Effect on Active Delay |
|---------|----------------------|
| OFF (0x0200) | ❌ Does NOT cancel - relay stays ON |
| ON (0x0100) | ❌ Does NOT cancel - timer continues |
| Bitmap write (0x0000) | ❌ Does NOT cancel |
| Bitmap write (0x0600) | ❌ Does NOT work - bitmap only accepts 0x00-0xFF |
| ALL_OFF (0x0800) | ❌ Does NOT cancel |
| **DELAY 0 (0x0600)** | ✅ **Cancels timer, turns OFF immediately** |

**Note:** DELAY 0 must be sent to individual relay registers (0x0000-0x0007), not the bitmap register.

**Test sequence:**
```bash
# 1. Set relay ON with 255-second delay
mbpoll -1 -q -m rtu -a 2 -r 0 -t 4:hex -0 -b 9600 -P even /dev/ttyUSB485 0x06FF

# 2. Try OFF - does NOT work, relay stays ON
mbpoll -1 -q -m rtu -a 2 -r 0 -t 4:hex -0 -b 9600 -P even /dev/ttyUSB485 0x0200

# 3. Send DELAY 0 - cancels timer, relay turns OFF
mbpoll -1 -q -m rtu -a 2 -r 0 -t 4:hex -0 -b 9600 -P even /dev/ttyUSB485 0x0600
```

**Library implementation note:** When implementing emergency stop or immediate OFF functionality, always use `0x0600` (DELAY 0) instead of `0x0200` (OFF) to ensure any active delay timers are cancelled.

**Momentary Mode:**
- Fixed 1-second duration
- Relay turns ON, waits 1 second, then turns OFF automatically

**Latch (Inter-locking) Mode:**
- Turning one relay ON automatically turns all others OFF
- Useful for mutually exclusive operations (e.g., forward/reverse motor control)

---

### Multi-Command Support (FC 0x10) - **HARDWARE TESTED** ✅

**CRITICAL DISCOVERY**: All command types work in FC 0x10 (Write Multiple Registers)!

The RYN4 module supports **mixed command types** in a single batch operation, enabling:

✅ **All Commands Supported in FC 0x10:**
- ON (0x0100)
- OFF (0x0200)
- TOGGLE (0x0300)
- LATCH (0x0400)
- MOMENTARY (0x0500)
- DELAY (0x06XX) with **independent timing per relay**

**Hardware Testing Confirmed (2025-12-07):**
- Tested on RYN408F, Slave ID 2, 9600 baud
- All 6 command types verified working in FC 0x10
- Mixed commands in single operation work perfectly
- Independent DELAY timing confirmed (tested 3s, 5s, 8s simultaneously)

#### Example: Mixed Commands in Single Operation

```bash
# Single FC 0x10 with different commands per relay:
# R1: ON, R2: MOMENTARY, R3: DELAY 5s, R4: TOGGLE, R5-8: OFF
mbpoll -1 -q -m rtu -a 2 -r 0 -t 4:hex -0 -b 9600 -P none /dev/ttyUSB0 \
  0x0100 0x0500 0x0605 0x0300 0x0200 0x0200 0x0200 0x0200
```

**Result:**
- Relay 1: Turns ON permanently
- Relay 2: Pulses ON for 1 second, then OFF
- Relay 3: Turns ON for 5 seconds, then OFF
- Relay 4: Toggles state
- Relays 5-8: Turn OFF

**All commands execute atomically and simultaneously!**

#### Performance Benefits

| Method | Commands | Timing | Execution |
|--------|----------|--------|-----------|
| **Sequential** (FC 0x06 × 8) | Individual | ~400-500ms | Sequential |
| **Batch** (FC 0x10 × 1) | Atomic | ~50-80ms | Simultaneous |
| **Speedup** | | **6-8x faster** | **Synchronized** |

#### Independent DELAY Timing

**Tested Scenario:**
```bash
# Three relays with different delays in single command
mbpoll -1 -q -m rtu -a 2 -r 0 -t 4:hex -0 -b 9600 -P none /dev/ttyUSB0 \
  0x0603 0x0605 0x0608 0x0200
#  3sec   5sec   8sec   OFF
```

**Observed Behavior:**
- t=0s: Relays 1, 2, 3 all turn ON
- t=3s: Relay 1 turns OFF (3-second timer expired)
- t=5s: Relay 2 turns OFF (5-second timer expired)
- t=8s: Relay 3 turns OFF (8-second timer expired)

Each relay maintains **independent hardware timer** - no cross-interference!

#### LATCH Behavior in Multi-Command

When multiple LATCH commands sent via FC 0x10:
- **Last relay in sequence wins** and stays ON
- All other relays turn OFF (inter-locking behavior maintained)

**Example:**
```bash
mbpoll -1 -q -m rtu -a 2 -r 0 -t 4:hex -0 -b 9600 -P none /dev/ttyUSB0 \
  0x0400 0x0400 0x0400 0x0400
#  All LATCH commands

# Result: Only relay 4 ON, relays 1-3 OFF
```

#### Use Cases Enabled

1. **Complex Automation Sequences**
   - Boiler startup: Pump ON, fan pulse, ignition delay
   - All synchronized in single command

2. **Staggered Motor Start**
   - Prevent power surge: Start motors with 5s, 10s, 15s delays
   - Reduces inrush current

3. **Synchronized Test Patterns**
   - Pulse all relays simultaneously for system test
   - Perfect timing synchronization

4. **Emergency Sequences**
   - Safety shutdown: Some OFF, some pulsed, some delayed
   - Single atomic operation

**See HARDWARE_TEST_RESULTS.md for complete test data and examples.**

---

### Status Response Values (Read Values - Function Code 0x03)

Values returned FROM the device when reading relay states.

| Response Value | Relay State | Description |
|----------------|-------------|-------------|
| 0x0001 | ON | Relay is energized (contact closed) |
| 0x0000 | OFF | Relay is de-energized (contact open) |

---

### **CRITICAL: Command vs Status Value Asymmetry**

⚠️ **The hardware uses DIFFERENT values for commands and status!**

| Operation | Write Command | Read Response |
|-----------|---------------|---------------|
| Turn relay ON | Send 0x0100 | Reads back 0x0001 |
| Turn relay OFF | Send 0x0200 | Reads back 0x0000 |

**Example:**
```cpp
// 1. Send ON command (0x0100)
ryn4.controlRelay(1, RelayAction::ON);

// 2. Read status - device returns 0x0001 (NOT 0x0100!)
bool state;
ryn4.readRelayStatus(1, state);  // state = true (relay is ON)
```

**Why This Matters:**
- The library handles this conversion automatically
- If writing raw Modbus commands, you MUST use correct encoding
- Command write: 0x0100/0x0200
- Status read: 0x0001/0x0000

---

## Protocol Details

### Modbus RTU Frame Structure

#### Write Single Register (FC 0x06) - Control Command

```
┌──────────┬──────────┬──────────┬──────────┬──────────┬──────────┬──────────┬──────────┐
│  Byte 1  │  Byte 2  │  Byte 3  │  Byte 4  │  Byte 5  │  Byte 6  │  Byte 7  │  Byte 8  │
├──────────┼──────────┼──────────┼──────────┼──────────┼──────────┼──────────┼──────────┤
│ Slave ID │   0x06   │ Addr Hi  │ Addr Lo  │ Data Hi  │ Data Lo  │  CRC Lo  │  CRC Hi  │
└──────────┴──────────┴──────────┴──────────┴──────────┴──────────┴──────────┴──────────┘

Field Descriptions:
- Slave ID: Device address (0x00-0x3F from DIP switches)
- Function: 0x06 (Write Single Register)
- Address: Register address (0x0001-0x0008, or 0x0000 for broadcast)
- Data: Command code (0x0100-0x0800) + Delay parameter
- CRC: Modbus RTU CRC16 checksum
```

**Example: Turn ON Channel 1 (Slave ID = 0x01)**
```
01 06 00 01 01 00 D9 9A
│  │  │  │  │  │  │  └─ CRC Hi
│  │  │  │  │  │  └──── CRC Lo
│  │  │  │  │  └─────── Delay = 0x00
│  │  │  │  └────────── Command = 0x01 (ON, full value 0x0100)
│  │  │  └───────────── Address Lo = 0x01
│  │  └──────────────── Address Hi = 0x00
│  └─────────────────── Function = 0x06
└────────────────────── Slave ID = 0x01
```

---

#### Read Holding Registers (FC 0x03) - Status Read

**Request:**
```
┌──────────┬──────────┬──────────┬──────────┬──────────┬──────────┬──────────┬──────────┐
│  Byte 1  │  Byte 2  │  Byte 3  │  Byte 4  │  Byte 5  │  Byte 6  │  Byte 7  │  Byte 8  │
├──────────┼──────────┼──────────┼──────────┼──────────┼──────────┼──────────┼──────────┤
│ Slave ID │   0x03   │ Addr Hi  │ Addr Lo  │ Qty Hi   │ Qty Lo   │  CRC Lo  │  CRC Hi  │
└──────────┴──────────┴──────────┴──────────┴──────────┴──────────┴──────────┴──────────┘

- Slave ID: Device address
- Function: 0x03 (Read Holding Registers)
- Address: Starting register (0x0001-0x0008)
- Quantity: Number of registers to read (0x0001-0x0008)
- CRC: Modbus RTU CRC16 checksum
```

**Response:**
```
┌──────────┬──────────┬──────────┬──────────┬──────────┬─────┬──────────┬──────────┐
│  Byte 1  │  Byte 2  │  Byte 3  │  Byte 4  │  Byte 5  │ ... │  CRC Lo  │  CRC Hi  │
├──────────┼──────────┼──────────┼──────────┼──────────┼─────┼──────────┼──────────┤
│ Slave ID │   0x03   │ByteCount │ Data Hi  │ Data Lo  │ ... │  CRC Lo  │  CRC Hi  │
└──────────┴──────────┴──────────┴──────────┴──────────┴─────┴──────────┴──────────┘

- Slave ID: Device address (echo)
- Function: 0x03 (echo)
- Byte Count: Number of data bytes (2 * quantity)
- Data: Relay states (0x0001=ON, 0x0000=OFF) for each register
- CRC: Modbus RTU CRC16 checksum
```

**Example: Read Channel 1 Status (Relay is ON)**
```
Request:  01 03 00 01 00 01 D5 CA
Response: 01 03 02 00 01 79 84
                   └──┘
                   0x0001 = ON
```

---

### Communication Settings

| Parameter | Value |
|-----------|-------|
| **Baud Rate** | 2400, 4800, 9600 (default), 19200 BPS |
| **Data Bits** | 8 |
| **Parity** | None (default), Even, Odd (software configurable) |
| **Stop Bits** | 1 |
| **Frame Format** | 8N1 (default), 8E1, 8O1 |
| **Protocol** | Modbus RTU |
| **Function Codes** | 0x01/0x03 (Read), 0x05/0x06/0x0F/0x10 (Write) |

**⚠️ Note:** Check register 0x00FF to verify current parity setting before connecting. If communication fails, try Even parity (8E1) - some devices ship with parity already configured.

---

## Wiring Diagram

### RS485 Connection

```
ESP32                   RS485 Module              RYN4 Module
┌─────────┐            ┌──────────┐              ┌──────────┐
│         │            │          │              │          │
│ GPIO 17 ├───────────►│ RX (RO)  │              │          │
│  (RXD)  │            │          │              │          │
│         │            │          │    RS485     │          │
│ GPIO 18 ├───────────►│ TX (DI)  │◄────Bus─────►│  A       │
│  (TXD)  │            │          │              │  B       │
│         │            │          │              │          │
│ GPIO 16 ├───────────►│ DE/RE    │              │          │
│(DIR Ctl)│            │          │              │          │
│         │            │          │              │          │
│   GND   ├───────────►│   GND    ├─────────────►│  GND     │
│         │            │          │              │          │
│   3.3V  ├───────────►│   VCC    │              │          │
│         │            │          │              │ 12-24V   │
└─────────┘            └──────────┘              │ Power In │
                                                 └──────────┘

Termination Resistor (120Ω):
- Install at both ends of RS485 bus for long cable runs (>10m)
- Between A and B terminals
```

**Notes:**
- RS485 modules may use different pin names (RO/DI, A/B vs +/-)
- Some modules have automatic direction control (no DE/RE needed)
- For multiple RYN4 devices, connect all A-to-A and B-to-B in parallel
- Maximum bus length: ~1200m (depending on baud rate)
- Use twisted pair cable for RS485 A/B lines

---

### Relay Output Wiring

Each relay channel has 3 terminals:

```
Relay Terminal Layout (per channel):
┌─────┬─────┬─────┐
│ COM │ NO  │ NC  │
└─────┴─────┴─────┘

COM = Common (connects to either NO or NC)
NO  = Normally Open (connected when relay is ON)
NC  = Normally Closed (connected when relay is OFF)

Relay State:
┌──────────────────┬──────────────────┬──────────────────┐
│   Relay State    │  COM ↔ NO        │  COM ↔ NC        │
├──────────────────┼──────────────────┼──────────────────┤
│ OFF (de-energize)│  Open (∞Ω)       │  Closed (0Ω)     │
│ ON  (energize)   │  Closed (0Ω)     │  Open (∞Ω)       │
└──────────────────┴──────────────────┴──────────────────┘
```

**Example: Controlling a pump (normally off)**
```
     12V Power Supply
         │
         ├──────┐
         │      │
        COM    NO ──► Pump (+)
         │
      Relay 1

Pump (-) ──► GND
```

When relay ON (0x0100): COM↔NO closes, pump runs
When relay OFF (0x0200): COM↔NO opens, pump stops

---

## Example Modbus Packets

All examples assume Slave ID = 0x01.

### Individual Relay Control (FC 0x06)

```
Channel 1 ON (turn ON):
  01 06 00 01 01 00 D9 9A

Channel 1 OFF (turn OFF):
  01 06 00 01 02 00 D9 6A

Channel 1 TOGGLE:
  01 06 00 01 03 00 D8 FA

Channel 1 LATCH (inter-locking):
  01 06 00 01 04 00 DA CA

Channel 1 MOMENTARY (1-second pulse):
  01 06 00 01 05 00 DB 5A

Channel 1 DELAY 10 seconds:
  01 06 00 01 06 0A 5B AD

Channel 1 DELAY 100 seconds:
  01 06 00 01 06 64 DA 41
```

### Broadcast Control (All Relays)

```
ALL_ON (turn all relays ON):
  01 06 00 00 07 00 8B FA

ALL_OFF (turn all relays OFF):
  01 06 00 00 08 00 8E 0A
```

### Read Relay Status (FC 0x03)

```
Read Channel 1 status:
  Request:  01 03 00 01 00 01 D5 CA
  Response: 01 03 02 00 01 79 84  (relay is ON)
  Response: 01 03 02 00 00 B8 44  (relay is OFF)

Read Channel 2 status:
  Request:  01 03 00 02 00 01 25 CA
  Response: 01 03 02 00 01 79 84  (relay is ON)

Read Channel 1 and 2 status (both ON):
  Request:  01 03 00 01 00 02 95 CB
  Response: 01 03 04 00 01 00 01 AB F3

Read all 8 channels:
  Request:  01 03 00 01 00 08 15 C8
  Response: 01 03 10 [16 bytes] [CRC]
            (2 bytes per channel: 00 01 or 00 00)
```

---

## Troubleshooting

### No Response from Module

**Check DIP Switches:**
- Verify slave ID matches your configuration
- Example: For slave ID 0x01, only A0 should be ON

**Check Baud Rate:**
- Verify M1/M2 jumper configuration
- Default is 9600 (M0/M1/M2 all OPEN)

**Check Command Mode:**
- M0 must be OPEN (disconnected) for Modbus RTU
- If M0 is shorted, module uses AT commands (not supported)

**Check RS485 Wiring:**
- Verify A-to-A and B-to-B connections
- Swap A and B if no communication (some modules use reversed polarity)
- Add 120Ω termination resistor for long cables

**Check Power:**
- RYN4 requires 12-24V DC on power terminals
- Verify power LED is ON

---

### Incorrect Relay State

**Command/Status Value Confusion:**
- Remember: Write 0x0100 (ON), but read back 0x0001
- Don't compare write values directly to read values

**State Verification:**
- Use verified control methods: `controlRelayVerified()`
- Library automatically reads back state after write

**Delay Timing:**
- Modbus has inherent latency (~50-100ms)
- Wait 100ms between command and status read
- Library handles this automatically with `waitForData()`

---

### CRC Errors

**Manual Commands:**
- Use Modbus CRC16 calculator
- Or use tools like `mbpoll` that calculate CRC automatically

**Example mbpoll Commands:**
```bash
# Toggle relay 1 (slave ID 2)
mbpoll -q -m rtu -a 2 -r 1 -t 4:hex -b 9600 -P none /dev/ttyUSB0 0x0300

# Read all 8 relay states
mbpoll -q -1 -m rtu -a 2 -r 1 -c 8 -t 4:hex -b 9600 -P none /dev/ttyUSB0
```

See README.md for complete mbpoll usage guide.

---

### Multiple Devices on Same Bus

**Unique Slave IDs Required:**
- Each RYN4 module must have a different DIP switch configuration
- Example bus: Module 1 (ID=0x01), Module 2 (ID=0x02), Module 3 (ID=0x03)

**Bus Topology:**
- Connect all A terminals together
- Connect all B terminals together
- Connect GND of all devices
- Use star or daisy-chain topology

**Termination:**
- Install 120Ω resistor at BOTH ends of bus
- Not required for short cables (<10m) with 2-3 devices

---

## Additional Resources

- **Manufacturer PDF**: `/old/Documents/R4D8A08-Relay/8 Channel Rail RS485 Relay commamd.pdf`
- **Library Examples**: `examples/` directory
- **mbpoll Tool**: Command-line Modbus testing utility
- **Modbus Poll Software**: GUI tool for Windows (auto CRC calculation)

---

## Revision History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2025-12-07 | Initial hardware documentation |
| 1.1 | 2025-12-14 | Added complete register map from hardware scan, FC 02/04 not supported, DELAY command behavior showing physical state only, valid address range 0x0000-0x00FF, DELAY 0 (0x0600) cancels active timers |
