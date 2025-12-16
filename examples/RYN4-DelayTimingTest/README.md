# RYN4 DELAY Command Timing Test

Standalone test to verify DELAY command timing and readback behavior on RYN4 relay modules.

## Background

In the boiler controller, we're seeing "Relay verification SUCCESS after 2 attempts" when using DELAY commands. This test determines whether:

1. RYN4 hardware has inherent delay between command acceptance and state reflection
2. This is a bus/timing issue on our side

## Test Sequence

### Test 1: Single Relay DELAY Timing
1. Send DELAY command to relay 1 (e.g., `0x0614` = ON for 20s)
2. Immediately read back holding register
3. Log the time delta and returned value
4. Repeat read every 100ms for 2 seconds
5. Verify we always get `0x0001` (ON state)

### Test 2: Multiple Relay DELAY (Batch)
1. Send DELAY 10s to relays 1, 2, 3 via `setMultipleRelayCommands()`
2. Read back bitmap status every 100ms
3. Verify all 3 relays show ON immediately

### Test 3: Bitmap vs Register Read
1. Compare timing of bitmap read (`0x0080`) vs individual register read
2. Verify both return same relay states

## Hardware Setup

- RYN4 8-channel relay module (RYN408F or RYN404E)
- RS485 Modbus RTU connection
- ESP32 master

Default pin configuration:
```cpp
#define MODBUS_RX_PIN 36
#define MODBUS_TX_PIN 4
#define MODBUS_BAUD_RATE 9600
#define RYN4_ADDRESS 0x02
```

## Building and Running

```bash
# Build with debug output
pio run -e dev_debug_selective

# Upload and monitor
pio run -e dev_debug_selective -t upload && pio device monitor
```

## Expected Output

If RYN4 responds immediately:
```
Attempt | Time (ms) | Delta (ms) | Bitmap | Relay 1
--------|-----------|------------|--------|--------
     1  |    1234   |       5    |  0x01  | ON
     2  |    1334   |     105    |  0x01  | ON
     ...

>> RESULT: Relay shows ON immediately after DELAY command <<
>> No inherent hardware delay detected <<
```

If there's hardware delay:
```
Attempt | Time (ms) | Delta (ms) | Bitmap | Relay 1
--------|-----------|------------|--------|--------
     1  |    1234   |       5    |  0x00  | OFF
     2  |    1334   |     105    |  0x01  | ON
     ...

>> RESULT: Relay shows ON after DELAY in readback <<
>> Hardware delay detected: ~105 ms <<
```

## DELAY Command Format

- `0x06XX` where XX is delay in seconds
- Example: `0x0614` = DELAY 20 seconds (0x14 = 20)
- DELAY 0 (`0x0600`) = Cancel timer and turn OFF immediately

## Reference

This test is based on the DELAY usage pattern in the boiler controller:
- `src/modules/tasks/RYN4ProcessingTask.cpp` - SET/READ tick logic
- `src/shared/RelayState.h` - DELAY tracking structure
