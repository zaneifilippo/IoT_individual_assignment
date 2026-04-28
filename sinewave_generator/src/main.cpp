#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_INA219.h>
#include <math.h>

constexpr uint8_t DAC_PIN = 25;

// I2C AZ-Delivery / ESP32 classica
constexpr uint8_t I2C_SDA = 21;
constexpr uint8_t I2C_SCL = 22;

// Generatore onda
constexpr uint16_t FS_WAVE = 500;
constexpr uint16_t TABLE_SIZE = 1000;
constexpr uint32_t TS_WAVE_US = 1000000UL / FS_WAVE;

// Logger INA219
constexpr uint32_t INA_PERIOD_MS = 100;

uint8_t waveTable[TABLE_SIZE];
uint16_t waveIdx = 0;
uint32_t nextWaveUs = 0;
uint32_t nextInaMs = 0;
uint32_t lastInaUs = 0;

float energy_mJ = 0.0f;

Adafruit_INA219 ina219;
bool inaOk = false;

void buildWaveTable()
{
  for (uint16_t i = 0; i < TABLE_SIZE; i++)
  {
    float t = i / (float)FS_WAVE;

    float y = 2.0f * sinf(2.0f * PI * 0.5f * t) + 1.0f * sinf(2.0f * PI * 2.0f * t);

    int dacValue = (int)lroundf(128.0f + 42.0f * y);

    if (dacValue < 0)
      dacValue = 0;
    if (dacValue > 255)
      dacValue = 255;

    waveTable[i] = (uint8_t)dacValue;
  }
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  buildWaveTable();
  dacWrite(DAC_PIN, 128);

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);

  inaOk = ina219.begin();

  if (inaOk)
  {
    ina219.setCalibration_32V_1A();
    Serial.println("# INA219 found: DAC + power logger mode");
  }
  else
  {
    Serial.println("# INA219 not found: DAC-only mode");
  }

  nextWaveUs = micros();
  nextInaMs = millis();
  lastInaUs = micros();

  Serial.println("# Signal generator running on GPIO25");
}

void loop()
{
  uint32_t nowUs = micros();

  // Generazione DAC sempre attiva, indipendente dalla INA219
  if ((int32_t)(nowUs - nextWaveUs) >= 0)
  {
    dacWrite(DAC_PIN, waveTable[waveIdx]);

    waveIdx++;
    if (waveIdx >= TABLE_SIZE)
    {
      waveIdx = 0;
    }

    nextWaveUs += TS_WAVE_US;
  }

  // Misura INA219 solo se presente
  if (inaOk && (int32_t)(millis() - nextInaMs) >= 0)
  {
    uint32_t currentUs = micros();
    float dt_s = (currentUs - lastInaUs) / 1000000.0f;
    lastInaUs = currentUs;

    float shunt_mV = ina219.getShuntVoltage_mV();
    float bus_V = ina219.getBusVoltage_V();
    float current_mA = ina219.getCurrent_mA();
    float power_mW = ina219.getPower_mW();
    float load_V = bus_V + (shunt_mV / 1000.0f);

    energy_mJ += power_mW * dt_s;

    Serial.print(millis() / 1000.0f, 3);
    Serial.print('\t');
    Serial.print(bus_V, 4);
    Serial.print('\t');
    Serial.print(shunt_mV, 4);
    Serial.print('\t');
    Serial.print(load_V, 4);
    Serial.print('\t');
    Serial.print(current_mA, 3);
    Serial.print('\t');
    Serial.print(power_mW, 3);
    Serial.print('\t');
    Serial.println(energy_mJ, 3);

    nextInaMs += INA_PERIOD_MS;
  }
}