import asyncio
import os
import sys
from abc import ABC
from typing import Callable, Optional
from bleak import BleakScanner, BleakClient, BleakError, BleakGATTCharacteristic

# ==========================================
# 1. ABSTRACT BASE / DRIVER CLASS
# ==========================================
class BleakDriver(ABC):
    """
    Abstract wrapper for BleakClient. 
    Handles connection, raw sending, notifications, and disconnection.
    """
    def __init__(self):
        self.client: Optional[BleakClient] = None
        self._connected = False

    async def connect(self, device_name: str, timeout: float = 10.0) -> bool:
        print(f"[BLE] Scanning for device with name: {device_name}...")
        
        device = await BleakScanner.find_device_by_filter(
            lambda d, ad: d.name == device_name or ad.local_name == device_name,
            timeout=timeout
        )

        if not device:
            print(f"[BLE] Device '{device_name}' not found.")
            return False

        print(f"[BLE] Found {device.address}. Connecting...")
        self.client = BleakClient(device.address)

        try:
            await self.client.connect()
            self._connected = True
            print(f"[BLE] Connected! MTU: {self.client.mtu_size}")
            return True
        except Exception as e:
            print(f"[BLE] Connection failed: {e}")
            self._connected = False
            return False

    async def disconnect(self):
        if self.client and self._connected:
            print("[BLE] Disconnecting...")
            await self.client.disconnect()
            self._connected = False
            print("[BLE] Disconnected.")

    async def send(self, uuid: str, data: bytes, response: bool = False):
        if not self._connected or not self.client:
            print("[BLE] Error: Not connected.")
            return

        try:
            await self.client.write_gatt_char(uuid, data, response=response)
        except BleakError as e:
            print(f"[BLE] Send Error: {e}")

    async def receive(self, uuid: str, callback: Callable):
        if not self._connected or not self.client:
            return

        try:
            await self.client.start_notify(uuid, callback)
            print(f"[BLE] Listening on {uuid}")
        except BleakError as e:
            print(f"[BLE] Notify Error on {uuid}: {e}")

# ==========================================
# 2. MAIN CLASS (Application Logic)
# ==========================================
class SkibidiTester(BleakDriver):
    """
    Concrete implementation with Flow Control (Stop-and-Wait).
    """
    def __init__(self, write_uuid: str, notify_uuid: str):
        super().__init__()
        self.write_uuid = write_uuid
        self.notify_uuid = notify_uuid
        # Event flag for flow control (Red Light / Green Light)
        self.ack_event = asyncio.Event()

    def _on_notification(self, sender: BleakGATTCharacteristic, data: bytearray):
        """
        Callback for ALL received data. 
        Distinguishes between ACK (Write UUID) and DEBUG LOGS (Notify UUID).
        """
        # 1. Handle ACK from the Write Channel (...003)
        if sender.uuid.lower() == self.write_uuid.lower():
            # Unblock the sender
            self.ack_event.set()
            
        # 2. Handle Debug/Other Logs (...002)
        elif sender.uuid.lower() == self.notify_uuid.lower():
            print(f"\n[RX LOG] {data.hex().upper()}\nCmd> ", end="", flush=True)

    async def sender(self, filename: str):
        """
        Sends file line-by-line, WAITING for confirmation (ACK) before sending the next line.
        """
        if not os.path.exists(filename):
            print(f"[APP] File {filename} not found.")
            return

        print(f"[APP] Parsing {filename}...")
        with open(filename, "r") as f:
            lines = f.readlines()

        print(f"[APP] Sending {len(lines)} commands with Flow Control...")

        for i, line in enumerate(lines, start=1):
            clean_line = line.strip()
            if not clean_line:
                continue

            try:
                data_bytes = bytes.fromhex(clean_line)
            except ValueError:
                print(f"[APP] Line {i} Invalid Hex: {clean_line}")
                continue

            # --- FLOW CONTROL START ---
            self.ack_event.clear()  # Set light to RED

            # Send the packet
            await self.send(self.write_uuid, data_bytes, response=False)

            # Wait for light to turn GREEN (Timeout 1.0s)
            try:
                await asyncio.wait_for(self.ack_event.wait(), timeout=1.0)
                # print(f"[APP] Line {i} sent & ACK'd")
            except asyncio.TimeoutError:
                print(f"[APP] Line {i} TIMEOUT waiting for ACK. Moving on...")
            # --- FLOW CONTROL END ---
            
        print("[APP] File dump finished.")

    async def run_interactive(self, filename: str):
        """The main interactive loop."""
        
        # 1. SETUP SUBSCRIPTIONS (Essential for ACKs)
        await self.receive(self.notify_uuid, self._on_notification)
        if self.write_uuid != self.notify_uuid:
             await self.receive(self.write_uuid, self._on_notification)

        # 2. AUTO-SEND FILE IMMEDIATELY
        await self.sender(filename)

        # 3. ENTER MANUAL MODE
        print("\n--- Interactive Mode ---")
        print(" [r] Reload and resend file")
        print(" [q] Quit")
        print(" [hex] Send manual hex string")
        
        msg_cnt = 1
        
        while True:
            try:
                user_input = await asyncio.to_thread(input, f"Cmd {msg_cnt}> ")
                user_input = user_input.strip()
            except EOFError:
                break

            if not user_input:
                continue

            if user_input.lower() == 'q':
                break
            
            if user_input.lower() == 'r':
                await self.sender(filename)
                continue

            # Manual Hex Entry
            try:
                data = bytes.fromhex(user_input)
                
                # Use Flow Control for manual sends too
                self.ack_event.clear()
                await self.send(self.write_uuid, data, response=False)
                
                try:
                    await asyncio.wait_for(self.ack_event.wait(), timeout=1.0)
                    print("[APP] Packet Delivered & Confirmed.")
                except asyncio.TimeoutError:
                    print("[APP] Warning: No confirmation received.")
                    
            except ValueError:
                print("[APP] Invalid input.")

            msg_cnt += 1
            await asyncio.sleep(0.1)

# ==========================================
# 3. EXECUTION
# ==========================================
async def main():
    # Constants
    DEVICE_NAME = "SKIBIDI"
    UUID_WRITE  = "00000000-0000-0000-0000-000000000003"
    UUID_NOTIFY = "00000000-0000-0000-0000-000000000003"
    FILE_NAME   = "test.txt"

    # Create the App instance
    app = SkibidiTester(write_uuid=UUID_WRITE, notify_uuid=UUID_NOTIFY)

    # 1. Connect
    if await app.connect(DEVICE_NAME):
        try:
            # 2. Run Main Logic
            await app.run_interactive(FILE_NAME)
        finally:
            # 3. Cleanup
            await app.disconnect()

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nExited by user.")