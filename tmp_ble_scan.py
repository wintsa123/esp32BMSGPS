import asyncio
from bleak import BleakScanner

async def main():
    ds = await BleakScanner.discover(timeout=10)
    for d in ds:
        print(repr(d.name), d.address)

asyncio.run(main())
