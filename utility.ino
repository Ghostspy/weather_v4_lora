void LoRaPowerUp(void)
{
  //Turn on LoRa
  //let power stabilize before turning on LoRa
  delay(LORA_POWER_SETTLE_MS);
  digitalWrite(LORA_PWR, HIGH);  //TODO: Need these as RTC_IO pins to stay enabled all the time
  delay(LORA_POWER_SETTLE_MS);

#ifdef heltec
  LoRa.setPins(18, 14, 26);
#else
  LoRa.setSPIFrequency(1000000);
  LoRa.setPins(15, 17, 13);
#endif
  if (!LoRa.begin(BAND)) {
    Serial.println("Starting LoRa failed! Rebooting...");
    Serial.flush();
    esp_restart();
  }

  title("LoRa radio online");

  //End LoRa turn on
}


void powerDownAll(void)
{
  digitalWrite(SENSOR_PWR, LOW);
  digitalWrite(LORA_PWR, LOW);
}

void powerUpSensors(void)
{
  delay(SENSOR_POWER_SETTLE_MS);
  digitalWrite(SENSOR_PWR, HIGH);
  delay(SENSOR_POWER_SETTLE_MS);
}

void powerDownSensors(void)
{
  digitalWrite(SENSOR_PWR, LOW);
}
