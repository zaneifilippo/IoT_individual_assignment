#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <arduinoFFT.h>
#include "secrets.h"

constexpr uint8_t ADC_PIN = 2;

// FFT iniziale pensata per segnali lenti
constexpr uint16_t FFT_SAMPLES = 1024;
constexpr double FFT_SAMPLE_FREQ_HZ = 100.0;
constexpr uint32_t FFT_SAMPLE_PERIOD_US = 1000000UL / (uint32_t)FFT_SAMPLE_FREQ_HZ;

// Sampling
constexpr float MIN_SAMPLE_FREQ_HZ = 5.0f;
constexpr float MAX_SAMPLE_FREQ_HZ = 2000.0f;
constexpr float NYQUIST_MARGIN = 2.5f;

// Soglie anti-rumore, come nel primo codice
constexpr double MIN_VALID_FREQ_HZ = 0.2;
constexpr double MAX_VALID_FREQ_HZ = 40.0;
constexpr double RELATIVE_THRESHOLD = 0.20;

// Buffer FFT
double vReal[FFT_SAMPLES];
double vImag[FFT_SAMPLES];
ArduinoFFT<double> FFT(vReal, vImag, FFT_SAMPLES, FFT_SAMPLE_FREQ_HZ);

// Aggregazione
constexpr uint32_t AGG_WINDOW_MS = 20000;

// MQTT
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// Sampling
float currentSampleFreqHz = 50.0f;
uint32_t currentSamplePeriodUs = 20000;
uint32_t nextSampleUs = 0;

// Aggregazione
uint32_t windowStartMs = 0;
uint32_t sampleCount = 0;
uint64_t sampleSum = 0;
int sampleMin = 4095;
int sampleMax = 0;

void connectWiFi()
{
  if (WiFi.status() == WL_CONNECTED)
    return;

  Serial.print("# Connecting WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n# WiFi connected");

  Serial.print("# Heltec IP: ");
  Serial.println(WiFi.localIP());

  Serial.print("# MQTT host: ");
  Serial.println(MQTT_HOST);
}

void connectMQTT()
{
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);

  while (!mqttClient.connected())
  {
    Serial.print("# Connecting MQTT... ");

    if (mqttClient.connect(MQTT_CLIENT_ID))
    {
      Serial.println("OK");

      mqttClient.publish(
          MQTT_TOPIC_STATUS,
          "connessione con heltec riuscita");
    }
    else
    {
      Serial.print("FAIL rc=");
      Serial.println(mqttClient.state());
      delay(2000);
    }
  }
}

float computeSamplingRate(float highestFreqHz)
{
  float fs = highestFreqHz * NYQUIST_MARGIN;

  if (fs < MIN_SAMPLE_FREQ_HZ)
    fs = MIN_SAMPLE_FREQ_HZ;

  if (fs > MAX_SAMPLE_FREQ_HZ)
    fs = MAX_SAMPLE_FREQ_HZ;

  return fs;
}

float runFFT()
{
  Serial.println("# Initial FFT acquisition...");

  uint32_t nextUs = micros();

  for (uint16_t i = 0; i < FFT_SAMPLES; i++)
  {
    while ((int32_t)(micros() - nextUs) < 0)
    {
    }

    nextUs += FFT_SAMPLE_PERIOD_US;

    int raw = analogRead(ADC_PIN);
    vReal[i] = (double)raw;
    vImag[i] = 0.0;
  }

  // Rimuove offset DC
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

  Serial.print("# bin_width_hz=");
  Serial.println(binWidthHz, 4);

  Serial.print("# peak_freq_hz=");
  Serial.println(peakFreqHz, 4);

  Serial.print("# highest_significant_freq_hz=");
  Serial.println(highestFreqHz, 4);

  return highestFreqHz;
}

// Creazione del pacchetto di dati da mandare
void publishData()
{
  if (sampleCount == 0)
    return;

  float mean = sampleSum / (float)sampleCount;

  char payload[200];

  snprintf(payload, sizeof(payload),
           "{\"mean\":%.2f}", //,\"min\":%d,\"max\":%d,\"n\":%lu,\"fs\":%.2f}",
           mean);             //, sampleMin, sampleMax,
                              //(unsigned long)sampleCount,
                              // currentSampleFreqHz);

  mqttClient.publish(MQTT_TOPIC_DATA, payload);

  Serial.println(payload);
}

void resetWindow()
{
  windowStartMs = millis();
  sampleCount = 0;
  sampleSum = 0;
  sampleMin = 4095;
  sampleMax = 0;
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  analogReadResolution(12);
  analogSetPinAttenuation(ADC_PIN, ADC_11db);

  Serial.println("# Heltec FFT + MQTT calibration");

  // Prima calibro la frequenza di campionamento.
  // Solo dopo attivo WiFi e MQTT.
  float highestFreqHz = runFFT();

  currentSampleFreqHz = computeSamplingRate(highestFreqHz);
  currentSamplePeriodUs = (uint32_t)(1000000.0f / currentSampleFreqHz);

  Serial.print("# selected_sample_freq_hz=");
  Serial.println(currentSampleFreqHz, 3);

  connectWiFi();
  connectMQTT();

  nextSampleUs = micros() + currentSamplePeriodUs;
  resetWindow();
}

void loop()
{
  if (WiFi.status() != WL_CONNECTED)
    connectWiFi();

  if (!mqttClient.connected())
    connectMQTT();

  mqttClient.loop();

  uint32_t nowUs = micros();

  if ((int32_t)(nowUs - nextSampleUs) >= 0)
  {
    nextSampleUs += currentSamplePeriodUs;

    int raw = analogRead(ADC_PIN);

    sampleCount++;
    sampleSum += raw;

    if (raw < sampleMin)
      sampleMin = raw;

    if (raw > sampleMax)
      sampleMax = raw;
  }

  if (millis() - windowStartMs >= AGG_WINDOW_MS)
  {
    publishData();
    resetWindow();
  }
}