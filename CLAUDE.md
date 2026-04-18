# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is an Arduino sketch for a LoRa-based weather station running on ESP32. It reads multiple environmental sensors and transmits data wirelessly via LoRa radio. The station uses deep sleep between readings to conserve power.

**Target hardware:** ESP32 (custom PCB, not Heltec devkit by default)  
**Board version required:** ESP32 Arduino core 3.x (ESP-IDF 5.x) — tested and working on **3.3.8**; earlier 2.x cores are not supported  
**LoRa frequency:** 915 MHz (US band)

## Building and Flashing

This is an Arduino IDE project. There is no CLI build system. To build and flash:

1. Open `weather_v4_lora.ino` in Arduino IDE
2. Select ESP32 board — tested and known-working with **core version 3.3.8**
3. Compile and upload via USB

To monitor serial output (115200 baud), use the Arduino Serial Monitor or any serial terminal.

## Required Libraries

- `LoRa` — LoRa radio driver
- `DallasTemperature` + `OneWire` — DS18B20 temperature sensor
- `BH1750` **(by Christopher Laws)** — light/lux meter; **must be the standalone library**, not the copy bundled inside the Heltec ESP32 Dev-Boards library. If only the Heltec library is installed, Arduino IDE will pull the entire Heltec bundle to satisfy `BH1750.h` and compilation will fail with missing Heltec-specific defines.
- `BME280I2C` — barometric pressure, humidity, case temperature
- `Adafruit_SI1145` — UV index sensor
- `heltec.h` — only needed if `#define heltec` is enabled; do **not** install the Heltec ESP32 Dev-Boards library unless targeting Heltec hardware

## Architecture

The sketch runs entirely in `setup()` — `loop()` is empty. The station wakes from deep sleep, does work based on wake reason, then sleeps again.

### Wake-up reasons (in `weather_v4_lora.ino`)
- **POR (case 0):** First boot — sets an arbitrary Unix epoch time, schedules first wake in 5 seconds
- **EXT0 (GPIO 25):** Rain tip gauge reed switch closed — increments `rainTicks` in RTC memory, goes back to sleep immediately
- **Timer:** Main work cycle — reads sensors, accumulates rainfall data, and conditionally sends LoRa packets

### Send cadence
`bootCount` is stored in RTC memory and incremented each timer wake. Data is transmitted every `SEND_FREQUENCY_LORA` (5) timer wakes, alternating between two packet types:
- **Environmental packet** (`sensorData` struct): weather readings
- **Hardware/diagnostics packet** (`diagnostics` struct): battery ADC, solar ADC, ESP32 core temp, charge status

### Key data structures (defined in `weather_v4_lora.ino`)
- `sensorData` — environmental readings sent over LoRa (40 bytes)
- `diagnostics` — hardware health readings sent over LoRa
- `rainfallData` — stored in RTC memory, tracks 24h hourly buckets and 60-min 10-minute buckets
- `sensorStatus` — tracks which sensors initialized successfully

### RTC memory (survives deep sleep)
```cpp
RTC_DATA_ATTR volatile int rainTicks;   // accumulated since last timer wake
RTC_DATA_ATTR struct rainfallData rainfall;
RTC_DATA_ATTR int bootCount;
RTC_DATA_ATTR float maxWindSpeed;       // wind gust since last send
```

### File breakdown
| File | Responsibility |
|------|---------------|
| `weather_v4_lora.ino` | Main sketch, structs, setup/sleep logic, ISR prototypes |
| `defines.h` | Pin assignments, timing constants, device ID, feature flags |
| `sensors.ino` | All sensor read functions (DS18B20, BH1750, BME280, SI1145, ADCs) |
| `wind.ino` | Wind speed ISR (`windTick`), speed calculation, gust tracking |
| `rainfall.ino` | Rain tip ISR (`rainTick`), hourly/60-min accumulation arrays |
| `lora.ino` | `loraSend()` — writes raw struct bytes over LoRa |
| `utility.ino` | Power up/down for sensors and LoRa module |
| `time.ino` | `updateWake()` — calculates next sleep duration aligned to interval |

## Key Configuration (`defines.h`)

- `DEVID 0x11223344` — station ID embedded in every packet; receiver must match
- `UpdateIntervalSeconds 30` — deep sleep duration between timer wakes
- `SEND_FREQUENCY_LORA 5` — send every 5th wake (every 150 seconds)
- `#define heltec` — uncomment to target Heltec ESP32 LoRa v2 devkit (changes I2C pins and LoRa SPI pins)
- `#define BH1750Enable` — comment out to disable lux sensor
- `WIND_TICKS_PER_REVOLUTION 2` — calibration: reed switch closures per anemometer revolution
- `BAND 915E6` — LoRa frequency; comment/uncomment for 433 MHz

## LoRa Protocol

Packets are raw binary struct dumps (no framing beyond the struct). The receiver must use the same struct layout. Sync word is `0x54`, CRC enabled. Packet size varies by type (`sizeof(sensorData)` vs `sizeof(diagnostics)`). The `deviceID` field at offset 0 of both structs lets the receiver distinguish station and packet type.

## Debugging

Enable/disable serial output globally with `#define SerialMonitor` in `defines.h`. Use `MonPrintf()` (wraps `Serial.printf`, gated on `SerialMonitor`) for debug output. `title()` prints banner-style section headers. `sensorStatusToConsole()` prints sensor init results.

Wind and rainfall ISRs both use software debounce: wind ignores ticks < 10ms apart; rain ignores tips < 400ms apart.

## Arduino Core 3.x Compatibility Notes

Several fixes were required to build against ESP32 Arduino core 3.3.8 (ESP-IDF 5.x):

| Issue | Fix | Location |
|-------|-----|----------|
| `ESP_EXT1_WAKEUP_ANY_LOW` undeclared | Renamed to `ESP_EXT1_WAKEUP_ALL_LOW` (enum renamed in core 3.x) | `weather_v4_lora.ino:344` |
| `TEMPERATURE_SENSOR_CLK_SRC_DEFAULT` undeclared | Wrapped `readESPCoreTemp()` with `#if SOC_TEMP_SENSOR_SUPPORTED` — original ESP32 chip does not have the temperature_sensor IDF driver | `sensors.ino` |
| `GPIO_IS_VALID_GPIO` undeclared in OneWire 2.3.8 | Added compatibility shim to the library file: `#if !defined(GPIO_IS_VALID_GPIO) #define GPIO_IS_VALID_GPIO(gpio_num) ((gpio_num >= 0) && (gpio_num < GPIO_NUM_MAX)) #endif` | `/Users/ghost/Documents/Arduino/libraries/OneWire/util/OneWire_direct_gpio.h` (local library file, not in this repo) |
| Heltec library compile errors when BH1750.h resolved from Heltec bundle | Install standalone BH1750 library by Christopher Laws so Arduino does not pull in the Heltec bundle | Library Manager |

The OneWire patch (`OneWire_direct_gpio.h`) is a local edit to the installed library — it is not tracked in this repo. It may need to be reapplied if the OneWire library is updated or reinstalled.
