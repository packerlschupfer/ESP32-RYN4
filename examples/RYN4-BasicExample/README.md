# RYN4 Basic Example

Simple demonstration of the RYN4 relay control library for ESP32.

## Features Demonstrated

- Modbus RTU initialization (8N1 - factory default parity)
- RYN4 device setup and initialization
- Individual relay ON/OFF control
- Relay toggle operation
- Multi-relay batch control
- Timed relay operations (DELAY command)
- Reading relay states

## Hardware Requirements

- ESP32 development board
- RYN404E (4-channel) or RYN408F (8-channel) relay module
- RS485 transceiver (e.g., MAX485, SP3485)

## Wiring

| ESP32 | MAX485 | RYN4 Module |
|-------|--------|-------------|
| GPIO17 (TX) | DI | - |
| GPIO16 (RX) | RO | - |
| GPIO4 | DE+RE | - |
| - | A | A+ |
| - | B | B- |
| GND | GND | GND |

## RYN4 Module Setup

1. **Slave Address (DIP Switches A0-A5)**: Set to 0x01 (default)
   - A0=ON, A1-A5=OFF for address 0x01

2. **Baud Rate (M1/M2 Jumpers)**: 9600 baud
   - M1=OPEN, M2=OPEN for 9600 baud

3. **Mode (M0 Jumper)**: Modbus RTU mode
   - M0=OPEN (required for this library)

## Building

```bash
cd example/RYN4-BasicExample
pio run
```

## Uploading

```bash
pio run -t upload
```

## Monitor Output

```bash
pio device monitor
```

Expected output:
```
========================================
RYN4 Basic Example
========================================

Initializing Modbus RTU...
Modbus RTU initialized (9600 baud, 8E1)
Creating RYN4 device at address 0x01...
Initializing RYN4...
RYN4 initialized successfully!

Initial relay states:
Relay States: R1:OFF R2:OFF R3:OFF R4:OFF R5:OFF R6:OFF R7:OFF R8:OFF

=== Test 1: Turn Relay 1 ON ===
Result: SUCCESS
Relay States: R1:ON R2:OFF R3:OFF R4:OFF R5:OFF R6:OFF R7:OFF R8:OFF
...
```

## Customization

Edit `src/main.cpp` to change:
- GPIO pins for Modbus (lines 24-27)
- RYN4 slave address (line 31)
- Test timing (line 34)

## Troubleshooting

1. **"RYN4 initialization failed"**
   - Check RS485 wiring (A/B polarity)
   - Verify slave address matches DIP switch setting
   - Ensure M0 jumper is OPEN for Modbus mode

2. **No response from relays**
   - Check power supply to relay module (12V/24V DC)
   - Verify baud rate matches M1/M2 jumper setting
   - Try swapping A and B wires

3. **Intermittent communication**
   - Add 120 ohm termination resistor on long RS485 runs
   - Check for loose connections
