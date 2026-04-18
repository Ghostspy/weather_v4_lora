//===========================================
// Target build defines
//===========================================
//#define heltec


//===========================================
// Pin Defines
//===========================================
#define WIND_SPD_PIN 14  //reed switch based anemometer count
#define RAIN_PIN     25  //reed switch based tick counter on tip bucket
#define WIND_DIR_PIN 35  //variable voltage divider output based on varying R network with reed switches
#define PR_PIN       15  //photoresistor pin 
#define VBAT_PIN     39  //voltage divider for battery monitor
#define VSOLAR_PIN   36  //voltage divider for solar voltage monitor
#define TEMP_PIN      4  // DS18B20 hooked up to GPIO pin 4
#define LED_BUILTIN   2  //Diagnostics using built-in LED, may be set to 12 for newer boards that do not use devkit sockets
#define LORA_PWR     16
#define SENSOR_PWR   26
#define CHG_STAT     34


#define SEC 1E6          //Multiplier for uS based math
#define WDT_TIMEOUT 30   //watchdog timer

#define DEVID 0x11223344

#define BAND 915E6
//#define BAND 433E6

#define SerialMonitor

//===========================================
//Set how often to wake and read sensors
//===========================================
const int UpdateIntervalSeconds = 30;  //Sleep timer (30s) for my normal operation
#define SEND_FREQUENCY_LORA 5

//===========================================
//BH1750 Enable
//===========================================
#define BH1750Enable

//===========================================
//Anemometer Calibration
//===========================================
//I see 2 switch pulls to GND per revolation. Not sure what others see
#define WIND_TICKS_PER_REVOLUTION 2

//===========================================
// ISR Debounce timings (milliseconds)
//===========================================
#define RAIN_DEBOUNCE_MS    400   // min ms between valid rain gauge tips
#define WIND_DEBOUNCE_MS     10   // min ms between valid anemometer ticks
#define WIND_MAX_SAMPLES     10   // max ISR ticks buffered per wake cycle

//===========================================
// Wind speed calibration
//===========================================
// 2.4 km/h is the anemometer output per revolution per second (manufacturer spec)
#define WIND_SPEED_CALIBRATION 2.4f

//===========================================
// Wind acquisition window
//===========================================
#define WIND_ACQUISITION_MS 5000  // ms to collect wind ticks before reading

//===========================================
// LoRa sync word (must match receiver)
//===========================================
#define LORA_SYNC_WORD 0x54

//===========================================
// Power rail settling times (milliseconds)
//===========================================
#define SENSOR_POWER_SETTLE_MS  500   // ms for sensor rail to stabilize after power-on
#define LORA_POWER_SETTLE_MS    500   // ms for LoRa module to stabilize after power-on

//===========================================
// Full cycle repeats every 2 * SEND_FREQUENCY_LORA timer wakes
// Environment packet sent first half, hardware/diagnostics packet sent second half
//===========================================
#define FULL_SEND_CYCLE (2 * SEND_FREQUENCY_LORA)
