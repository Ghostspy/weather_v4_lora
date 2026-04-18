//LoRa baseed weather station
//Hardware design by Debasish Dutta - opengreenenergy@gmail.com
//Software design by James Hughes - jhughes1010@gmail.com

/* History
   0.9.0 10-2-22 Initial development for Heltec ESP32 LoRa v2 devkit

   1.0.0 11-02-22 First release
                  Much fine tuning to do, but interested in getting feedback from users

   1.0.1 11-06-22 Minor code changes
                  Passing void ptr to universal LoRaSend function
                  Refactor LoRa powerup and powerdown routines (utility.ino)
                  Better positioning of LoRa power up (after sensor data is acquired)
                  Corrected error in 60min rainfall routines (only using 5 slots, not 6)
                    ***Still not perfect on 60 minute cutoff

   1.0.2 11-11-22 More sensors online
                  Added chargeStatus to hardware structure
                  Removed VBAT (will be calculated in receiver)

   1.0.3 11-18-22 #heltec now works again for heltec dev boards
                  WindDirADC value now being sent
                  Metric wind speed being sent, was sending imperial value

   1.1.0 11-23-22 Wind gust measurement added. Wakes more frequently without sending data.
                  New data struct member added on sensors, now 40 bytes

   1.1.1 02-05-23 Turned on CRC at the LoRa hardware level to help increase packet integrity. 
                  Added ID int value to the structure to better isolate the data. Station ID and RX ID MUST MATCH. I played around with syncWord()                                
                  , but not getting desired results.
*/

// Hardware build target: ESP32
#define VERSION "1.1.1"

#ifdef heltec
#include "heltec.h"
#else
#include <LoRa.h>
#include <SPI.h>
#endif

#include "defines.h"
#include <soc/soc.h>
#include <soc/rtc_cntl_reg.h>
#include <esp_task_wdt.h>
#include <esp_system.h>
#include <driver/rtc_io.h>
#include <sys/time.h>
#include <time.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <Wire.h>
#include <BH1750.h>
#include <BME280I2C.h>
#include <Adafruit_SI1145.h>

//===========================================
// Weather-environment structure
//===========================================
struct __attribute__((packed)) sensorData {
  int deviceID;
  int windDirectionADC;
  int rainTicks24h;
  int rainTicks60m;
  float temperatureC;
  float windSpeed;
  float windSpeedMax;
  float barometricPressure;
  float humidity;
  float UVIndex;
  float lux;
};

//===========================================
// Station hardware structure
//===========================================
struct __attribute__((packed)) diagnostics {
  int deviceID;
  float BMEtemperature;
  int batteryADC;
  int solarADC;
  int coreC;
  int bootCount;
  bool chargeStatusB;
};

//===========================================
// Sensor initilization structure
//===========================================
struct sensorStatus {
  int uv;
  int bme;
  int lightMeter;
  int temperature;
};

//===========================================
// rainfallData structure
//===========================================
struct rainfallData {
  unsigned int intervalRainfall;
  unsigned int hourlyRainfall[24];
  unsigned int current60MinRainfall[6];
  unsigned int hourlyCarryover;
  unsigned int priorHour;
  unsigned int minuteCarryover;
  unsigned int priorMinute;
};

//===========================================
// RTC Memory storage
//===========================================
RTC_DATA_ATTR volatile int rainTicks = 0;
RTC_DATA_ATTR struct rainfallData rainfall;
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR float maxWindSpeed = 0.0f;


//===========================================
// ISR Prototypes
//===========================================
void IRAM_ATTR rainTick(void);
void IRAM_ATTR windTick(void);

//===========================================
// ISR mutex declarations (defined in rainfall.ino / wind.ino)
//===========================================
extern portMUX_TYPE rainMux;
extern portMUX_TYPE windMux;

//===========================================
// Global instantiation
//===========================================
BH1750 lightMeter(0x23);
BME280I2C bme;
Adafruit_SI1145 uv = Adafruit_SI1145();
struct sensorStatus status;
time_t now;
time_t nextUpdate;
struct tm timeinfo;
//long rssi = 0;

