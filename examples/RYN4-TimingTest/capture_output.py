#!/usr/bin/env python3
import serial
import time
import sys

def capture_serial(port='/dev/ttyACM0', baudrate=115200, duration=30):
    """Capture serial output for specified duration"""
    try:
        ser = serial.Serial(port, baudrate, timeout=1)
        print(f"Connected to {port} at {baudrate} baud")
        print("-" * 60)
        
        start_time = time.time()
        while time.time() - start_time < duration:
            if ser.in_waiting:
                line = ser.readline()
                try:
                    decoded = line.decode('utf-8', errors='replace').strip()
                    if decoded:
                        print(f"[{time.time() - start_time:6.2f}] {decoded}")
                except Exception as e:
                    print(f"[{time.time() - start_time:6.2f}] Raw: {line.hex()}")
        
        print("-" * 60)
        print(f"Capture complete after {duration} seconds")
        ser.close()
    except serial.SerialException as e:
        print(f"Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    capture_serial()