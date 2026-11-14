// ECSE3038 Test 2: the bathroom fan (board)
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "secrets.h"            // #define DEVICE_KEY "..."  (never committed)

#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASSWORD ""
#define WIFI_CHANNEL 6

const char *API_URL = "http://iot-cstewart.northcentralus.cloudapp.azure.com";
const char *MQTT_NAME = "christinastewart";
const char *BROKER_HOST = "135.232.203.144";
const uint16_t BROKER_PORT = 1883;

const int DHT_PIN = 15;                       // check diagram.json
const int FAN_PIN = 2;                        // check diagram.json
const unsigned long WIFI_TIMEOUT_MS = 15000;
const unsigned long RETRY_EVERY_MS = 5000;
const unsigned long SEND_EVERY_MS = 10000;

DHT dht(DHT_PIN, DHT22);
WiFiClient net;
PubSubClient mqtt(net);

String statusTopic;
String settingsTopic;
String stateTopic;

String fanMode = "";                          // "" until valid settings arrive: fan off
float maxHumidity = NAN;
float lastHumidity = NAN;
String reportedFan = "";                      // what we last told the broker
unsigned long lastSend = 0;
unsigned long lastAttempt = 0;

bool connectWiFi() {
  Serial.print("Connecting to ");
  Serial.print(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD, WIFI_CHANNEL);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > WIFI_TIMEOUT_MS) {
      Serial.println();
      Serial.println("No Wi-Fi after 15 s. Trying again in 5 s.");
      return false;
    }
    delay(250);
    Serial.print(".");
  }
  Serial.println(" connected");
  Serial.print("MAC address: ");
  Serial.println(WiFi.macAddress());
  return true;
}

// B1: POST {"mac", "humidity"} with the X-API-Key header.
void sendReading(float humidity) {
  char body[80];
  snprintf(body, sizeof(body), "{\"mac\": \"%s\", \"humidity\": %.1f}",
           WiFi.macAddress().c_str(), humidity);

  HTTPClient http;
  http.begin(String(API_URL) + "/readings");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Key", DEVICE_KEY);
  int status = http.POST(body);

  Serial.print("POST ");
  Serial.print(body);
  Serial.print(" -> ");
  Serial.println(status);
  if (status == 401) {
    Serial.println("Key refused: POST /fans with this MAC, then put the key in secrets.h.");
  } else if (status < 0) {
    Serial.println(http.errorToString(status));
  }
  http.end();
}

// B4 + B5: drive the fan LED, and report on/off only when it changes.
void updateFan() {
  bool fanOn = false;
  if (fanMode == "on") {
    fanOn = true;
  } else if (fanMode == "auto" && !isnan(maxHumidity) && !isnan(lastHumidity) &&
             lastHumidity > maxHumidity) {
    fanOn = true;
  }

  digitalWrite(FAN_PIN, fanOn ? HIGH : LOW);
  String state = fanOn ? "on" : "off";

  Serial.print("Mode: ");
  Serial.print(fanMode == "" ? "(none yet)" : fanMode);
  Serial.print("  Max: ");
  Serial.print(maxHumidity);
  Serial.print("  Humidity: ");
  Serial.print(lastHumidity);
  Serial.print("  -> fan ");
  Serial.println(state);

  if (state != reportedFan && mqtt.connected()) {
    mqtt.publish(stateTopic.c_str(), state.c_str(), true);
    reportedFan = state;
  }
}

// B3: read the settings JSON, and refuse anything bad.
void onMessage(char *topic, byte *payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  message.trim();

  Serial.print("Got ");
  Serial.print(topic);
  Serial.print(" = ");
  Serial.println(message);

  if (String(topic) != settingsTopic) {
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, message);
  if (error) {
    Serial.print("Refused: not JSON (");
    Serial.print(error.c_str());
    Serial.println("). Settings unchanged.");
    return;
  }
  if (!doc["mode"].is<const char *>() || !doc["max_humidity"].is<float>()) {
    Serial.println("Refused: a field is missing or has the wrong type. Settings unchanged.");
    return;
  }
  String newMode = doc["mode"].as<String>();
  if (newMode != "auto" && newMode != "on" && newMode != "off") {
    Serial.println("Refused: mode must be auto, on or off. Settings unchanged.");
    return;
  }

  fanMode = newMode;
  maxHumidity = doc["max_humidity"].as<float>();
  Serial.println("Settings accepted.");
  updateFan();                                // react now, not at the next reading
}

// B2: connect with a last will, say online, subscribe.
bool connectMqtt() {
  String clientId = "fan-" + String(MQTT_NAME) + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  Serial.print("Connecting to broker ");
  Serial.print(BROKER_HOST);
  Serial.print(" as ");
  Serial.println(clientId);

  bool ok = mqtt.connect(clientId.c_str(), statusTopic.c_str(), 1, true, "offline");
  if (!ok) {
    Serial.print("Broker said no, state ");
    Serial.println(mqtt.state());
    return false;
  }
  Serial.println("Connected to broker");

  mqtt.publish(statusTopic.c_str(), "online", true);
  mqtt.subscribe(settingsTopic.c_str());

  reportedFan = "";                           // new connection: report the fan again
  updateFan();
  return true;
}

void setup() {
  Serial.begin(115200);
  Serial.println("Hello from the ESP32");
  dht.begin();
  pinMode(FAN_PIN, OUTPUT);

  String base = "ecse3038/" + String(MQTT_NAME);
  statusTopic = base + "/status";
  settingsTopic = base + "/fan/settings";
  stateTopic = base + "/fan/state";

  mqtt.setServer(BROKER_HOST, BROKER_PORT);
  mqtt.setCallback(onMessage);
  mqtt.setKeepAlive(15);

  connectWiFi();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastAttempt >= RETRY_EVERY_MS) {
      lastAttempt = millis();
      connectWiFi();
    }
    return;
  }

  if (mqtt.connected()) {
    mqtt.loop();
  } else if (millis() - lastAttempt >= RETRY_EVERY_MS) {
    lastAttempt = millis();
    connectMqtt();
  }

  if (millis() - lastSend < SEND_EVERY_MS) {
    return;
  }
  lastSend = millis();

  float humidity = dht.readHumidity();
  if (isnan(humidity)) {
    Serial.println("No reading from the sensor. Check the three wires.");
    return;
  }
  lastHumidity = humidity;
  sendReading(humidity);
  updateFan();
}