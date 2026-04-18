OneWire oneWire(TEMP_PIN);
DallasTemperature temperatureSensor(&oneWire);

#include "driver/temperature_sensor.h"


//===========================================
// sensorEnable: Initialize i2c and 1w sensors
//===========================================
void sensorEnable(void) {
  status.temperature = 1;
#ifdef heltec
  Wire.begin(4, 15);
#else
  Wire.begin();
#endif
  status.bme = bme.begin();
  status.lightMeter = lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x23);
  if (!status.lightMeter) {
    status.lightMeter = lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x5C);
  }
  status.uv = uv.begin();

  temperatureSensor.begin();  //returns void - cannot directly check
}

//=======================================================
//  readSensors: Read all sensors and battery voltage
//=======================================================
void readSensors(struct sensorData *environment) {
  copyRainTicks24h(environment);
  copyRainTicks60m(environment);
  readWindSpeed(environment);
  readWindDirectionADC(environment);
  readTemperature(environment);
  readLux(environment);
  readUV(environment);
  readBME(environment);
}

//=======================================================
//  readSystemSensors: Hardware health and diagnostics
//=======================================================
void readSystemSensors(struct diagnostics *hardware) {
  readBME(hardware);
  readBatteryADC(hardware);
  readSolarADC(hardware);
  readESPCoreTemp(hardware);
  readChargeStatus(hardware);
}
//=======================================================
//  readTemperature: Read 1W DS1820B
//=======================================================
void readTemperature(struct sensorData *environment) {
  MonPrintf("Requesting temperatures...\n");
  temperatureSensor.requestTemperatures();
  environment->temperatureC = temperatureSensor.getTempCByIndex(0);

  // Check if reading was successful
  if (environment->temperatureC != DEVICE_DISCONNECTED_C) {
    MonPrintf("Temperature for the device 1 (index 0) is: %5.1f C\n", environment->temperatureC);
  } else {
    MonPrintf("Error: Could not read temperature data\n");
    environment->temperatureC = -40;
  }
}

//=======================================================
//  readSolarADC: read analog volatage divider value
//=======================================================
void readSolarADC(struct diagnostics *hardware) {
  hardware->solarADC = analogRead(VSOLAR_PIN);
  MonPrintf("Solar ADC :%i\n", hardware->solarADC);
}

//=======================================================
//  readBattery: read analog volatage divider value
//=======================================================
void readBatteryADC(struct diagnostics *hardware)
//TODO: Rethink the low voltage warning indicator as the calibration is being moved to the LoRa receiver
{
  hardware->batteryADC = analogRead(VBAT_PIN);
  MonPrintf("Battery ADC :%i\n", hardware->batteryADC);
}

//=======================================================
//  readLux: LUX sensor read
//=======================================================
void readLux(struct sensorData *environment) {
#ifdef BH1750Enable
  if (status.lightMeter) {
    environment->lux = lightMeter.readLightLevel();
  } else {
    environment->lux = -1;
  }
#else
  environment->lux = -3;
#endif
  MonPrintf("LUX value: %6.2f\n", environment->lux);
}

//=======================================================
//  readBME: BME sensor read
//=======================================================
void readBME(struct diagnostics *hardware) {
  float pressure, bmeTemp, relHum;
  if (status.bme) {
    bme.read(pressure, bmeTemp, relHum, BME280::TempUnit_Celsius, BME280::PresUnit_Pa);
    hardware->BMEtemperature = bmeTemp;
  } else {
    hardware->BMEtemperature = -100;
  }
  MonPrintf("BME case temperature: %6.2f\n", hardware->BMEtemperature);
}

//=======================================================
//  readBME: BME sensor read
//=======================================================
void readBME(struct sensorData *environment) {
  float pressure, case_temperature, humidity;
  if (status.bme) {
    bme.read(pressure, case_temperature, humidity, BME280::TempUnit_Celsius, BME280::PresUnit_Pa);
    environment->barometricPressure = pressure;
    environment->humidity = humidity;
  } else {
    //set to insane values
    environment->barometricPressure = -100;
    environment->humidity = -100;
  }
  MonPrintf("BME barometric pressure: %6.2f  BME humidity: %6.2f\n", environment->barometricPressure, environment->humidity);
}

//=======================================================
//  readUV: get implied uv sensor value
//=======================================================
void readUV(struct sensorData *environment) {
  if (status.uv) {
    environment->UVIndex = (float)uv.readUV() / 100;
  } else {
    environment->UVIndex = -1;
  }
  MonPrintf("UV Index: %f\n", environment->UVIndex);
  MonPrintf("Vis: %i\n", uv.readVisible());
  MonPrintf("IR: %i\n", uv.readIR());
}

//===========================================
// readESPCoreTemp: ESP32 internal temperature sensor
// Uses the ESP-IDF 5.x temperature_sensor driver (Arduino 3.x).
// The sensor has ~1-2 °C accuracy and measures the die, not ambient.
//===========================================
void readESPCoreTemp(struct diagnostics *hardware) {
#if SOC_TEMP_SENSOR_SUPPORTED
  temperature_sensor_handle_t tempHandle = NULL;
  temperature_sensor_config_t tempConfig = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
  temperature_sensor_install(&tempConfig, &tempHandle);
  temperature_sensor_enable(tempHandle);
  float coreC = 0;
  temperature_sensor_get_celsius(tempHandle, &coreC);
  temperature_sensor_disable(tempHandle);
  temperature_sensor_uninstall(tempHandle);
  hardware->coreC = (int)coreC;
  MonPrintf("Core temp: %.1f C\n", coreC);
#else
  // Original ESP32 does not include the temperature_sensor IDF driver
  hardware->coreC = 0;
#endif
}

//===========================================
// readChargeStatus: charge status is active low
//===========================================
void readChargeStatus(struct diagnostics *hardware) {
  hardware->chargeStatusB = digitalRead(CHG_STAT);
  MonPrintf("Charger Status: %i\n", hardware->chargeStatusB);
}


//===========================================
// sensorStatusToConsole: Output .begin return values
//===========================================
void sensorStatusToConsole(void) {
  MonPrintf("----- Sensor Statuses -----\n");
  MonPrintf("BME status:         %i\n", status.bme);
  MonPrintf("UV status:          %i\n", status.uv);
  MonPrintf("lightMeter status:  %i\n", status.lightMeter);
  MonPrintf("temperature status: %i\n\n", status.temperature);
}
