#pragma once

const char *WIFI_SSID = "Vodafone-C44464333";   //"FiloPhone"; //
const char *WIFI_PASSWORD = "9yMeC7aR47ytfPxJ"; //"*********"; //"9yMeC7aR47ytfPxJ";

const char *MQTT_HOST = "192.168.1.4"; //"172.20.10.4"; //"192.168.1.2"; // IP locale del tuo PC
const uint16_t MQTT_PORT = 1883;

const char *MQTT_CLIENT_ID = "heltec-v3-test";
const char *MQTT_TOPIC_STATUS = "iot/heltec/status";
const char *MQTT_TOPIC_DATA = "iot/heltec/aggregate";