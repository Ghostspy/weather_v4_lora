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
- **POR (case 0):** First boot — initializes RTC memory (`memset` rainfall to zero, clears `rainTicks`/`bootCount`/`maxWindSpeed`), sets an arbitrary Unix epoch time, schedules first wake in 5 seconds
- **EXT1 (GPIO 25):** Rain tip gauge reed switch closed — increments `rainTicks` inside a `portENTER_CRITICAL_ISR` / `portEXIT_CRITICAL_ISR` guard, goes back to sleep immediately
- **Timer:** Main work cycle — reads sensors, accumulates rainfall data, and conditionally sends LoRa packets

### Send cadence
`bootCount` is stored in RTC memory and incremented each timer wake. The full cycle is `FULL_SEND_CYCLE` (= `2 × SEND_FREQUENCY_LORA` = 10) timer wakes:
- **At wake 0 of the cycle:** reads all sensors, sends an environmental packet (`sensorData` struct)
- **At wake `SEND_FREQUENCY_LORA` of the cycle:** reads system sensors, sends a hardware/diagnostics packet (`diagnostics` struct)

### Key data structures (defined in `weather_v4_lora.ino`)
- `sensorData` — environmental readings sent over LoRa; declared `__attribute__((packed))`
- `diagnostics` — hardware health readings sent over LoRa; declared `__attribute__((packed))`
- `rainfallData` — stored in RTC memory, tracks 24 h hourly buckets and 60-min 10-minute buckets
- `sensorStatus` — tracks which sensors initialized successfully

### RTC memory (survives deep sleep)
```cpp
RTC_DATA_ATTR volatile int rainTicks;   // accumulated since last timer wake
RTC_DATA_ATTR struct rainfallData rainfall;
RTC_DATA_ATTR int bootCount;
RTC_DATA_ATTR float maxWindSpeed;       // wind gust since last send
```

### ISR safety
Both ISRs use dedicated `portMUX_TYPE` mutexes and critical-section guards:
- `rainMux` (defined in `rainfall.ino`) — protects `rainTicks` in `rainTick()` ISR and EXT1 wake handler
- `windMux` (defined in `wind.ino`) — protects `tickTime[]` / `count` in `windTick()` ISR and `calculateWindSpeed()`

The main task snapshots shared state under `portENTER_CRITICAL` before processing, then clears the ISR-side buffer.

### Packed structs and library interop
Both `sensorData` and `diagnostics` are `__attribute__((packed))`. This means their fields **cannot be passed by reference** to library functions — packed fields may be unaligned and the compiler rejects binding them to `T&` parameters.

In `sensors.ino`, both `readBME()` overloads use local float intermediaries when calling `bme.read()`:
```cpp
float pressure, bmeTemp, relHum;
bme.read(pressure, bmeTemp, relHum, ...);
hardware->BMEtemperature = bmeTemp;
```

Any future code that calls a library function with a packed struct field by reference must follow the same pattern.

### File breakdown
| File | Responsibility |
|------|---------------|
| `weather_v4_lora.ino` | Main sketch, structs, setup/sleep logic, ISR prototypes |
| `defines.h` | Pin assignments, timing constants, device ID, feature flags |
| `sensors.ino` | All sensor read functions (DS18B20, BH1750, BME280, SI1145, ADCs) |
| `wind.ino` | Wind speed ISR (`windTick`), speed calculation, gust tracking |
| `rainfall.ino` | Rain tip ISR (`rainTick`), hourly/60-min accumulation arrays |
| `lora.ino` | `loraSend()` — writes raw struct bytes over LoRa |
| `utility.ino` | `powerUpSensors()`, `powerDownSensors()`, `LoRaPowerUp()` |
| `time.ino` | `updateWake()` — calculates next sleep duration aligned to interval |

## Key Configuration (`defines.h`)