//===========================================
// Setup
//===========================================
void setup() {
  esp_sleep_wakeup_cause_t wakeup_reason;
  struct sensorData environment = {};
  struct diagnostics hardware = {};
  environment.deviceID = DEVID;
  hardware.deviceID = DEVID;

  struct timeval tv;

  void *LoRaPacket;
  int LoRaPacketSize;


  Serial.begin(115200);
  printTitle();
  title("Boot count: %i", bootCount);
  Serial.println(environment.deviceID, HEX);

  //Enable WDT for any lock-up events (ESP32 Arduino 3.x / ESP-IDF 5.x API)
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = WDT_TIMEOUT * 1000,
    .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
    .trigger_panic = true,
  };
  esp_task_wdt_reconfigure(&wdt_config);
  esp_task_wdt_add(NULL);

  //time testing
  time(&now);
  localtime_r(&now, &timeinfo);
  updateWake();

  Serial.print("The current date/time is ");
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
  //-- end time testing

  //set hardware pins
  pinMode(WIND_SPD_PIN, INPUT);
  pinMode(RAIN_PIN, INPUT);
  pinMode(CHG_STAT, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(SENSOR_PWR, OUTPUT);
  pinMode(LORA_PWR, OUTPUT);

  digitalWrite(LED_BUILTIN, LOW);



  BlinkLED(1);





  //get wake up reason
  /*
    POR - First boot
    TIMER - Periodic send of sensor data on LoRa
    Interrupt - Count tick in rain gauge
  */
  wakeup_reason = esp_sleep_get_wakeup_cause();
  MonPrintf("\n\nWakeup reason: %d\n", wakeup_reason);
  //MonPrintf("rainTicks: %i\n", rainTicks);
  switch (wakeup_reason) {
    //Power on reset
    case 0:
      // Explicitly initialize RTC memory on first boot — not guaranteed zero by hardware
      memset(&rainfall, 0, sizeof(rainfall));
      rainTicks = 0;
      bootCount = 0;
      maxWindSpeed = 0.0f;
      // Seed clock from compile time so rainfall hour/minute buckets start
      // at a sane time. No network available for NTP; this drifts but is
      // accurate to within a few seconds of when the sketch was built.
      tv.tv_sec = compileTime();
      tv.tv_usec = 0;
      settimeofday(&tv, NULL);
      //default to wake 5 sec after POR
      nextUpdate = 5;
      break;

    //Rain Tip Gauge
    case ESP_SLEEP_WAKEUP_EXT1:
      MonPrintf("Wakeup caused by external signal using RTC_IO\n");
      // Guard for consistency — no ISR is attached yet at this point,
      // but rainTicks is shared with main-loop timer wake path.
      portENTER_CRITICAL(&rainMux);
      rainTicks++;
      portEXIT_CRITICAL(&rainMux);
      break;

    //Timer
    case ESP_SLEEP_WAKEUP_TIMER:
      title("Wakeup caused by timer");
      powerUpSensors();



      //Rainfall interrupt pin set up
      //delay(100);  //possible settling time on pin to charge
      attachInterrupt(digitalPinToInterrupt(RAIN_PIN), rainTick, FALLING);
      attachInterrupt(digitalPinToInterrupt(WIND_SPD_PIN), windTick, FALLING);
      //give 5 seconds to aquire wind speed data
      delay(WIND_ACQUISITION_MS);
      //TODO: set TOD on interval
      checkMaxWind();


      if (bootCount % FULL_SEND_CYCLE == 0) {
        title("Sending sensor data");


        //read sensors
        sensorEnable();
        sensorStatusToConsole();

        //update rainfall — atomically snapshot and clear rainTicks
        int localRainTicks;
        portENTER_CRITICAL(&rainMux);
        localRainTicks = rainTicks;
        rainTicks = 0;
        portEXIT_CRITICAL(&rainMux);

        addTipsToMinute(localRainTicks);
        clearRainfallMinute(timeinfo.tm_min + 10);

        addTipsToHour(localRainTicks);
        clearRainfallHour(timeinfo.tm_hour + 1);

        //environmental sensor data send
        readSensors(&environment);

        LoRaPacket = &environment;
        LoRaPacketSize = sizeof(environment);
        PrintEnvironment(environment);
        powerDownSensors();
        LoRaPowerUp();
        BlinkLED(2);
        loraSend(LoRaPacket, LoRaPacketSize);
        //Power down peripherals
        LoRa.end();
        powerDownAll();
      } else if (bootCount % FULL_SEND_CYCLE == SEND_FREQUENCY_LORA) {
        title("Sending hardware data");
        sensorEnable();
        sensorStatusToConsole();
        //system (battery levels, ESP32 core temp, case temp, etc) send
        readSystemSensors(&hardware);
        hardware.bootCount = bootCount;

        LoRaPacket = &hardware;
        LoRaPacketSize = sizeof(hardware);
        Serial.printf("DEVID: %x\n", hardware.deviceID);
        powerDownSensors();
        LoRaPowerUp();
        BlinkLED(2);
        loraSend(LoRaPacket, LoRaPacketSize);
        //Power down peripherals
        LoRa.end();
        powerDownAll();
      }
      bootCount++;
      break;
  }

  //preparing for sleep
  BlinkLED(1);
  sleepyTime(nextUpdate);
}

