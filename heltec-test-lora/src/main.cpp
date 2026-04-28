#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <arduinoFFT.h>

// =====================
// ADC / SIGNAL SETTINGS
// =====================
constexpr uint8_t ADC_PIN = 2;

// FFT iniziale per stimare la frequenza del segnale
constexpr uint16_t FFT_SAMPLES = 1024;
constexpr double FFT_SAMPLE_FREQ_HZ = 1000.0;
constexpr uint32_t FFT_SAMPLE_PERIOD_US = 1000000UL / (uint32_t)FFT_SAMPLE_FREQ_HZ;

constexpr float MIN_SAMPLE_FREQ_HZ = 20.0f;
constexpr float MAX_SAMPLE_FREQ_HZ = 2000.0f;
constexpr float NYQUIST_MARGIN = 2.5f;

constexpr double MIN_VALID_FREQ_HZ = 0.2;
constexpr double MAX_VALID_FREQ_HZ = 40.0;
constexpr double RELATIVE_THRESHOLD = 0.20;

// Aggregazione
constexpr uint32_t AGGREGATION_WINDOW_MS = 10000;

// ATTENZIONE: con intervalli più brevi la trasmissione non parte proprio
constexpr uint32_t UPLINK_INTERVAL_MS = 60000;

// =====================
// HELTEC V3 SX1262 PINS
// =====================
constexpr int LORA_NSS = 8;
constexpr int LORA_DIO1 = 14;
constexpr int LORA_RST = 12;
constexpr int LORA_BUSY = 13;
constexpr int LORA_SCK = 9;
constexpr int LORA_MISO = 11;
constexpr int LORA_MOSI = 10;

// RadioLib SX1262
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);

// Regione EU868
LoRaWANNode node(&radio, &EU868);

// =====================
// TTN / OTAA CREDENTIALS
// =====================

// JoinEUI / AppEUI (lasciato a 0 come da TTN)
uint64_t joinEUI = 0x0000000000000000ULL;

// DevEUI (NUOVO)
uint64_t devEUI = 0x70B3D57ED00772E6ULL;

// AppKey (NUOVA)
uint8_t appKey[] = {
    0xC5, 0xDB, 0xA1, 0xAD,
    0x35, 0xBA, 0xEA, 0x63,
    0x25, 0xB6, 0x08, 0xAA,
    0xDF, 0xEB, 0x17, 0x93};

// Per RadioLib OTAA (LoRaWAN 1.0.x)
uint8_t nwkKey[] = {
    0xC5, 0xDB, 0xA1, 0xAD,
    0x35, 0xBA, 0xEA, 0x63,
    0x25, 0xB6, 0x08, 0xAA,
    0xDF, 0xEB, 0x17, 0x93};

// =====================
// FFT BUFFERS
// =====================
double vReal[FFT_SAMPLES];
double vImag[FFT_SAMPLES];

ArduinoFFT<double> FFT(vReal, vImag, FFT_SAMPLES, FFT_SAMPLE_FREQ_HZ);

// =====================
// RUNTIME STATE
// =====================
float currentSampleFreqHz = 50.0f;
uint32_t currentSamplePeriodUs = 20000;
uint32_t nextSampleUs = 0;

uint32_t windowStartMs = 0;
uint32_t lastUplinkMs = 0;

uint32_t sampleCount = 0;
uint64_t sampleSum = 0;

// =====================
// UTILITY
// =====================
void printState(const char *msg, int16_t state)
{
  Serial.print(msg);
  Serial.print(" -> ");
  Serial.println(state);
}

float computeSamplingRate(float fmax)
{
  float fs = fmax * NYQUIST_MARGIN;

  if (fs < MIN_SAMPLE_FREQ_HZ)
    fs = MIN_SAMPLE_FREQ_HZ;
  if (fs > MAX_SAMPLE_FREQ_HZ)
    fs = MAX_SAMPLE_FREQ_HZ;

  return fs;
}

float runInitialFft()
{
  Serial.println("# FFT: starting initial acquisition...");

  uint32_t nextUs = micros();

  for (uint16_t i = 0; i < FFT_SAMPLES; i++)
  {
    while ((int32_t)(micros() - nextUs) < 0)
    {
      // precise wait
    }

    nextUs += FFT_SAMPLE_PERIOD_US;

    int raw = analogRead(ADC_PIN);
    vReal[i] = (double)raw;
    vImag[i] = 0.0;
  }

  // Rimuovi componente DC
  double mean = 0.0;
  for (uint16_t i = 0; i < FFT_SAMPLES; i++)
  {
    mean += vReal[i];
  }
  mean /= FFT_SAMPLES;

  for (uint16_t i = 0; i < FFT_SAMPLES; i++)
  {
    vReal[i] -= mean;
  }

  FFT.windowing(FFTWindow::Hamming, FFTDirection::Forward);
  FFT.compute(FFTDirection::Forward);
  FFT.complexToMagnitude();

  double binWidthHz = FFT_SAMPLE_FREQ_HZ / FFT_SAMPLES;

  uint16_t minBin = ceil(MIN_VALID_FREQ_HZ / binWidthHz);
  uint16_t maxBin = floor(MAX_VALID_FREQ_HZ / binWidthHz);

  if (maxBin >= FFT_SAMPLES / 2)
  {
    maxBin = FFT_SAMPLES / 2 - 1;
  }

  double maxMag = 0.0;
  uint16_t peakBin = minBin;

  for (uint16_t i = minBin; i <= maxBin; i++)
  {
    if (vReal[i] > maxMag)
    {
      maxMag = vReal[i];
      peakBin = i;
    }
  }

  double threshold = maxMag * RELATIVE_THRESHOLD;
  uint16_t highestSignificantBin = peakBin;

  for (uint16_t i = minBin; i <= maxBin; i++)
  {
    if (vReal[i] >= threshold)
    {
      highestSignificantBin = i;
    }
  }

  float peakFreqHz = peakBin * binWidthHz;
  float highestFreqHz = highestSignificantBin * binWidthHz;

  Serial.print("# FFT bin_width_hz=");
  Serial.println(binWidthHz, 4);

  Serial.print("# FFT peak_freq_hz=");
  Serial.println(peakFreqHz, 4);

  Serial.print("# FFT highest_significant_freq_hz=");
  Serial.println(highestFreqHz, 4);

  return highestFreqHz;
}

