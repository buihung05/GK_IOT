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

#ifndef REQUIRE_TEMPERATURE_NODE
#define REQUIRE_TEMPERATURE_NODE 1
#endif

namespace {
constexpr uint16_t INA219_CONFIG_32V_64_SAMPLES = 0x3F77;

WebServer server(80);
Preferences preferences;
Adafruit_INA219* ina219 = nullptr;
ChargerController controller;
SensorReading latestReading;

bool inaReady = false;
uint8_t ina219I2cAddress = 0x40;
uint32_t lastSensorAtMs = 0;
uint32_t lastInaRetryAtMs = 0;
uint32_t handledEventId = 0;
uint8_t learningSamples = 0;
float learnedTypicalPowerW = 0.0f;
uint16_t potentiometerRaw = 0;
bool relayOutputOn = false;
float measuredPowerW = 0.0f;

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

float storedFloat(const char* key, float fallback) {
  return preferences.isKey(key) ? preferences.getFloat(key, fallback) : fallback;
}

uint32_t storedUInt(const char* key, uint32_t fallback) {
  return preferences.isKey(key) ? preferences.getUInt(key, fallback) : fallback;
}

uint8_t storedUChar(const char* key, uint8_t fallback) {
  return preferences.isKey(key) ? preferences.getUChar(key, fallback) : fallback;
}

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
  Wire.beginTransmission(ina219I2cAddress);
  Wire.write(0x00);
  Wire.write(static_cast<uint8_t>(value >> 8));
  Wire.write(static_cast<uint8_t>(value & 0xFF));
  Wire.endTransmission();
}

bool ina219Responding() {
  Wire.beginTransmission(ina219I2cAddress);
  return Wire.endTransmission() == 0;
}

uint8_t findIna219Address() {
  Serial.print("INA219 I2C scan 0x40-0x4F:");
  for (uint8_t address = 0x40; address <= 0x4F; ++address) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.printf(" found 0x%02X\n", address);
      return address;
    }
  }
  Serial.println(" khong co phan hoi");
  return 0;
}

bool initializeIna219() {
  const uint8_t detectedAddress = findIna219Address();
  if (detectedAddress == 0) {
    inaReady = false;
    Serial.println("INA219: kiem tra VCC/GND/SDA/SCL");
    return false;
  }

  if (ina219 == nullptr || detectedAddress != ina219I2cAddress) {
    delete ina219;
    ina219I2cAddress = detectedAddress;
    ina219 = new Adafruit_INA219(ina219I2cAddress);
  }
  if (!ina219->begin()) {
    inaReady = false;
    Serial.printf("INA219: tim thay 0x%02X nhung khoi tao that bai\n",
                  ina219I2cAddress);
    return false;
  }

  ina219->setCalibration_32V_2A();
  writeIna219Configuration(INA219_CONFIG_32V_64_SAMPLES);
  inaReady = true;
  Serial.printf("INA219: da ket noi tai dia chi 0x%02X\n", ina219I2cAddress);
  return true;
}

void configurePowerStage() {
  const uint8_t relayOffLevel = RELAY_ACTIVE_LOW ? HIGH : LOW;
  // Ghi mức OFF trước khi đổi chân thành OUTPUT để hạn chế relay nháy lúc boot.
  digitalWrite(RELAY_PIN, relayOffLevel);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(POTENTIOMETER_PIN, INPUT);
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);
  analogReadResolution(12);
}

void applyPowerStage() {
  bool requestedOn = controller.relayRequested();
#if SIMULATION_MODE
  // Chế độ mô phỏng không bao giờ kích relay thật.
  requestedOn = false;
#endif
  relayOutputOn = requestedOn;
  const uint8_t relayLevel = relayOutputOn
                                 ? (RELAY_ACTIVE_LOW ? LOW : HIGH)
                                 : (RELAY_ACTIVE_LOW ? HIGH : LOW);
  digitalWrite(RELAY_PIN, relayLevel);
  digitalWrite(STATUS_LED_PIN, relayOutputOn ? HIGH : LOW);
}

