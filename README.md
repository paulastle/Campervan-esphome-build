# Campervan32

An [ESPHome](https://esphome.io)-based ESP32-S3 dashboard for campervan monitoring, running on small round Waveshare displays. Integrates with [Home Assistant](https://www.home-assistant.io/) for remote monitoring.

This is built for a specific campervan setup — it's shared as a reference for anyone building something similar. Fork it and adapt it to your own needs.

It has been created with Claude code i dont profess to be a programmer!

## Hardware

Two Waveshare display variants are supported, each with its own configuration file:

| Display | Resolution | Config file |
|---------|-----------|-------------|
| [ESP32-S3-Touch-LCD-1.46B](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.46B) | 412x412 round IPS LCD | `waveshare-146.yaml` (Previous project use the 1.75!) |
| [ESP32-S3-Touch-AMOLED-1.75](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75) | 466x466 AMOLED | `waveshare-175.yaml` |

Both boards have an ESP32-S3 with 16 MB flash and 8 MB PSRAM, plus onboard IMU, RTC, and capacitive touch.

## Connected devices

The dashboard reads data from these BLE peripherals:

- [Victron SmartSolar MPPT](https://www.victronenergy.com/) — solar charge controller (via [victron_ble](https://github.com/Fabian-Schmidt/esphome-victron_ble))
- [Victron SmartShunt](https://www.victronenergy.com/) — battery monitor (via victron_ble)
- [Victron Blue Smart IP22 Charger](https://www.victronenergy.com/) — AC mains charger (via victron_ble)
- [ThermoPro TP357](https://www.amazon.co.uk/dp/B0BPX7S67L) — Bluetooth hygrometer/thermometer
- [ThermoPro TP351](https://www.amazon.co.uk/dp/B0DXN5SK2L) — Bluetooth hygrometer/thermometer (x2)
- [BM6 Battery Monitor](https://www.amazon.co.uk/Battery-Monitor-Campervans-Compatible-bluetooth/dp/B0C2QSDV5H/) — starter battery monitor (1.75" variant only, via BLE GATT + AES decryption)

## Features

- Energy flow visualisation with live power balance
- Solar: live power, daily yield, charge status
- Battery: voltage, current, state of charge, time-to-go, history
- Starter battery monitoring (1.75" only): voltage, SOC, temperature via BM6
- Mains/AC charger: status, power, history
- Temperature: van interior, outside, fridge with 24-hour history graphs
- Screensaver with analog clock
- Settings: brightness, alarms, screen rotation (1.75" only), network diagnostics
- Home Assistant integration via ESPHome native API

## Prerequisites

- [ESPHome](https://esphome.io/guides/installing_esphome) CLI (`pip install esphome` or `brew install esphome`)
- Home Assistant (optional, for remote monitoring and control)

## Victron BLE credentials

Each Victron device needs its MAC address and encryption key in `secrets.yaml`. To find these:

1. Open the **VictronConnect** app on your phone
2. Connect to the Victron device
3. Go to the device's **Settings** (gear icon)
4. Under **Product info**, find the **MAC address**
5. Enable **Instant readout** if not already enabled — this activates BLE advertising
6. The **encryption key** is shown on the same screen once instant readout is enabled

Enter the MAC as `AA:BB:CC:DD:EE:FF` and the key as a 32-character hex string in `secrets.yaml`.

See the [victron_ble documentation](https://github.com/Fabian-Schmidt/esphome-victron_ble) for more details.

## BM6 and ThermoPro MAC addresses

The BM6 battery monitor and ThermoPro thermometers don't have a dedicated app for finding credentials — you'll need a BLE scanner such as [nRF Connect](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-mobile) (available for iOS and Android).

1. Open nRF Connect and start scanning
2. Look for devices named `BM6`, `TP357` or `TP351` (ThermoPro devices may also appear as `TP3xx`)
3. The MAC address is shown next to each device name — copy it into `secrets.yaml`

No encryption keys are needed for ThermoPro devices. The BM6 uses a fixed AES key that is already included in the firmware.

## Getting started

1. Clone this repo:
   ```bash
   git clone https://github.com/paulastle/campervan32.git
   cd campervan32
   ```

2. Copy `secrets.yaml.example` to `secrets.yaml` and fill in your credentials:
   ```bash
   cp secrets.yaml.example secrets.yaml
   ```

3. Choose the YAML file matching your hardware and build:
   ```bash
   esphome compile waveshare-175.yaml
   ```

4. Flash via USB (first time):
   ```bash
   esphome run waveshare-175.yaml
   ```

5. Flash via OTA (subsequent updates):
   ```bash
   esphome upload waveshare-175.yaml --device <IP>
   ```

6. View logs:
   ```bash
   esphome logs waveshare-175.yaml --device <IP>
   ```

## Project structure

```
waveshare-146.yaml        Main config — 1.46" round LCD variant
waveshare-175.yaml        Main config — 1.75" AMOLED variant
secrets.yaml.example      Credential template (copy to secrets.yaml)
components/               Custom ESPHome components
  spd2010_glue/           SPD2010 touch driver (1.46" variant)
includes/                 C++ headers
  bm6_decrypt.h           BM6 battery monitor AES decryption
  net_test.h              Network connectivity test
  temp_history.h          Temperature history ring buffer
CLAUDE.md                 AI assistant context (safe to ignore/delete)
```

## Licence

This is a personal project with no warranty or support obligation. You're welcome to fork and adapt it for your own use.