//===================================================
// loop: these are not the droids you are looking for
//===================================================
void loop() {
  //no loop code
}

//===================================================
// printTitle
//===================================================
void printTitle(void) {
  Serial.printf("\n\nWeather station v4\n");
  Serial.printf("Version %s\n\n", VERSION);
}

//===========================================
// sleepyTime: prepare for sleep and set
// timer and EXT0 WAKE events
//===========================================
void sleepyTime(long nextUpdate) {
  int elapsedTime;
  Serial.println("Going to sleep now...");

  // EXT1 wakeup works on both ESP32 and ESP32-S3 (EXT0 was ESP32-only).
  // Wakes on LOW — matches the rain gauge reed switch pulling to GND.
  // NOTE: on ESP32-S3, RAIN_PIN must be in the range 0-21 (RTC-capable GPIOs).
  //       GPIO 25 does not exist on ESP32-S3; reassign RAIN_PIN in defines.h
  //       if targeting S3 hardware.
  esp_sleep_enable_ext1_wakeup(1ULL << RAIN_PIN, ESP_EXT1_WAKEUP_ALL_LOW);
  elapsedTime = (int)millis() / 1000;

  //subtract elapsed time to try to maintain interval
  nextUpdate -= elapsedTime;
  if (nextUpdate < 3) {
    nextUpdate = 3;
  }
  Serial.printf("Elapsed time: %i seconds\n", elapsedTime);
  Serial.printf("Waking in %i seconds\n", nextUpdate);
  Serial.flush();
  esp_sleep_enable_timer_wakeup(nextUpdate * SEC);
  esp_deep_sleep_start();
}

//===========================================
// MonPrintf: diagnostic printf to terminal
//===========================================
void MonPrintf(const char *format, ...) {
  char buffer[200];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
#ifdef SerialMonitor
  Serial.printf("%s", buffer);
#endif
}

//===========================================
// BlinkLED: Blink BUILTIN x times
//===========================================
void BlinkLED(int count) {
  int x;
  //if reason code =0, then set count =1 (just so I can see something)
  if (!count) {
    count = 1;
  }
  for (x = 0; x < count; x++) {
    //LED ON
    digitalWrite(LED_BUILTIN, HIGH);
    delay(150);
    //LED OFF
    digitalWrite(LED_BUILTIN, LOW);
    delay(350);
  }
}

//===========================================
// PrintEnvironment:
//===========================================
void PrintEnvironment(struct sensorData environment) {
  Serial.printf("Temperature: %f\n", environment.temperatureC);
  Serial.printf("Wind speed: %f\n", environment.windSpeed);
  //TODO:  Serial.printf("Wind direction: %f\n", environment->windDirection);
  Serial.printf("barometer: %f\n", environment.barometricPressure);
  Serial.printf("Humidity: %f\n", environment.humidity);
  Serial.printf("UV Index: %f\n", environment.UVIndex);
  Serial.printf("Lux: %f\n", environment.lux);
  Serial.printf("DEVID: %x\n", environment.deviceID);
}

//===========================================
// compileTime: parse __DATE__ / __TIME__ into a Unix timestamp.
// Used on POR to seed the software clock without NTP.
//===========================================
time_t compileTime() {
  const char* months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
  char monthStr[4];
  int day, year, hour, minute, second;

  sscanf(__DATE__, "%3s %d %d", monthStr, &day, &year);
  sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second);

  int month = 0;
  for (int i = 0; i < 12; i++) {
    if (strncmp(monthStr, months[i], 3) == 0) { month = i; break; }
  }

  struct tm t = {};
  t.tm_year  = year - 1900;
  t.tm_mon   = month;
  t.tm_mday  = day;
  t.tm_hour  = hour;
  t.tm_min   = minute;
  t.tm_sec   = second;
  t.tm_isdst = -1;
  return mktime(&t);
}

//===========================================
// Title: banner to terminal
//===========================================
void title(const char *format, ...) {
  char buffer[200];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
#ifdef SerialMonitor
  Serial.printf("==============================================\n");
  Serial.printf("%s\n", buffer);
  Serial.printf("==============================================\n");
#endif
}