void resetAggregationWindow()
{
  windowStartMs = millis();
  sampleCount = 0;
  sampleSum = 0;
}

bool sendMeanUplink(uint16_t meanAdc)
{
  uint8_t payload[2];

  // Big-endian: byte alto, byte basso
  payload[0] = meanAdc >> 8;
  payload[1] = meanAdc & 0xFF;

  Serial.print("# LoRaWAN uplink mean_adc=");
  Serial.println(meanAdc);

  int16_t state = node.sendReceive(payload, sizeof(payload), 1, false);

  if (state >= RADIOLIB_ERR_NONE)
  {
    Serial.print("# LoRaWAN uplink OK, state=");
    Serial.println(state);

    if (state > 0)
    {
      Serial.println("# LoRaWAN downlink received");
    }
    else
    {
      Serial.println("# LoRaWAN no downlink");
    }

    return true;
  }

  Serial.print("# LoRaWAN uplink FAILED, error=");
  Serial.println(state);
  return false;
}

void setupLoRaWAN()
{
  Serial.println("# LoRaWAN: initializing SPI...");
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);

  Serial.println("# LoRaWAN: initializing radio SX1262...");
  int16_t state = radio.begin();
  printState("# radio.begin()", state);

  if (state != RADIOLIB_ERR_NONE)
  {
    Serial.println("# ERROR: radio init failed. Check antenna, pins, board.");
    while (true)
      delay(1000);
  }

  Serial.println("# LoRaWAN: configuring OTAA credentials...");
  state = node.beginOTAA(joinEUI, devEUI, nwkKey, appKey);
  printState("# node.beginOTAA()", state);

  if (state != RADIOLIB_ERR_NONE)
  {
    Serial.println("# ERROR: beginOTAA failed. Check keys format.");
    while (true)
      delay(1000);
  }

  node.setADR(true);

  Serial.println("# LoRaWAN: joining TTN via OTAA...");
  Serial.println("# If this hangs/fails, check gateway coverage, EU868, keys, JoinEUI, DevEUI.");

  state = node.activateOTAA();
  printState("# node.activateOTAA()", state);

  if (state == RADIOLIB_LORAWAN_NEW_SESSION)
  {
    Serial.println("# LoRaWAN JOIN SUCCESS: new session created");
  }
  else if (state == RADIOLIB_LORAWAN_SESSION_RESTORED)
  {
    Serial.println("# LoRaWAN JOIN SUCCESS: session restored");
  }
  else
  {
    Serial.println("# ERROR: OTAA join failed.");
    Serial.println("# Check TTN Live Data for join request/join accept details.");
    while (true)
      delay(5000);
  }
  delay(5000);
}

void setup()
{
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("# Heltec V3 ADC + FFT + LoRaWAN mean uplink");

  analogReadResolution(12);
  analogSetPinAttenuation(ADC_PIN, ADC_11db);

  setupLoRaWAN();

  float fmax = runInitialFft();
  currentSampleFreqHz = computeSamplingRate(fmax);
  currentSamplePeriodUs = (uint32_t)(1000000.0f / currentSampleFreqHz);

  Serial.print("# Sampling freq selected_hz=");
  Serial.println(currentSampleFreqHz, 3);

  nextSampleUs = micros() + currentSamplePeriodUs;
  resetAggregationWindow();
  lastUplinkMs = millis();

  Serial.println("# Runtime started");
}

void loop()
{
  uint32_t nowUs = micros();

  if ((int32_t)(nowUs - nextSampleUs) >= 0)
  {
    nextSampleUs += currentSamplePeriodUs;

    int raw = analogRead(ADC_PIN);

    sampleCount++;
    sampleSum += raw;
  }

  uint32_t nowMs = millis();

  if ((uint32_t)(nowMs - windowStartMs) >= AGGREGATION_WINDOW_MS)
  {
    if (sampleCount > 0)
    {
      float mean = sampleSum / (float)sampleCount;
      uint16_t meanU16 = (uint16_t)roundf(mean);

      Serial.print("# Aggregate window n=");
      Serial.print(sampleCount);
      Serial.print(" mean=");
      Serial.println(meanU16);

      if ((uint32_t)(nowMs - lastUplinkMs) >= UPLINK_INTERVAL_MS)
      {
        sendMeanUplink(meanU16);
        lastUplinkMs = millis();
      }
    }

    resetAggregationWindow();
  }

  delay(1);
}