void loadPersistentData() {
  preferences.begin("ev-charger", false);

  ChargerSettings settings;
  settings.activePowerW = storedFloat("pActive", 2.0f);
  settings.fixedFullPowerW = storedFloat("pFull", 0.8f);
  settings.learnedFullRatio = storedFloat("pRatio", 0.18f);
  settings.warningTemperatureC = storedFloat("tWarn", 35.0f);
  settings.cutoffTemperatureC = storedFloat("tCut", 45.0f);
  settings.recoveryTemperatureC = storedFloat("tRecover", 32.0f);
  settings.minimumChargeMs = storedUInt("minCharge", 10000UL);
  settings.fullConfirmMs = storedUInt("confirm", 60000UL);
  settings.decisionWaitMs = storedUInt("decision", 900000UL);
  settings.continuePauseMs = storedUInt("pause", 300000UL);

  // Không cho dữ liệu NVS cũ hoặc hỏng tạo cấu hình nguy hiểm.
  if (!isfinite(settings.activePowerW) ||
      !isfinite(settings.fixedFullPowerW) ||
      settings.activePowerW < 0.2f || settings.activePowerW > 100.0f ||
      settings.fixedFullPowerW < 0.05f ||
      settings.fixedFullPowerW >= settings.activePowerW) {
    settings.activePowerW = 2.0f;
    settings.fixedFullPowerW = 0.8f;
  }
  if (!isfinite(settings.learnedFullRatio) ||
      settings.learnedFullRatio < 0.05f ||
      settings.learnedFullRatio > 0.8f) {
    settings.learnedFullRatio = 0.18f;
  }
  if (!isfinite(settings.warningTemperatureC) ||
      !isfinite(settings.cutoffTemperatureC) ||
      !isfinite(settings.recoveryTemperatureC) ||
      settings.warningTemperatureC < 20.0f ||
      settings.warningTemperatureC > 90.0f ||
      settings.cutoffTemperatureC < 25.0f ||
      settings.cutoffTemperatureC > 100.0f ||
      settings.recoveryTemperatureC < 10.0f ||
      settings.recoveryTemperatureC > 80.0f ||
      settings.recoveryTemperatureC >= settings.cutoffTemperatureC ||
      settings.warningTemperatureC >= settings.cutoffTemperatureC) {
    settings.warningTemperatureC = 35.0f;
    settings.cutoffTemperatureC = 45.0f;
    settings.recoveryTemperatureC = 32.0f;
  }
  settings.minimumChargeMs = constrain(settings.minimumChargeMs, 0UL, 3600000UL);
  settings.fullConfirmMs = constrain(settings.fullConfirmMs, 3000UL, 3600000UL);
  settings.decisionWaitMs = constrain(settings.decisionWaitMs, 5000UL, 3600000UL);
  settings.continuePauseMs = constrain(settings.continuePauseMs, 5000UL, 3600000UL);
  controller.setSettings(settings);

  learnedTypicalPowerW = storedFloat("learnedP", 0.0f);
  if (!isfinite(learnedTypicalPowerW) || learnedTypicalPowerW < 0.0f) {
    learnedTypicalPowerW = 0.0f;
  }
  learningSamples = constrain(storedUChar("learnedN", 0),
                              static_cast<uint8_t>(0),
                              static_cast<uint8_t>(3));
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
  measuredPowerW = latestReading.powerW;
  latestReading.temperatureC = simulatedTemperatureC;
  latestReading.powerValid = simulatedPowerValid;
  latestReading.temperatureValid = simulatedTemperatureValid;
  potentiometerRaw = static_cast<uint16_t>(
      constrain(simulatedCurrentA / 0.48f, 0.0f, 1.0f) * 4095.0f);
#else
  potentiometerRaw = analogRead(POTENTIOMETER_PIN);
  if (inaReady && !ina219Responding()) {
    inaReady = false;
    Serial.println("INA219: mat ket noi");
  }
  if (!inaReady && millis() - lastInaRetryAtMs >= INA219_RETRY_INTERVAL_MS) {
    lastInaRetryAtMs = millis();
    initializeIna219();
  }
  latestReading.powerValid = inaReady;
  if (inaReady) {
    const float busVoltage = ina219->getBusVoltage_V();
    const float shuntVoltage = ina219->getShuntVoltage_mV() / 1000.0f;
    latestReading.voltageV = max(0.0f, busVoltage + shuntVoltage);
    latestReading.currentA = max(0.0f, ina219->getCurrent_mA() / 1000.0f);
    measuredPowerW = latestReading.voltageV * latestReading.currentA;
    // Relay chỉ bật/tắt nên biến trở không thể thay đổi công suất tải thật.
    // Dùng biến trở để tạo đường cong công suất cho thuật toán demo, trong khi
    // INA219 vẫn xác nhận tải thật có dòng điện.
    const float simulatedChargeRatio = potentiometerRaw / 4095.0f;
    latestReading.powerW = measuredPowerW * simulatedChargeRatio;
  } else {
    latestReading.voltageV = 0.0f;
    latestReading.currentA = 0.0f;
    latestReading.powerW = 0.0f;
    measuredPowerW = 0.0f;
  }

  const uint32_t age = millis() - lastRemoteTemperatureAtMs;
  if (hasRemoteTemperature) {
    latestReading.temperatureC = remoteTemperatureC;
    // Một khi nút nhiệt đã xuất hiện, mất dữ liệu vẫn được xử lý theo hướng
    // an toàn kể cả trong môi trường thử nghiệm không bắt buộc cảm biến.
    latestReading.temperatureValid = age <= TEMPERATURE_STALE_MS;
  } else {
    latestReading.temperatureC = 25.0f;
    latestReading.temperatureValid = REQUIRE_TEMPERATURE_NODE == 0;
  }
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
  json += "\"relayOn\":" + boolJson(relayOutputOn) + ",";
  json += "\"outputEnabled\":" + boolJson(controller.relayRequested()) + ",";
  json += "\"relayActiveLow\":" + boolJson(RELAY_ACTIVE_LOW) + ",";
  json += "\"potentiometer\":" + String(roundf(potentiometerRaw * 100.0f / 4095.0f), 0) + ",";
  json += "\"voltage\":" + String(latestReading.voltageV, 2) + ",";
  json += "\"current\":" + String(latestReading.currentA, 3) + ",";
  json += "\"power\":" + String(latestReading.powerW, 2) + ",";
  json += "\"measuredPower\":" + String(measuredPowerW, 2) + ",";
  json += "\"filteredPower\":" + String(controller.filteredPowerW(), 2) + ",";
  json += "\"temperature\":" + String(latestReading.temperatureC, 1) + ",";
  json += "\"humidity\":" + String(humidity, 1) + ",";
  json += "\"temperatureAgeSeconds\":" + String(temperatureAgeSeconds) + ",";
  json += "\"temperatureSource\":\"" + String(SIMULATION_MODE ? "simulation" : "dht11_remote") + "\",";
  json += "\"powerValid\":" + boolJson(latestReading.powerValid) + ",";
  json += "\"ina219Address\":" + String(ina219I2cAddress) + ",";
  json += "\"temperatureValid\":" + boolJson(latestReading.temperatureValid) + ",";
  json += "\"temperatureAvailable\":" + boolJson(
#if SIMULATION_MODE
      true
#else
      hasRemoteTemperature
#endif
      ) + ",";
  json += "\"temperatureRequired\":" + boolJson(REQUIRE_TEMPERATURE_NODE != 0) + ",";
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
  String message;
  if (!accepted) {
    if (!latestReading.powerValid) {
      message = "Khong the bat dau: INA219 chua san sang";
    } else if (!latestReading.temperatureValid) {
      message = "Khong the bat dau: chua nhan du lieu DHT11";
    } else if (controller.lastEvent() == ChargerEvent::START_BLOCKED_HOT) {
      message = "Khong the bat dau: nhiet do chua an toan";
    } else {
      message = "Lenh khong hop le voi trang thai hien tai";
    }
  }
  String response = String("{\"ok\":") + boolJson(accepted);
  if (!accepted) response += ",\"message\":\"" + message + "\"";
  response += ",\"status\":" + statusJson() + "}";
  sendJson(accepted ? 200 : 409, response);
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

  Wire.begin(21, 22);
  initializeIna219();

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