- `DEVID 0x11223344` — station ID embedded in every packet; receiver must match
- `UpdateIntervalSeconds 30` — deep sleep duration between timer wakes
- `SEND_FREQUENCY_LORA 5` — send every 5th wake (every 150 seconds)
- `FULL_SEND_CYCLE (2 * SEND_FREQUENCY_LORA)` — full env + diagnostics cycle = 10 wakes
- `LORA_SYNC_WORD 0x54` — LoRa sync word; must match receiver's `SYNC`
- `WIND_SPEED_CALIBRATION 2.4f` — km/h per rev/s (manufacturer spec)
- `WIND_ACQUISITION_MS 5000` — ms to collect wind ticks before computing speed
- `WIND_MAX_SAMPLES 10` — max ISR tick timestamps buffered per wake cycle
- `WIND_DEBOUNCE_MS 10` — min ms between valid anemometer ticks
- `RAIN_DEBOUNCE_MS 400` — min ms between valid rain gauge tips
- `SENSOR_POWER_SETTLE_MS 500` — ms for sensor rail to stabilize after power-on
- `LORA_POWER_SETTLE_MS 500` — ms for LoRa module to stabilize after power-on
- `#define heltec` — uncomment to target Heltec ESP32 LoRa v2 devkit
- `#define BH1750Enable` — comment out to disable lux sensor
- `WIND_TICKS_PER_REVOLUTION 2` — calibration: reed switch closures per revolution
- `BAND 915E6` — LoRa frequency; comment/uncomment for 433 MHz

## Pin Assignments

| Pin | GPIO | Notes |
|-----|------|-------|
| WIND_SPD_PIN | 14 | Anemometer interrupt, falling edge |
| RAIN_PIN | 25 | Rain gauge, EXT1 wake + interrupt |
| WIND_DIR_PIN | 35 | Wind vane ADC |
| VBAT_PIN | 39 | Battery ADC |
| VSOLAR_PIN | 36 | Solar ADC |
| TEMP_PIN | 4 | DS18B20 OneWire |
| LORA_PWR | 16 | LoRa power control |
| SENSOR_PWR | 26 | Sensor rail power control |
| CHG_STAT | 34 | LiPo charger status (active low) |

## LoRa Protocol

Packets are raw binary struct dumps (no framing beyond the struct). Both structs are `__attribute__((packed))` — the receiver must use the same packed struct layout. Sync word is `0x54`, CRC enabled. Packet size varies by type (`sizeof(sensorData)` vs `sizeof(diagnostics)`). The `deviceID` field at offset 0 of both structs lets the receiver distinguish station and packet type.

## Debugging

Enable/disable serial output globally with `#define SerialMonitor` in `defines.h`. Use `MonPrintf()` (wraps `Serial.printf`, gated on `SerialMonitor`) for debug output. `title()` prints banner-style section headers. `sensorStatusToConsole()` prints sensor init results.

Wind and rainfall ISRs both use software debounce: wind ignores ticks < `WIND_DEBOUNCE_MS` apart; rain ignores tips < `RAIN_DEBOUNCE_MS` apart.

## Arduino Core 3.x Compatibility Notes

Several fixes were required to build against ESP32 Arduino core 3.3.8 (ESP-IDF 5.x):

| Issue | Fix | Location |
|-------|-----|----------|
| `ESP_EXT1_WAKEUP_ANY_LOW` undeclared | Renamed to `ESP_EXT1_WAKEUP_ALL_LOW` (enum renamed in core 3.x) | `weather_v4_lora.ino` |
| `TEMPERATURE_SENSOR_CLK_SRC_DEFAULT` undeclared | Wrapped `readESPCoreTemp()` with `#if SOC_TEMP_SENSOR_SUPPORTED` — original ESP32 chip does not have the temperature_sensor IDF driver | `sensors.ino` |
| `GPIO_IS_VALID_GPIO` undeclared in OneWire 2.3.8 | Added compatibility shim to the library file: `#if !defined(GPIO_IS_VALID_GPIO) #define GPIO_IS_VALID_GPIO(gpio_num) ((gpio_num >= 0) && (gpio_num < GPIO_NUM_MAX)) #endif` | `/Users/ghost/Documents/Arduino/libraries/OneWire/util/OneWire_direct_gpio.h` (local library file, not in this repo) |
| Heltec library compile errors when BH1750.h resolved from Heltec bundle | Install standalone BH1750 library by Christopher Laws so Arduino does not pull in the Heltec bundle | Library Manager |
| `bme.read()` cannot bind packed struct fields to `float&` | Use local float intermediaries in both `readBME()` overloads before assigning to struct fields | `sensors.ino` |

The OneWire patch (`OneWire_direct_gpio.h`) is a local edit to the installed library — it is not tracked in this repo. It may need to be reapplied if the OneWire library is updated or reinstalled.
