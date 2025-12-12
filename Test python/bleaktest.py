import asyncio
from bleak import BleakScanner, BleakClient

DEVICE_NAME = "SKIBIDI"

async def main():
    print(f"Scanning for {DEVICE_NAME}...")
    device = await BleakScanner.find_device_by_filter(
        lambda d, ad: d.name == DEVICE_NAME or ad.local_name == DEVICE_NAME
    )

    if not device:
        print(f"{DEVICE_NAME} not found.")
        return

    print(f"Found {DEVICE_NAME} at {device.address}. Connecting...")
    
    async with BleakClient(device) as client:
        print(f"Connected to {DEVICE_NAME}!")
        print(f"Device MTU: {client.mtu_size}")
        
        # Keep connection alive for 5 seconds so you can verify it on the device
        await asyncio.sleep(5)
        
    print("Disconnected.")

if __name__ == "__main__":
    asyncio.run(main())