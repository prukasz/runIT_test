import asyncio
import sys
import os
import re  # Dodano do obsługi regex
from bleak import BleakScanner, BleakClient, BleakError
from MessageDispatch import notification_handler, set_display_mode, DisplayMode

set_display_mode(DisplayMode.PRETTY)

DEVICE_NAME = "SKIBIDI"

UUID_SERVICE = "00000000-0000-0000-0000-000000000020"
UUID_WRITE   = "00000000-0000-0000-0000-000000000003"   # emu_in
UUID_NOTIFY  = "00000000-0000-0000-0000-000000000003"   # emu_out
UUID_READ  = "00000000-0000-0000-0000-000000000002"

CMD_FILE = "test_dump.txt"

# Created inside main() so it's bound to the running event loop
_ready_event: asyncio.Event = None  # type: ignore

async def _ready_ack_handler(sender, data: bytearray):
    """Called when device sends the 0x00 ACK on emu_in characteristic."""
    _ready_event.set()
    
async def get_characteristic_or_fail(client, uuid):
    """Ensures the characteristic exists, otherwise prints all services."""
    print("Reading services...")

    # ON WINDOWS AND BLEAK >= 0.22 — Services are already loaded
    services = client.services

    if services is None:
        # VERY rare Windows bug, force refresh by accessing services
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
    global _ready_event
    if not os.path.exists(CMD_FILE):
        print(f"File {CMD_FILE} not found.")
        return

    print(f"Sending commands from {CMD_FILE}...")

    with open(CMD_FILE, "r") as f:
        lines = f.readlines()

    # Prime the event so the first packet is sent immediately
    _ready_event.set()

    for i, line in enumerate(lines, start=1):
        # Strip whitespace first
        stripped = line.strip()
        
        # Skip empty lines
        if not stripped:
            continue
        
        # Remove all #...# comment blocks (greedy to handle multiple)
        line_no_comments = re.sub(r'#[^#]*#', '', stripped)
        
        # Remove all whitespace
        clean_line = "".join(line_no_comments.split())

        if not clean_line:
            continue

        try:
            data = bytes.fromhex(clean_line)
        except ValueError:
            print(f"Invalid hex string on line {i}: {clean_line}")
            continue

        try:
            try:
                await asyncio.wait_for(_ready_event.wait(), timeout=5.0)
            except asyncio.TimeoutError:
                print(f"Warning: ACK timeout for message {i}, sending anyway")
            _ready_event.clear()
            await client.write_gatt_char(write_char, data, response=False)
            print(f"Message {i} sent: {data.hex().upper()}")
        except Exception as e:
            print(f"Failed to send message {i}: {e}")


async def main():
    global _ready_event
    _ready_event = asyncio.Event()
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

        await client.start_notify(read_char.uuid, notification_handler)
        # Subscribe to emu_in for ready-ACK from device
        await client.start_notify(write_char.uuid, _ready_ack_handler)

        # Initial send
        await send_file(client, write_char)

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
                try:
                    await asyncio.wait_for(_ready_event.wait(), timeout=5.0)
                except asyncio.TimeoutError:
                    print("Warning: ACK timeout, sending anyway")
                _ready_event.clear()
                await client.write_gatt_char(write_char, data, response=False)
                print("Message sent")
            except BleakError as e:
                print("Failed to send:", e)

            msg_num += 1
            #await asyncio.sleep(0.1)


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("Interrupted.")
