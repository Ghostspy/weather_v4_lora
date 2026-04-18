# weather_v4_lora

LoRa-based ESP32 weather station. Reads a suite of environmental sensors and
transmits data wirelessly via LoRa. Designed for low-power, solar-charged
unattended operation using deep sleep between readings.

Hardware design by Debasish Dutta. Software by James Hughes.

---

## Requirements

- **ESP32 Arduino core 3.x** (ESP-IDF 5.x) — tested on **3.3.8**; earlier 2.x
  cores are not supported due to updated WDT and internal temperature sensor APIs
- Arduino IDE

---

## Hardware

### Pin Assignments

| Pin | GPIO | Purpose |
|-----|------|---------|
| WIND_SPD_PIN | 14 | Anemometer reed switch (interrupt, falling edge) |
| RAIN_PIN | 25 | Tip-bucket rain gauge (EXT1 wake + interrupt, falling edge) |
| WIND_DIR_PIN | 35 | Wind vane — variable resistor network ADC |
| VBAT_PIN | 39 | Battery voltage divider ADC |
| VSOLAR_PIN | 36 | Solar panel voltage divider ADC |
| TEMP_PIN | 4 | DS18B20 OneWire temperature sensor |
| PR_PIN | 15 | Photoresistor (reserved, not currently read) |
| LORA_PWR | 16 | LoRa module power control (active high) |
| SENSOR_PWR | 26 | Sensor rail power control (active high) |
| CHG_STAT | 34 | LiPo charger status input (active low) |
| LED_BUILTIN | 2 | Diagnostic LED (may need to change to 12 on some boards) |

### LoRa SPI (non-Heltec)

| Function | GPIO |
|----------|------|
| CS | 15 |
| RESET | 17 |
| IRQ | 13 |

SPI clock is set to 1 MHz (`LoRa.setSPIFrequency(1000000)`).

---

## Sensors

| Sensor | Interface | Measures |
|--------|-----------|---------|
| DS18B20 | OneWire (GPIO 4) | Outdoor temperature (°C) |
| BME280 | I2C | Barometric pressure (Pa), humidity (%), case temperature |
| BH1750 | I2C (0x23) | Ambient light (lux) |
| SI1145 | I2C | UV index, visible light, IR |
| Reed switch anemometer | GPIO 14 interrupt | Wind speed (km/h) |
| Resistor-network wind vane | GPIO 35 ADC | Wind direction (raw ADC) |
| Tip-bucket rain gauge | GPIO 25 EXT1 | Rainfall (tick counts) |
| Battery divider | GPIO 39 ADC | Battery level (raw ADC) |
| Solar divider | GPIO 36 ADC | Solar input (raw ADC) |
| ESP32 internal | temperature_sensor driver | Die temperature (°C) |

Sensors are powered through a switched rail (SENSOR_PWR) that is brought up
only during a timer-wake reading cycle, then shut off before sleep.

---

## Required Libraries

Install all of the following via the Arduino Library Manager:

- **LoRa** (by Sandeep Mistry)
- **DallasTemperature**
- **OneWire**
- **BH1750** (by Christopher Laws) — **important:** this must be the standalone
  library. Do *not* rely on the BH1750 copy bundled inside the Heltec ESP32
  Dev-Boards library; if no standalone BH1750 is installed, Arduino IDE will pull
  in the entire Heltec bundle to satisfy `#include <BH1750.h>` and compilation
  will fail with missing Heltec-specific defines.
- **BME280** (BME280I2C — by Tyler Glenn)
- **Adafruit SI1145**

If targeting a Heltec ESP32 LoRa v2 devkit, also install:

- **Heltec ESP32 Dev-Boards**

> Do not install the Heltec ESP32 Dev-Boards library unless you are targeting
> Heltec hardware (`#define heltec` in `defines.h`). Having it installed without
> the standalone BH1750 library will cause compilation failures.

---

## Configuration (`defines.h`)

| Define | Default | Description |
|--------|---------|-------------|
| `DEVID` | `0x11223344` | Station ID in every packet — must match the receiver |
| `UpdateIntervalSeconds` | `30` | Deep-sleep duration between timer wakes |
| `SEND_FREQUENCY_LORA` | `5` | Transmit every N timer wakes (every 150 s at 30 s interval) |
| `FULL_SEND_CYCLE` | `2 × SEND_FREQUENCY_LORA` | Full env+diagnostics cycle length in timer wakes |
| `BAND` | `915E6` | LoRa frequency — change to `433E6` for non-US regions |
| `LORA_SYNC_WORD` | `0x54` | LoRa sync word — must match the receiver |
| `WIND_TICKS_PER_REVOLUTION` | `2` | Reed switch closures per anemometer revolution |
| `WIND_SPEED_CALIBRATION` | `2.4` | km/h per rev/s (manufacturer spec) |
| `WIND_ACQUISITION_MS` | `5000` | ms to collect wind ticks before computing speed |
| `WIND_MAX_SAMPLES` | `10` | Max ISR tick timestamps buffered per wake cycle |
| `WIND_DEBOUNCE_MS` | `10` | Minimum ms between valid anemometer ticks |
| `RAIN_DEBOUNCE_MS` | `400` | Minimum ms between valid rain gauge tips |
| `SENSOR_POWER_SETTLE_MS` | `500` | ms for sensor rail to stabilize after power-on |
| `LORA_POWER_SETTLE_MS` | `500` | ms for LoRa module to stabilize after power-on |
| `WDT_TIMEOUT` | `30` | Watchdog timeout in seconds |
| `BH1750Enable` | defined | Comment out to disable the lux sensor |
| `heltec` | undefined | Uncomment to target Heltec ESP32 LoRa v2 devkit |
| `SerialMonitor` | defined | Comment out to silence all serial debug output |

