# CraftifAI Hackathon Safety Beacon

A Bluetooth Low Energy (BLE) safety beacon prototype built for a hackathon. The repository contains two complementary pieces:

- an ESP-IDF firmware project that reads a DHT22 sensor and advertises sensor data over BLE
- a Python monitor that listens for advertisements from the beacon and prints decoded readings such as temperature, humidity, and a safety status

## What this project does

The idea is to turn a small environmental sensor into a wearable or device-side safety beacon:

1. A DHT22 sensor samples temperature and humidity.
2. The firmware packages the reading into BLE advertising data.
3. A host-side Python scanner detects the beacon and decodes the payload.

This makes it possible to quickly inspect nearby sensor data from a laptop or other Bluetooth-enabled device.

## Repository structure

- [adv_scaner.py](adv_scaner.py) - Python BLE scanner that watches for advertisements from a device named `DHT22` and prints decoded values.
- [dht22_uart_stream/](dht22_uart_stream) - ESP-IDF firmware project for an ESP32-class target.
  - [dht22_uart_stream/firmware/app/app.c](dht22_uart_stream/firmware/app/app.c) - firmware entrypoint that starts the sensor provider and BLE advertising.
  - [dht22_uart_stream/firmware/services/](dht22_uart_stream/firmware/services) - sensor and BLE advertisement services.
  - [dht22_uart_stream/firmware/README.md](dht22_uart_stream/firmware/README.md) - more detail about the firmware layout.
- [Safety_beacon.html](Safety_beacon.html) - exported project/chat transcript artifact.
- [Screenshot 2026-08-08 164625.png](Screenshot%202026-08-08%20164625.png) - project screenshot.

## Requirements

### Python monitor

- Python 3.8+
- `bleak` package

Install the Python dependency:

```bash
pip install bleak
```

### Firmware

- ESP-IDF installed and configured
- An ESP32/ESP32-C3 target board with a connected DHT22 sensor

## Quick start

### 1. Run the Python scanner

From the repository root:

```bash
python adv_scaner.py
```

The script will scan for BLE advertisements and print decoded information when it detects a device named `DHT22`.

### 2. Build and flash the firmware

From the firmware project directory:

```bash
cd dht22_uart_stream
idf.py set-target esp32c3
idf.py build
idf.py flash monitor
```

If you are using a different ESP target, adjust the target accordingly.

## Notes

- The monitor currently looks for the BLE local name `DHT22`.
- The firmware advertises sensor data as manufacturer-specific BLE data.
- The project is intended as a prototype/hackathon demo and may be expanded with stronger parsing, thresholds, and a richer dashboard.

## Future ideas

- Add a proper mobile or web dashboard for live beacon data.
- Improve payload format versioning and validation.
- Add configurable safety thresholds and alerting.
- Extend the firmware to support additional sensors or multiple beacon modes.
