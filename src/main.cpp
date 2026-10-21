#include <Arduino.h>
#include <Adafruit_INA219.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>

#include "AppConfig.h"
#include "ChargerController.h"
#include "WebPage.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#define WIFI_SSID ""
#define WIFI_PASSWORD ""
#endif

#ifndef SIMULATION_MODE
#define SIMULATION_MODE 1
#endif

namespace {
constexpr uint8_t INA219_I2C_ADDRESS = 0x40;
constexpr uint16_t INA219_CONFIG_32V_64_SAMPLES = 0x3F77;
constexpr uint16_t PWM_MAX = (1U << MOSFET_PWM_RESOLUTION_BITS) - 1U;

WebServer server(80);
Preferences preferences;
Adafruit_INA219 ina219;
ChargerController controller;
SensorReading latestReading;

bool inaReady = false;
uint32_t lastSensorAtMs = 0;
uint32_t handledEventId = 0;
uint8_t learningSamples = 0;
float learnedTypicalPowerW = 0.0f;
uint16_t potentiometerRaw = 0;
uint8_t mosfetDuty = 0;

float remoteTemperatureC = 0.0f;
float remoteHumidityPercent = 0.0f;
uint32_t lastRemoteTemperatureAtMs = 0;
bool hasRemoteTemperature = false;

#if SIMULATION_MODE
float simulatedVoltageV = 12.0f;
float simulatedCurrentA = 0.48f;
float simulatedTemperatureC = 30.0f;
float simulatedHumidityPercent = 55.0f;
bool simulatedPowerValid = true;
bool simulatedTemperatureValid = true;
#endif

String boolJson(bool value) { return value ? "true" : "false"; }

float constrainedArg(const String& name, float fallback, float low, float high) {
  if (!server.hasArg(name)) return fallback;
  return constrain(server.arg(name).toFloat(), low, high);
}

uint32_t secondsArg(const String& name, uint32_t fallbackMs,
                    uint32_t lowSeconds, uint32_t highSeconds) {
  if (!server.hasArg(name)) return fallbackMs;
  const uint32_t seconds = static_cast<uint32_t>(server.arg(name).toInt());
  return constrain(seconds, lowSeconds, highSeconds) * 1000UL;
}

void writeIna219Configuration(uint16_t value) {
  Wire.beginTransmission(INA219_I2C_ADDRESS);
  Wire.write(0x00);
  Wire.write(static_cast<uint8_t>(value >> 8));
  Wire.write(static_cast<uint8_t>(value & 0xFF));
  Wire.endTransmission();
}

void configurePowerStage() {
  pinMode(MOSFET_GATE_PIN, OUTPUT);
  pinMode(POTENTIOMETER_PIN, INPUT);
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(MOSFET_GATE_PIN, LOW);
  digitalWrite(STATUS_LED_PIN, LOW);
  analogReadResolution(12);
  ledcSetup(MOSFET_PWM_CHANNEL, MOSFET_PWM_FREQUENCY_HZ,
            MOSFET_PWM_RESOLUTION_BITS);
  ledcAttachPin(MOSFET_GATE_PIN, MOSFET_PWM_CHANNEL);
  ledcWrite(MOSFET_PWM_CHANNEL, 0);
}

void applyPowerStage() {
#if SIMULATION_MODE
  mosfetDuty = controller.relayRequested()
                   ? static_cast<uint8_t>(map(potentiometerRaw, 0, 4095, 0, PWM_MAX))
                   : 0;
  // Chế độ mô phỏng luôn giữ chân Gate ở mức tắt dù dashboard hiển thị PWM ảo.
  ledcWrite(MOSFET_PWM_CHANNEL, 0);
#else
  mosfetDuty = controller.relayRequested()
                   ? static_cast<uint8_t>(map(potentiometerRaw, 0, 4095, 0, PWM_MAX))
                   : 0;
  ledcWrite(MOSFET_PWM_CHANNEL, mosfetDuty);
#endif
  digitalWrite(STATUS_LED_PIN, mosfetDuty > 0 ? HIGH : LOW);
}

void loadPersistentData() {
  preferences.begin("ev-charger", false);

  ChargerSettings settings;
  settings.activePowerW = preferences.getFloat("pActive", 2.0f);
  settings.fixedFullPowerW = preferences.getFloat("pFull", 0.8f);
  settings.learnedFullRatio = preferences.getFloat("pRatio", 0.18f);
  settings.warningTemperatureC = preferences.getFloat("tWarn", 35.0f);
  settings.cutoffTemperatureC = preferences.getFloat("tCut", 45.0f);
  settings.recoveryTemperatureC = preferences.getFloat("tRecover", 32.0f);
  settings.minimumChargeMs = preferences.getUInt("minCharge", 10000UL);
  settings.fullConfirmMs = preferences.getUInt("confirm", 60000UL);
  settings.decisionWaitMs = preferences.getUInt("decision", 900000UL);
  settings.continuePauseMs = preferences.getUInt("pause", 300000UL);
  controller.setSettings(settings);

  learnedTypicalPowerW = preferences.getFloat("learnedP", 0.0f);
  learningSamples = preferences.getUChar("learnedN", 0);
  controller.setLearningProfile(learnedTypicalPowerW, learningSamples);
}

void saveSettings(const ChargerSettings& settings) {
  preferences.putFloat("pActive", settings.activePowerW);
  preferences.putFloat("pFull", settings.fixedFullPowerW);
  preferences.putFloat("pRatio", settings.learnedFullRatio);
  preferences.putFloat("tWarn", settings.warningTemperatureC);
  preferences.putFloat("tCut", settings.cutoffTemperatureC);
  preferences.putFloat("tRecover", settings.recoveryTemperatureC);
  preferences.putUInt("minCharge", settings.minimumChargeMs);
  preferences.putUInt("confirm", settings.fullConfirmMs);
  preferences.putUInt("decision", settings.decisionWaitMs);
  preferences.putUInt("pause", settings.continuePauseMs);
}

void saveLearningFromCompletedSession() {
  if (learningSamples >= 3 || controller.activeSampleCount() < 5) return;
  const float sessionAverage = controller.sessionAverageActivePowerW();
  if (sessionAverage < controller.settings().activePowerW) return;

  learnedTypicalPowerW =
      (learnedTypicalPowerW * learningSamples + sessionAverage) /
      static_cast<float>(learningSamples + 1);
  ++learningSamples;
  preferences.putFloat("learnedP", learnedTypicalPowerW);
  preferences.putUChar("learnedN", learningSamples);
  controller.setLearningProfile(learnedTypicalPowerW, learningSamples);
}

void resetLearning() {
  learnedTypicalPowerW = 0.0f;
  learningSamples = 0;
  preferences.remove("learnedP");
  preferences.remove("learnedN");
  controller.setLearningProfile(0.0f, 0);
}

void readSensors() {
#if SIMULATION_MODE
  latestReading.voltageV = simulatedVoltageV;
  latestReading.currentA = simulatedCurrentA;
  latestReading.powerW = simulatedVoltageV * simulatedCurrentA;
  latestReading.temperatureC = simulatedTemperatureC;
  latestReading.powerValid = simulatedPowerValid;
  latestReading.temperatureValid = simulatedTemperatureValid;
  potentiometerRaw = static_cast<uint16_t>(
      constrain(simulatedCurrentA / 0.48f, 0.0f, 1.0f) * 4095.0f);
#else
  potentiometerRaw = analogRead(POTENTIOMETER_PIN);
  latestReading.powerValid = inaReady;
  if (inaReady) {
    const float busVoltage = ina219.getBusVoltage_V();
    const float shuntVoltage = ina219.getShuntVoltage_mV() / 1000.0f;
    latestReading.voltageV = max(0.0f, busVoltage + shuntVoltage);
    latestReading.currentA = max(0.0f, ina219.getCurrent_mA() / 1000.0f);
    latestReading.powerW = latestReading.voltageV * latestReading.currentA;
  }

  const uint32_t age = millis() - lastRemoteTemperatureAtMs;
  latestReading.temperatureC = remoteTemperatureC;
  latestReading.temperatureValid =
      hasRemoteTemperature && age <= TEMPERATURE_STALE_MS;
#endif
}

String statusJson() {
  const ChargerSettings& cfg = controller.settings();
  const uint32_t temperatureAgeSeconds =
#if SIMULATION_MODE
      0;
#else
      hasRemoteTemperature ? (millis() - lastRemoteTemperatureAtMs) / 1000UL
                           : 0xFFFFFFFFUL;
#endif
  const float humidity =
#if SIMULATION_MODE
      simulatedHumidityPercent;
#else
      remoteHumidityPercent;
#endif

  String json;
  json.reserve(1500);
  json += "{";
  json += "\"state\":\"" + String(ChargerController::stateName(controller.state())) + "\",";
  json += "\"event\":\"" + String(ChargerController::eventName(controller.lastEvent())) + "\",";
  json += "\"eventId\":" + String(controller.eventId()) + ",";
  json += "\"relayOn\":" + boolJson(controller.relayRequested()) + ",";
  json += "\"outputEnabled\":" + boolJson(controller.relayRequested()) + ",";
  json += "\"mosfetDuty\":" + String(roundf(mosfetDuty * 100.0f / PWM_MAX), 0) + ",";
  json += "\"potentiometer\":" + String(roundf(potentiometerRaw * 100.0f / 4095.0f), 0) + ",";
  json += "\"voltage\":" + String(latestReading.voltageV, 2) + ",";
  json += "\"current\":" + String(latestReading.currentA, 3) + ",";
  json += "\"power\":" + String(latestReading.powerW, 2) + ",";
  json += "\"filteredPower\":" + String(controller.filteredPowerW(), 2) + ",";
  json += "\"temperature\":" + String(latestReading.temperatureC, 1) + ",";
  json += "\"humidity\":" + String(humidity, 1) + ",";
  json += "\"temperatureAgeSeconds\":" + String(temperatureAgeSeconds) + ",";
  json += "\"temperatureSource\":\"" + String(SIMULATION_MODE ? "simulation" : "dht11_remote") + "\",";
  json += "\"powerValid\":" + boolJson(latestReading.powerValid) + ",";
  json += "\"temperatureValid\":" + boolJson(latestReading.temperatureValid) + ",";
  json += "\"temperatureWarning\":" + boolJson(controller.temperatureWarning()) + ",";
  json += "\"seenActive\":" + boolJson(controller.hasSeenActiveCharge()) + ",";
  json += "\"sessionSeconds\":" + String(controller.sessionElapsedMs(millis()) / 1000UL) + ",";
  json += "\"decisionRemainingSeconds\":" + String(controller.decisionRemainingMs(millis()) / 1000UL) + ",";
  json += "\"effectiveFullThreshold\":" + String(controller.effectiveFullThresholdW(), 2) + ",";
  json += "\"learnedPower\":" + String(learnedTypicalPowerW, 2) + ",";
  json += "\"learningSamples\":" + String(learningSamples) + ",";
  json += "\"simulation\":" + boolJson(SIMULATION_MODE != 0) + ",";
  json += "\"wifiConnected\":" + boolJson(WiFi.status() == WL_CONNECTED) + ",";
  json += "\"stationIp\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"apIp\":\"" + WiFi.softAPIP().toString() + "\",";
  json += "\"config\":{";
  json += "\"activePower\":" + String(cfg.activePowerW, 2) + ",";
  json += "\"fullPower\":" + String(cfg.fixedFullPowerW, 2) + ",";
  json += "\"fullRatio\":" + String(cfg.learnedFullRatio, 2) + ",";
  json += "\"warningTemp\":" + String(cfg.warningTemperatureC, 1) + ",";
  json += "\"cutoffTemp\":" + String(cfg.cutoffTemperatureC, 1) + ",";
  json += "\"recoveryTemp\":" + String(cfg.recoveryTemperatureC, 1) + ",";
  json += "\"minimumChargeSeconds\":" + String(cfg.minimumChargeMs / 1000UL) + ",";
  json += "\"confirmSeconds\":" + String(cfg.fullConfirmMs / 1000UL) + ",";
  json += "\"decisionSeconds\":" + String(cfg.decisionWaitMs / 1000UL) + ",";
  json += "\"continuePauseSeconds\":" + String(cfg.continuePauseMs / 1000UL);
  json += "}}";
  return json;
}

void sendJson(int status, const String& body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(status, "application/json; charset=utf-8", body);
}

void handleAction() {
  const String action = server.arg("action");
  bool accepted = true;
  if (action == "start") {
    accepted = controller.start(millis());
  } else if (action == "stop") {
    controller.stopManually(millis());
  } else if (action == "continue") {
    accepted = controller.continueCharging(millis());
  } else if (action == "reset_learning") {
    resetLearning();
  } else {
    sendJson(400, "{\"ok\":false,\"message\":\"Lenh khong hop le\"}");
    return;
  }

  applyPowerStage();
  sendJson(accepted ? 200 : 409,
           String("{\"ok\":") + boolJson(accepted) +
               ",\"status\":" + statusJson() + "}");
}

void handleConfig() {
  ChargerSettings next = controller.settings();
  next.activePowerW = constrainedArg("activePower", next.activePowerW, 0.2f, 100.0f);
  next.fixedFullPowerW = constrainedArg("fullPower", next.fixedFullPowerW, 0.05f, 50.0f);
  next.learnedFullRatio = constrainedArg("fullRatio", next.learnedFullRatio, 0.05f, 0.8f);
  next.warningTemperatureC = constrainedArg("warningTemp", next.warningTemperatureC, 20.0f, 90.0f);
  next.cutoffTemperatureC = constrainedArg("cutoffTemp", next.cutoffTemperatureC, 25.0f, 100.0f);
  next.recoveryTemperatureC = constrainedArg("recoveryTemp", next.recoveryTemperatureC, 10.0f, 80.0f);
  next.minimumChargeMs = secondsArg("minimumChargeSeconds", next.minimumChargeMs, 0, 3600);
  next.fullConfirmMs = secondsArg("confirmSeconds", next.fullConfirmMs, 3, 3600);
  next.decisionWaitMs = secondsArg("decisionSeconds", next.decisionWaitMs, 5, 3600);
  next.continuePauseMs = secondsArg("continuePauseSeconds", next.continuePauseMs, 5, 3600);

  if (next.fixedFullPowerW >= next.activePowerW ||
      next.recoveryTemperatureC >= next.cutoffTemperatureC ||
      next.warningTemperatureC >= next.cutoffTemperatureC) {
    sendJson(422, "{\"ok\":false,\"message\":\"Nguong cau hinh khong hop le\"}");
    return;
  }

  controller.setSettings(next);
  saveSettings(next);
  sendJson(200, String("{\"ok\":true,\"status\":") + statusJson() + "}");
}

void handleTemperatureUpdate() {
  if (server.arg("token") != TEMPERATURE_NODE_TOKEN) {
    sendJson(403, "{\"ok\":false,\"message\":\"Token khong hop le\"}");
    return;
  }
  if (!server.hasArg("temperature")) {
    sendJson(400, "{\"ok\":false,\"message\":\"Thieu nhiet do\"}");
    return;
  }

  const float temperature = server.arg("temperature").toFloat();
  const float humidity = server.hasArg("humidity")
                             ? server.arg("humidity").toFloat()
                             : remoteHumidityPercent;
  if (temperature < -20.0f || temperature > 60.0f || humidity < 0.0f ||
      humidity > 100.0f) {
    sendJson(422, "{\"ok\":false,\"message\":\"Gia tri cam bien khong hop le\"}");
    return;
  }

  remoteTemperatureC = temperature;
  remoteHumidityPercent = humidity;
  lastRemoteTemperatureAtMs = millis();
  hasRemoteTemperature = true;
  sendJson(200, "{\"ok\":true}");
}

void handleSimulation() {
#if SIMULATION_MODE
  const String preset = server.arg("preset");
  if (preset == "charging") {
    simulatedVoltageV = 12.0f;
    simulatedCurrentA = 0.48f;
    simulatedTemperatureC = 31.0f;
    simulatedPowerValid = simulatedTemperatureValid = true;
  } else if (preset == "near_full") {
    simulatedVoltageV = 12.0f;
    simulatedCurrentA = 0.09f;
    simulatedTemperatureC = 34.0f;
    simulatedPowerValid = simulatedTemperatureValid = true;
  } else if (preset == "full") {
    simulatedVoltageV = 12.0f;
    simulatedCurrentA = 0.03f;
    simulatedTemperatureC = 32.0f;
    simulatedPowerValid = simulatedTemperatureValid = true;
  } else if (preset == "overheat") {
    simulatedVoltageV = 12.0f;
    simulatedCurrentA = 0.40f;
    simulatedTemperatureC = 60.0f;
    simulatedPowerValid = simulatedTemperatureValid = true;
  } else if (preset == "sensor_fault") {
    simulatedTemperatureValid = false;
  } else if (preset == "disconnected") {
    simulatedCurrentA = 0.0f;
    simulatedTemperatureC = 29.0f;
    simulatedPowerValid = simulatedTemperatureValid = true;
  } else if (preset == "custom") {
    simulatedVoltageV = constrainedArg("voltage", simulatedVoltageV, 0.0f, 26.0f);
    simulatedCurrentA = constrainedArg("current", simulatedCurrentA, 0.0f, 5.0f);
    simulatedTemperatureC = constrainedArg("temperature", simulatedTemperatureC, -20.0f, 80.0f);
    simulatedPowerValid = simulatedTemperatureValid = true;
  } else {
    sendJson(400, "{\"ok\":false,\"message\":\"Kich ban khong hop le\"}");
    return;
  }
  readSensors();
  applyPowerStage();
  sendJson(200, String("{\"ok\":true,\"status\":") + statusJson() + "}");
#else
  sendJson(403, "{\"ok\":false,\"message\":\"Che do mo phong da tat\"}");
#endif
}

void configureWebServer() {
  server.on("/", HTTP_GET, []() {
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
  });
  server.on("/api/status", HTTP_GET, []() { sendJson(200, statusJson()); });
  server.on("/api/action", HTTP_POST, handleAction);
  server.on("/api/config", HTTP_POST, handleConfig);
  server.on("/api/temperature", HTTP_POST, handleTemperatureUpdate);
  server.on("/api/sim", HTTP_POST, handleSimulation);
  server.on("/favicon.ico", HTTP_GET, []() { server.send(204); });
  server.onNotFound([]() {
    sendJson(404, "{\"ok\":false,\"message\":\"Khong tim thay tai nguyen\"}");
  });
  server.begin();
}

void configureNetwork() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  if (strlen(WIFI_SSID) > 0) {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    const uint32_t startedAt = millis();
    while (WiFi.status() != WL_CONNECTED &&
           millis() - startedAt < WIFI_CONNECT_TIMEOUT_MS) {
      delay(200);
    }
  }

  Serial.print("Dashboard AP: http://");
  Serial.println(WiFi.softAPIP());
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Dashboard LAN: http://");
    Serial.println(WiFi.localIP());
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  configurePowerStage();

  Wire.begin();
  inaReady = ina219.begin();
  if (inaReady) {
    ina219.setCalibration_32V_2A();
    writeIna219Configuration(INA219_CONFIG_32V_64_SAMPLES);
  }

  loadPersistentData();
  readSensors();
  controller.update(latestReading, millis());
  applyPowerStage();
  configureNetwork();
  configureWebServer();

  Serial.printf("Mode: %s\n", SIMULATION_MODE ? "SIMULATION" : "HARDWARE");
}

void loop() {
  server.handleClient();

  const uint32_t now = millis();
  if (now - lastSensorAtMs >= SENSOR_INTERVAL_MS) {
    lastSensorAtMs = now;
    readSensors();
    controller.update(latestReading, now);
    applyPowerStage();

    if (controller.eventId() != handledEventId) {
      handledEventId = controller.eventId();
      if (controller.lastEvent() == ChargerEvent::FULL_CONFIRMED) {
        saveLearningFromCompletedSession();
      }
    }
  }
}
