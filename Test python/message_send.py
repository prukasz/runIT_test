#!/usr/bin/env python3
from bluepy.btle import Scanner, Peripheral, BTLEException
import sys
import time
import subprocess

MAX_MESSAGE_SIZE = 250
DEVICE_NAME = "SKIBIDI"
UUID = "00000000-0000-0000-0000-000000000003"
CMD_FILE = "valuedump.txt"
subprocess.run(["sudo", "hciconfig", "hci0", "down"])
subprocess.run(["sudo", "hciconfig", "hci0", "up"])

scanner = Scanner()
esp_mac = None

try:
    print("Scanning for BLE devices...")
    devices = scanner.scan(5.0)

    for dev in devices:
        for (adtype, desc, value) in dev.getScanData():
            if desc == "Complete Local Name" and value == DEVICE_NAME:
                esp_mac = dev.addr
                print(f"Found {DEVICE_NAME} at address {esp_mac}")
                break
        if esp_mac:
            break

    if not esp_mac:
        print(f"{DEVICE_NAME} not found")
        sys.exit(1)

    print("Connecting to ESP32...")
    esp = Peripheral(esp_mac)
    print("Connected!")
    esp.setMTU(MAX_MESSAGE_SIZE)

    chars = esp.getCharacteristics(uuid=UUID)
    if not chars:
        print(f"Characteristic not found!")
        sys.exit(1)
    write_char = chars[0]
    

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
                hex_str = " ".join(f"{b:02X}" for b in data)
                print(f"Message {i} sent: {hex_str}")
            except BTLEException as e:
                print(f"Failed to send message {i}: {e}")
            #time.sleep()

    print("\nManual mode")
    msg_num = i + 1
    while True:
        user_input = input(f"Message {msg_num} > ").strip()
        if not user_input:
            print("Exiting")
            break
        try:
            data = bytes.fromhex(user_input)
        except ValueError:
            print("Invalid string")
            continue

        try:
            write_char.write(data, withResponse=False)
            hex_str = " ".join(f"{b:02X}" for b in data)
            print(f"Message {msg_num} sent: {hex_str}")
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
        except Exception:
            pass
    try:
        scanner.clear()
    except Exception:
        pass
