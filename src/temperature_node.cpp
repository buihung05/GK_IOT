#include <Arduino.h>
#include <DHT.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "AppConfig.h"

namespace {
constexpr uint8_t DHT_TYPE = DHT11;
constexpr uint32_t SEND_INTERVAL_MS = 2500;
constexpr uint32_t RECONNECT_INTERVAL_MS = 5000;
constexpr char CONTROLLER_URL[] = "http://192.168.4.1/api/temperature";

DHT dht(DHT_DATA_PIN, DHT_TYPE);
uint32_t lastSendAtMs = 0;
uint32_t lastReconnectAtMs = 0;

void connectToController() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.printf("Dang ket noi den %s...\n", AP_SSID);
  WiFi.disconnect();
  WiFi.begin(AP_SSID, AP_PASSWORD);
}

void sendReading(float temperatureC, float humidityPercent) {
  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, CONTROLLER_URL)) return;

  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  const String body =
      "token=" + String(TEMPERATURE_NODE_TOKEN) +
      "&temperature=" + String(temperatureC, 1) +
      "&humidity=" + String(humidityPercent, 1);
  const int status = http.POST(body);
  Serial.printf("Nhiet do %.1f C, do am %.1f %%, HTTP %d\n",
                temperatureC, humidityPercent, status);
  http.end();
}
}  // namespace

void setup() {
  Serial.begin(115200);
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);
  dht.begin();
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  connectToController();
}

void loop() {
  const uint32_t now = millis();
  if (WiFi.status() != WL_CONNECTED) {
    digitalWrite(STATUS_LED_PIN, LOW);
    if (now - lastReconnectAtMs >= RECONNECT_INTERVAL_MS) {
      lastReconnectAtMs = now;
      connectToController();
    }
    delay(10);
    return;
  }

  digitalWrite(STATUS_LED_PIN, HIGH);
  if (now - lastSendAtMs < SEND_INTERVAL_MS) {
    delay(10);
    return;
  }
  lastSendAtMs = now;

  const float humidity = dht.readHumidity();
  const float temperature = dht.readTemperature();
  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("Khong doc duoc DHT11");
    return;
  }
  sendReading(temperature, humidity);
}
