import asyncio
import struct
from bleak import BleakScanner

def detection_callback(device, adv_data):
    name = adv_data.local_name or device.name or ""
    
    # Only process our specific DHT22 beacon
    if name == "DHT22":
        print(f"\n📡 [DHT22 BEACON] {device.address} | RSSI: {adv_data.rssi} dBm")
        
        # Look for our custom manufacturer ID: 0xFFFF (65535 in decimal)
        if adv_data.manufacturer_data and 0xFFFF in adv_data.manufacturer_data:
            raw_bytes = adv_data.manufacturer_data[0xFFFF]
            
            # Print the raw hex just so we can see what's coming in
            print(f"Raw Hex: {raw_bytes.hex()}")
            
            # Ensure we have at least the 6 bytes we expect
            if len(raw_bytes) >= 6:
                # Byte 0: Version
                version = raw_bytes[0]
                
                # Byte 1: Flags (Bit 0 is the safe flag)
                flags = raw_bytes[1]
                is_safe = bool(flags & 0x01)
                
                # Bytes 2-3: Temperature (int16, little-endian) -> format '<h'
                temp_centi = struct.unpack('<h', raw_bytes[2:4])[0]
                temp_c = temp_centi / 10.0
                
                # Bytes 4-5: Humidity (uint16, little-endian) -> format '<H'
                hum_centi = struct.unpack('<H', raw_bytes[4:6])[0]
                hum_rh = hum_centi / 10.0
                
                # Determine display status
                status_icon = "✅ SAFE" if is_safe else "❌ UNSAFE (or Sensor Error)"
                
                # Print the decoded dashboard
                print(f"  ├─ Version:   {version}")
                print(f"  ├─ Status:    {status_icon} (Flag: {flags})")
                print(f"  ├─ Temp:      {temp_c:.2f} °C")
                print(f"  └─ Humidity:  {hum_rh:.2f} %RH")
            else:
                print(f"  └─ ⚠️ Payload too short: {len(raw_bytes)} bytes")

async def main():
    print("Starting DHT22 Beacon Monitor...")
    print("Press Ctrl+C to stop.\n")
    
    scanner = BleakScanner(detection_callback)
    await scanner.start()
    
    # Keep the script running forever so it acts as a continuous monitor
    try:
        while True:
            await asyncio.sleep(1)
    except KeyboardInterrupt:
        print("\nStopping monitor...")
    finally:
        await scanner.stop()
        print("Monitor stopped.")

if __name__ == "__main__":
    asyncio.run(main())