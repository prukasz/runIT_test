#!/usr/bin/env python3
from bluepy.btle import Scanner, Peripheral, BTLEException
import sys
import time

MAX_MESSAGE_SIZE = 250
DEVICE_NAME = "SKIBIDI"
UUID = "00000000-0000-0000-0000-000000000003"
CMD_FILE = "valuedump.txt"

# ------------------------------------------------------------
# SAFE “PARALLEL” SCANNING (non-blocking loop, no threads)
# ------------------------------------------------------------
def find_device():
    scanner = Scanner()
    print("Scanning for BLE devices...")

    while True:
        try:
            devices = scanner.scan(1.0)  # short scan, repeated
        except BTLEException:
            continue

        for dev in devices:
            for (_, desc, value) in dev.getScanData():
                if desc == "Complete Local Name" and value == DEVICE_NAME:
                    print(f"Found {DEVICE_NAME} at address {dev.addr}")
                    return dev.addr

# ------------------------------------------------------------
# Function to send all commands from file
# ------------------------------------------------------------
def send_file(write_char):
    with open(CMD_FILE) as f:
        for i, line in enumerate(f, start=1):
            clean_line = line.strip()
            if not clean_line:
                continue
            try:
                data = bytes.fromhex(clean_line)
            except ValueError:
                print(f"Invalid hex string on line {i}: {clean_line}")
                continue
            try:
                write_char.write(data, withResponse=False)
                print(f"Message {i} sent:", " ".join(f"{b:02X}" for b in data))
            except BTLEException as e:
                print(f"Failed to send message {i}: {e}")
            time.sleep(0.05)  # small delay between messages

# ------------------------------------------------------------
# MAIN
# ------------------------------------------------------------
try:
    esp_mac = find_device()

    if not esp_mac:
        print(f"{DEVICE_NAME} not found")
        sys.exit(1)

    print("Connecting to ESP32...")
    time.sleep(0.3)  # allow HCI to settle
    esp = Peripheral(esp_mac)
    print("Connected!")

    esp.setMTU(MAX_MESSAGE_SIZE)

    chars = esp.getCharacteristics(uuid=UUID)
    if not chars:
        print(f"Characteristic not found!")
        sys.exit(1)
    write_char = chars[0]

    # --------------------------------------------------------
    # Send commands from file initially
    # --------------------------------------------------------
    send_file(write_char)

    # --------------------------------------------------------
    # Manual mode
    # --------------------------------------------------------
    print("\nManual mode")
    msg_num = sum(1 for _ in open(CMD_FILE)) + 1

    while True:
        user_input = input(f"Message {msg_num} > ").strip()
        if not user_input:
            print("Exiting")
            break

        # Resend file on 'R' or 'r'
        if user_input.lower() == "r":
            print("Resending file...")
            send_file(write_char)
            msg_num += sum(1 for _ in open(CMD_FILE))
            continue

        try:
            data = bytes.fromhex(user_input)
        except ValueError:
            print("Invalid string")
            continue

        try:
            write_char.write(data, withResponse=False)
            print("Message sent:", " ".join(f"{b:02X}" for b in data))
        except BTLEException as e:
            print(f"Failed to send message {msg_num}: {e}")

        msg_num += 1
        time.sleep(0.2)

except BTLEException as e:
    print(f"Bluetooth error: {e}")

finally:
    if 'esp' in locals():
        try:
            esp.disconnect()
            print("Disconnected from ESP32.")
        except:
            pass
