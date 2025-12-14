import asyncio
import sys
import os
from bleak import BleakScanner, BleakClient, BleakError

DEVICE_NAME = "SKIBIDI"

UUID_SERVICE = "00000000-0000-0000-0000-000000000020"
UUID_WRITE   = "00000000-0000-0000-0000-000000000003"   # emu_in
UUID_NOTIFY  = "00000000-0000-0000-0000-000000000003"   # emu_out

CMD_FILE = "test.txt"


async def get_characteristic_or_fail(client, uuid):
    """Ensures the characteristic exists, otherwise prints all services."""
    print("Reading services...")

    # ON WINDOWS AND BLEAK >= 0.22 — Services are already loaded
    services = client.services

    if services is None:
        # VERY rare Windows bug, force refresh by accessing services
        await asyncio.sleep(1)
        services = client.services

    for service in services:
        for char in service.characteristics:
            if char.uuid.lower() == uuid.lower():
                print(f"FOUND characteristic {uuid}")
                return char

    print("\nERROR: Characteristic", uuid, "not found!")
    print("Available characteristics:")
    for service in services:
        for char in service.characteristics:
            print(" ", char.uuid)

    raise BleakError(f"Characteristic {uuid} not found")



async def send_file(client, write_char):
    if not os.path.exists(CMD_FILE):
        print(f"File {CMD_FILE} not found.")
        return

    print(f"Sending commands from {CMD_FILE}...")

    with open(CMD_FILE, "r") as f:
        lines = f.readlines()

    for i, line in enumerate(lines, start=1):
        clean_line = line.strip()
        if not clean_line:
            continue

        try:
            data = bytes.fromhex(clean_line)
        except ValueError:
            print(f"Invalid hex string on line {i}: {clean_line}")
            continue

        try:
            await client.write_gatt_char(write_char, data, response=True)
            print(f"Message {i} sent:", clean_line)
        except Exception as e:
            print(f"Failed to send message {i}: {e}")


async def main():
    print("Scanning...")
    device = await BleakScanner.find_device_by_filter(
        lambda d, ad: d.name == DEVICE_NAME or ad.local_name == DEVICE_NAME,
        timeout=10
    )

    if not device:
        print("Device not found.")
        return

    print("Connecting to", device.address)

    async with BleakClient(device) as client:
        print("Connected. MTU:", client.mtu_size)

        # Discover and validate characteristic
        write_char = await get_characteristic_or_fail(client, UUID_WRITE)

        # Initial send
        await send_file(client, write_char.uuid)

        print("\nManual mode:")
        msg_num = 1

        while True:
            user_input = await asyncio.to_thread(input, f"Message {msg_num}> ")
            user_input = user_input.strip()

            if not user_input:
                break

            if user_input.lower() == "r":
                await send_file(client, write_char.uuid)
                continue

            try:
                data = bytes.fromhex(user_input)
            except ValueError:
                print("Invalid hex string.")
                continue

            try:
                await client.write_gatt_char(write_char.uuid, data, response=False)
                print("Message sent")
            except BleakError as e:
                print("Failed to send:", e)

            msg_num += 1
            await asyncio.sleep(0.1)


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("Interrupted.")
