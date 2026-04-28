#pragma once

const char *WIFI_SSID = "yourwifi";
const char *WIFI_PASSWORD = "yourpswrd";

const char *MQTT_HOST = "192.000.0.0"; // IP locale del tuo PC
const uint16_t MQTT_PORT = 1883;

const char *MQTT_CLIENT_ID = "heltec-v3-test";
const char *MQTT_TOPIC_STATUS = "iot/heltec/status";
const char *MQTT_TOPIC_DATA = "iot/heltec/aggregate";
