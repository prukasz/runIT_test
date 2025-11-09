#!/usr/bin/env python3
from bluepy.btle import Scanner, Peripheral, BTLEException
import os
import sys
import time


DEVICE_NAME = "SKIBIDI"
f = open("message.txt")

os.system("sudo hciconfig hci0 down")
os.system("sudo hciconfig hci0 up")

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
        print(f"Device {DEVICE_NAME} not found. Exiting.")
        sys.exit(1)

    print("Connecting to ESP32...")
    esp = Peripheral(esp_mac)
    print("Connected!")

    write_char = esp.getCharacteristics(uuid="00000000-0000-0000-0000-000000000003")[0]

    for i, line in enumerate(f, start=1):
        clean_line = line.strip()
        try:
            data = bytes.fromhex(clean_line)
        except ValueError:
            print(f"Invalid hex string on line {i}: {clean_line}")
            continue

        print(f"Sending message {i}: {data}")
        write_char.write(data, withResponse=False)
        print(f"Message {i} sent successfully!")
        time.sleep(5)

except BTLEException as e:
    print(f"Bluetooth error: {e}")

finally:
    try:
        esp.disconnect()
        print("Disconnected from ESP32.")
    except Exception:
        pass

    try:
        scanner.clear()
    except Exception:
        pass
