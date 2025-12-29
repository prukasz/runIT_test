import asyncio
import sys
import os
import re  # Dodano do obsługi regex
from bleak import BleakScanner, BleakClient, BleakError

DEVICE_NAME = "SKIBIDI"

UUID_SERVICE = "00000000-0000-0000-0000-000000000020"
UUID_WRITE   = "00000000-0000-0000-0000-000000000003"   # emu_in
UUID_NOTIFY  = "00000000-0000-0000-0000-000000000003"   # emu_out
UUID_READ  = "00000000-0000-0000-0000-000000000002"

CMD_FILE = "test.txt" # Zmieniono na plik generowany przez FullDump

current_message_chunks = []  # Only current message
current_message_expected_len = 0
current_message_received_len = 0
new_message_flag = True

def indication_handler(sender: int, data: bytearray):
    global current_message_chunks, current_message_expected_len, current_message_received_len, new_message_flag

    data_bytes = bytes(data)
    
    if new_message_flag:
        current_message_chunks.clear()
        current_message_expected_len = 0
        current_message_received_len = 0
    # First chunk - extract 2-byte length header
        if len(data_bytes) < 2:
            print("Error: First chunk too short")
            return
    # Parse length from first 2 bytes (big-endian)
        current_message_expected_len = (data_bytes[0] << 8) | data_bytes[1]
    
    # Store data WITHOUT the header (skip first 2 bytes)
        actual_data = data_bytes[2:]
        current_message_chunks.append(actual_data)
        current_message_received_len = len(actual_data)
        new_message_flag = False
    
        print(f"   NEW MESSAGE - First chunk received")
        print(f"   Expected total: {current_message_expected_len} bytes")
        print(f"   Received data: {len(actual_data)} bytes")
    else:
        current_message_chunks.append(data_bytes)
        current_message_received_len += len(data_bytes)
        print(f"   Chunk received: {len(data_bytes)} bytes")
    
    if current_message_received_len >= current_message_expected_len:
        print(f"   MESSAGE COMPLETE: {current_message_received_len}/{current_message_expected_len} bytes")
        new_message_flag = True  # Reset for next message
    

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
        # --- MODYFIKACJA START ---
        # 1. Usuń wszystko co znajduje się pomiędzy # a # (włącznie z #)
        #    Regex: #.*?# (dopasowanie leniwe)
        line_no_comments = re.sub(r'#.*?#', '', line)
        
        # 2. Usuń wszystkie białe znaki (spacje, entery)
        clean_line = "".join(line_no_comments.split())
        # --- MODYFIKACJA KONIEC ---

        if not clean_line:
            continue

        try:
            data = bytes.fromhex(clean_line)
        except ValueError:
            print(f"Invalid hex string on line {i}: {clean_line}")
            continue

        try:
            await client.write_gatt_char(write_char, data, response=True)
            print(f"Message {i} sent: {data.hex().upper()}")
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
        read_char = await get_characteristic_or_fail(client, UUID_READ)

        await client.start_notify(read_char.uuid, indication_handler)

        global current_message_chunks, current_message_expected_len, current_message_received_len, new_message_flag

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

            if user_input.lower() == "show":             
                if current_message_chunks != []:
                    combined = b''.join(current_message_chunks)
                    print(f"Expected length: {current_message_expected_len} bytes")
                    print(f"Received length: {len(combined)} bytes")
                    
                    if len(combined) == current_message_expected_len:
                        print("Message is COMPLETE")
                    else:
                        print(f"Message is INCOMPLETE ({len(combined)}/{current_message_expected_len})")
                    
                    print(f"\nCombined data ({len(combined)} bytes):")
                    print(combined.hex().upper())
                    
                    print("\nFormatted view:")
                    for i in range(0, len(combined), 16):
                        chunk = combined[i:i+16]
                        hex_str = ' '.join(f'{b:02X}' for b in chunk)
                        print(f"{i:04d}: {hex_str}")
                else:
                    print("No message received yet.")
                continue

            if user_input.lower() == "read":
                try:
                    data = await client.read_gatt_char(read_char.uuid)
                    print(f"Read value: {data.hex().upper()}")
                    # Wyświetlenie jako liczba binarna
                    print(f"Length: {len(data)} bytes")
                except Exception as e:
                    print(f"Failed to read: {e}")
                continue

            try:
                # W trybie manualnym też warto obsłużyć wklejanie z komentarzami
                no_comments = re.sub(r'#.*?#', '', user_input)
                clean_input = "".join(no_comments.split())
                data = bytes.fromhex(clean_input)
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