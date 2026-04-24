#include <Arduino.h>

constexpr uint8_t ADC_PIN = 2;
constexpr uint32_t TEST_DURATION_MS = 10000;

void setup()
{
  Serial.begin(115200);
  delay(1000);

  analogReadResolution(12);
  analogSetPinAttenuation(ADC_PIN, ADC_11db);

  Serial.println("ADC max-speed benchmark");
}

void loop()
{
  uint32_t startMs = millis();
  uint32_t startUs = micros();

  uint32_t samples = 0;
  volatile int lastValue = 0;

  while ((millis() - startMs) < TEST_DURATION_MS)
  {
    lastValue = analogRead(ADC_PIN);
    samples++;
  }

  uint32_t elapsedUs = micros() - startUs; // tempo effettivo trascorso durante il test, in microsecondi
  float elapsedS = elapsedUs / 1000000.0f; // tempo effettivo trascorso durante il test, in secondi

  float sampleRate = samples / elapsedS; // campioni al secondo, calcolato come numero di campioni diviso tempo effettivo trascorso

  Serial.println();
  Serial.print("samples: ");
  Serial.println(samples);

  Serial.print("elapsed_s: ");
  Serial.println(elapsedS, 6);

  Serial.print("estimated_sample_rate_Hz: ");
  Serial.println(sampleRate, 2);

  Serial.print("last_value: ");
  Serial.println(lastValue);

  delay(3000);
}