#pragma once

#include <stdint.h>

enum class ChargerState : uint8_t {
  IDLE,
  CHARGING,
  VERIFYING_FULL,
  WAITING_DECISION,
  STOPPED_FULL,
  STOPPED_OVERHEAT,
  STOPPED_SENSOR_FAULT,
  STOPPED_MANUAL
};

enum class ChargerEvent : uint8_t {
  NONE,
  SESSION_STARTED,
  FULL_SUSPECTED,
  FULL_CONFIRMED,
  CONTINUE_SELECTED,
  MANUAL_STOPPED,
  OVERHEAT_STOPPED,
  SENSOR_FAULT_STOPPED,
  START_BLOCKED_HOT,
  START_BLOCKED_SENSOR
};

struct ChargerSettings {
  float activePowerW = 2.0f;
  float fixedFullPowerW = 0.8f;
  float learnedFullRatio = 0.18f;
  float powerHysteresisW = 0.5f;
  float warningTemperatureC = 35.0f;
  float cutoffTemperatureC = 45.0f;
  float recoveryTemperatureC = 32.0f;
  uint32_t minimumChargeMs = 10000;
  uint32_t fullConfirmMs = 60000;
  uint32_t decisionWaitMs = 900000;
  uint32_t continuePauseMs = 300000;
  uint32_t sensorFaultDelayMs = 5000;
};

struct SensorReading {
  float voltageV = 0.0f;
  float currentA = 0.0f;
  float powerW = 0.0f;
  float temperatureC = 0.0f;
  bool powerValid = false;
  bool temperatureValid = false;
};

class ChargerController {
 public:
  explicit ChargerController(const ChargerSettings& settings = ChargerSettings{});

  void setSettings(const ChargerSettings& settings);
  const ChargerSettings& settings() const;
  void setLearningProfile(float typicalChargePowerW, uint8_t samples);

  void update(const SensorReading& reading, uint32_t nowMs);
  bool start(uint32_t nowMs);
  void stopManually(uint32_t nowMs);
  bool continueCharging(uint32_t nowMs);

  ChargerState state() const;
  ChargerEvent lastEvent() const;
  uint32_t eventId() const;
  bool relayRequested() const;
  bool temperatureWarning() const;
  bool hasSeenActiveCharge() const;
  float filteredPowerW() const;
  float effectiveFullThresholdW() const;
  uint32_t sessionElapsedMs(uint32_t nowMs) const;
  uint32_t decisionRemainingMs(uint32_t nowMs) const;
  float sessionAverageActivePowerW() const;
  uint32_t activeSampleCount() const;

  static const char* stateName(ChargerState state);
  static const char* eventName(ChargerEvent event);

 private:
  void stopWith(ChargerState state, ChargerEvent event, uint32_t nowMs);
  void publishEvent(ChargerEvent event);
  void resetSession(uint32_t nowMs);
  bool isLowPower() const;

  ChargerSettings settings_;
  SensorReading reading_;
  ChargerState state_ = ChargerState::IDLE;
  ChargerEvent lastEvent_ = ChargerEvent::NONE;
  uint32_t eventId_ = 0;
  bool relayRequested_ = false;
  bool hasSeenActiveCharge_ = false;
  bool filterInitialized_ = false;
  bool invalidTemperatureTiming_ = false;
  float filteredPowerW_ = 0.0f;
  float learnedTypicalPowerW_ = 0.0f;
  uint8_t learningSamples_ = 0;
  double activePowerSum_ = 0.0;
  uint32_t activePowerSamples_ = 0;
  uint32_t sessionStartedAtMs_ = 0;
  uint32_t sessionStoppedAtMs_ = 0;
  uint32_t lowPowerStartedAtMs_ = 0;
  uint32_t decisionStartedAtMs_ = 0;
  uint32_t detectionMutedUntilMs_ = 0;
  uint32_t invalidTemperatureSinceMs_ = 0;
};

