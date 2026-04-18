//=======================================================
// Variables used in calculating the windspeed (from ISR)
//=======================================================
volatile unsigned long timeSinceLastTick = 0;
volatile unsigned long lastTick = 0;
volatile unsigned long tickTime[WIND_MAX_SAMPLES] = { 0 };
volatile int count = 0;

//=======================================================
//  calculateWindSpeed: shared helper — averages ISR tick deltas.
//  Skips tickTime[0] (measured from cold lastTick=0, unreliable).
//=======================================================
static float calculateWindSpeed(void) {
  long msTotal = 0;
  int samples = 0;
  if (count) {
    MonPrintf("Count: %i\n", count);
    for (int position = 1; position < count && position < WIND_MAX_SAMPLES; position++) {
      if (tickTime[position]) {
        msTotal += tickTime[position];
        samples++;
      }
    }
  } else {
    MonPrintf("No count values\n");
  }
  if (msTotal > 0 && samples > 0) {
    return (WIND_SPEED_CALIBRATION * 1000.0f / ((float)msTotal / samples)) / WIND_TICKS_PER_REVOLUTION;
  }
  MonPrintf("No Wind data\n");
  return 0.0f;
}

//========================================================================
//  readWindSpeed: Look at ISR data to see if we have wind data to average
//========================================================================
void readWindSpeed(struct sensorData *environment) {
  float windSpeed = calculateWindSpeed();
  MonPrintf("WindSpeed: %f\n", windSpeed);
  MonPrintf("maxWindSpeed: %f\n", maxWindSpeed);
  environment->windSpeed = windSpeed;
  environment->windSpeedMax = maxWindSpeed;
  maxWindSpeed = 0;
}

//=======================================================
//  readWindDirection: Read ADC to find wind direction
//=======================================================
void readWindDirectionADC(struct sensorData *environment) {
  environment->windDirectionADC = analogRead(WIND_DIR_PIN);
  MonPrintf("WindDirADC: %i\n", environment->windDirectionADC);
}

//=======================================================
//  checkMaxWind: Update max windspeed if current exceeds stored gust
//=======================================================
void checkMaxWind(void) {
  float windSpeed = calculateWindSpeed();
  if (windSpeed > maxWindSpeed) {
    maxWindSpeed = windSpeed;
  }
  MonPrintf("Current wind speed %f\n", windSpeed);
  MonPrintf("Max wind speed %f\n", maxWindSpeed);
}

//=======================================================
//  windTick: ISR to capture wind speed relay closure
//=======================================================
void IRAM_ATTR windTick(void) {
  timeSinceLastTick = millis() - lastTick;
  //software debounce attempt
  //record up to 10 ticks from anemometer
  if (timeSinceLastTick > WIND_DEBOUNCE_MS && count < WIND_MAX_SAMPLES) {
    lastTick = millis();
    tickTime[count] = timeSinceLastTick;
    count++;
    //digitalWrite(LED_BUILTIN, HIGH);
  }
}