---

## Building and Flashing

1. Install the ESP32 Arduino core **3.3.8** via the Arduino Boards Manager
2. Install all required libraries listed above
3. Open `weather_v4_lora.ino` in the Arduino IDE
4. Select your ESP32 board and port
5. Compile and upload
6. Open the Serial Monitor at **115200 baud** to view diagnostic output

### Known library compatibility issues (core 3.3.8)

- **OneWire 2.3.8** — `GPIO_IS_VALID_GPIO` was removed from ESP-IDF 5.x. A
  compatibility shim must be added to the installed library file
  `OneWire/util/OneWire_direct_gpio.h` in the ESP32 section:
  ```cpp
  #if !defined(GPIO_IS_VALID_GPIO)
  #define GPIO_IS_VALID_GPIO(gpio_num) ((gpio_num >= 0) && (gpio_num < GPIO_NUM_MAX))
  #endif
  ```
  This edit is in your local library installation and must be reapplied if
  OneWire is updated or reinstalled.

---

## How It Works

The station runs entirely in `setup()` — `loop()` is empty. All persistent
state is stored in RTC memory so it survives deep sleep.

### Wake cycle

```
POR  ──► set clock from compile time ──► sleep 5 s
          │
Timer ────► power up sensors
          ├─ attach rain + wind interrupts
          ├─ wait WIND_ACQUISITION_MS (5 s) for wind data
          ├─ snapshot + clear rainTicks atomically
          ├─ accumulate tips into 24-h and 60-min rolling arrays
          ├─ check/update wind gust (RTC memory)
          ├─ if bootCount % FULL_SEND_CYCLE == 0
          │    ──► read all sensors ──► send sensorData packet
          ├─ elif bootCount % FULL_SEND_CYCLE == SEND_FREQUENCY_LORA
          │    ──► read system sensors ──► send diagnostics packet
          ├─ power down sensors + LoRa
          └─ sleep UpdateIntervalSeconds
          │
EXT1 ─────► rainTicks++ (ISR-safe critical section) ──► sleep immediately
```

### ISR safety

Both the rain gauge (`rainTick`) and wind speed (`windTick`) ISRs use
`portENTER_CRITICAL_ISR` / `portEXIT_CRITICAL_ISR` with dedicated
`portMUX_TYPE` mutexes (`rainMux`, `windMux`). The main task snapshots shared
state under `portENTER_CRITICAL` before processing.

### LoRa packets

Two struct types are transmitted on alternating send events. Both structs are
declared `__attribute__((packed))` so their wire layout is identical on
transmitter and receiver regardless of compiler alignment.

**`sensorData`** — environmental reading:
`deviceID`, wind direction ADC, rain ticks (24 h), rain ticks (60 min),
temperature (°C), wind speed, wind speed max (gust), barometric pressure (Pa),
humidity (%), UV index, lux

**`diagnostics`** — hardware health:
`deviceID`, BME280 case temperature, battery ADC, solar ADC, ESP32 die
temperature, boot count, charge status

Sync word `0x54`, hardware CRC enabled. The `deviceID` field at offset 0 of
both structs lets the receiver identify the station and distinguish packet type.

### Rainfall accumulation

Rain ticks are accumulated in two rolling arrays stored in RTC memory:
- **24-hour** — 24 hourly buckets; oldest hour is zeroed each cycle
- **60-minute** — 6 ten-minute buckets; oldest bucket is zeroed each cycle

The clock is seeded from the compile-time timestamp on first boot. There is no
NTP — the clock drifts but bucket boundaries remain consistent within a
deployment.

### Wind speed

The anemometer ISR records timestamps of up to `WIND_MAX_SAMPLES` (10) reed-
switch closures per wake cycle. Speed is calculated from the average interval
between recorded ticks:

```
speed (km/h) = WIND_SPEED_CALIBRATION × 1000 / avg_ms_per_tick / WIND_TICKS_PER_REVOLUTION
```

Peak gust is tracked in RTC memory (`maxWindSpeed`) and included in the next
`sensorData` packet, then reset to 0.
