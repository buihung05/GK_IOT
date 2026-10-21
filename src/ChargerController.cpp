#include "ChargerController.h"

#include <algorithm>

namespace {
bool elapsed(uint32_t now, uint32_t since, uint32_t duration) {
  return static_cast<uint32_t>(now - since) >= duration;
}

bool deadlineReached(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}
}  // namespace

ChargerController::ChargerController(const ChargerSettings& settings)
    : settings_(settings) {}

void ChargerController::setSettings(const ChargerSettings& settings) {
  settings_ = settings;
}

const ChargerSettings& ChargerController::settings() const { return settings_; }

void ChargerController::setLearningProfile(float typicalChargePowerW,
                                           uint8_t samples) {
  learnedTypicalPowerW_ = std::max(0.0f, typicalChargePowerW);
  learningSamples_ = samples;
}

void ChargerController::resetSession(uint32_t nowMs) {
  sessionStartedAtMs_ = nowMs;
  sessionStoppedAtMs_ = 0;
  lowPowerStartedAtMs_ = 0;
  decisionStartedAtMs_ = 0;
  detectionMutedUntilMs_ = 0;
  invalidTemperatureTiming_ = false;
  hasSeenActiveCharge_ = false;
  filterInitialized_ = false;
  filteredPowerW_ = 0.0f;
  activePowerSum_ = 0.0;
  activePowerSamples_ = 0;
}

bool ChargerController::start(uint32_t nowMs) {
  if (!reading_.temperatureValid || !reading_.powerValid) {
    publishEvent(ChargerEvent::START_BLOCKED_SENSOR);
    return false;
  }
  const bool recoveringFromOverheat = state_ == ChargerState::STOPPED_OVERHEAT;
  const bool tooHotToStart =
      reading_.temperatureC >= settings_.cutoffTemperatureC ||
      (recoveringFromOverheat &&
       reading_.temperatureC > settings_.recoveryTemperatureC);
  if (tooHotToStart) {
    publishEvent(ChargerEvent::START_BLOCKED_HOT);
    return false;
  }

  resetSession(nowMs);
  relayRequested_ = true;
  state_ = ChargerState::CHARGING;
  publishEvent(ChargerEvent::SESSION_STARTED);
  return true;
}

void ChargerController::stopManually(uint32_t nowMs) {
  if (relayRequested_) {
    stopWith(ChargerState::STOPPED_MANUAL, ChargerEvent::MANUAL_STOPPED, nowMs);
  }
}

bool ChargerController::continueCharging(uint32_t nowMs) {
  if (!relayRequested_ || (state_ != ChargerState::WAITING_DECISION &&
                           state_ != ChargerState::VERIFYING_FULL)) {
    return false;
  }
  state_ = ChargerState::CHARGING;
  lowPowerStartedAtMs_ = 0;
  decisionStartedAtMs_ = 0;
  detectionMutedUntilMs_ = nowMs + settings_.continuePauseMs;
  publishEvent(ChargerEvent::CONTINUE_SELECTED);
  return true;
}

void ChargerController::update(const SensorReading& reading, uint32_t nowMs) {
  reading_ = reading;

  if (!relayRequested_) {
    return;
  }

  if (!reading.temperatureValid || !reading.powerValid) {
    if (!invalidTemperatureTiming_) {
      invalidTemperatureTiming_ = true;
      invalidTemperatureSinceMs_ = nowMs;
    } else if (elapsed(nowMs, invalidTemperatureSinceMs_,
                       settings_.sensorFaultDelayMs)) {
      stopWith(ChargerState::STOPPED_SENSOR_FAULT,
               ChargerEvent::SENSOR_FAULT_STOPPED, nowMs);
    }
    return;
  }
  invalidTemperatureTiming_ = false;

  if (reading.temperatureC >= settings_.cutoffTemperatureC) {
    stopWith(ChargerState::STOPPED_OVERHEAT,
             ChargerEvent::OVERHEAT_STOPPED, nowMs);
    return;
  }

  const float safePower = std::max(0.0f, reading.powerW);
  if (!filterInitialized_) {
    filteredPowerW_ = safePower;
    filterInitialized_ = true;
  } else {
    // EMA giúp loại các xung nhiễu ngắn nhưng vẫn phản ứng đủ nhanh cho demo.
    filteredPowerW_ = filteredPowerW_ * 0.8f + safePower * 0.2f;
  }

  if (filteredPowerW_ >= settings_.activePowerW) {
    hasSeenActiveCharge_ = true;
    activePowerSum_ += filteredPowerW_;
    ++activePowerSamples_;
  }

  if (!hasSeenActiveCharge_ ||
      !elapsed(nowMs, sessionStartedAtMs_, settings_.minimumChargeMs)) {
    return;
  }

  if (!deadlineReached(nowMs, detectionMutedUntilMs_)) {
    return;
  }

  const bool low = isLowPower();
  const bool clearlyActive =
      filteredPowerW_ >= effectiveFullThresholdW() + settings_.powerHysteresisW;

  switch (state_) {
    case ChargerState::CHARGING:
      if (low) {
        state_ = ChargerState::VERIFYING_FULL;
        lowPowerStartedAtMs_ = nowMs;
      }
      break;

    case ChargerState::VERIFYING_FULL:
      if (clearlyActive) {
        state_ = ChargerState::CHARGING;
        lowPowerStartedAtMs_ = 0;
      } else if (low && elapsed(nowMs, lowPowerStartedAtMs_,
                                settings_.fullConfirmMs)) {
        state_ = ChargerState::WAITING_DECISION;
        decisionStartedAtMs_ = nowMs;
        publishEvent(ChargerEvent::FULL_SUSPECTED);
      }
      break;

    case ChargerState::WAITING_DECISION:
      if (clearlyActive) {
        state_ = ChargerState::CHARGING;
        decisionStartedAtMs_ = 0;
      } else if (low && elapsed(nowMs, decisionStartedAtMs_,
                                settings_.decisionWaitMs)) {
        stopWith(ChargerState::STOPPED_FULL, ChargerEvent::FULL_CONFIRMED,
                 nowMs);
      }
      break;

    default:
      break;
  }
}

void ChargerController::stopWith(ChargerState state, ChargerEvent event,
                                 uint32_t nowMs) {
  relayRequested_ = false;
  sessionStoppedAtMs_ = nowMs;
  state_ = state;
  publishEvent(event);
}

void ChargerController::publishEvent(ChargerEvent event) {
  lastEvent_ = event;
  ++eventId_;
}

bool ChargerController::isLowPower() const {
  return filteredPowerW_ <= effectiveFullThresholdW();
}

ChargerState ChargerController::state() const { return state_; }
ChargerEvent ChargerController::lastEvent() const { return lastEvent_; }
uint32_t ChargerController::eventId() const { return eventId_; }
bool ChargerController::relayRequested() const { return relayRequested_; }

bool ChargerController::temperatureWarning() const {
  return reading_.temperatureValid &&
         reading_.temperatureC >= settings_.warningTemperatureC;
}

bool ChargerController::hasSeenActiveCharge() const {
  return hasSeenActiveCharge_;
}

float ChargerController::filteredPowerW() const { return filteredPowerW_; }

float ChargerController::effectiveFullThresholdW() const {
  const float learned = learnedTypicalPowerW_ * settings_.learnedFullRatio;
  const float candidate = std::max(settings_.fixedFullPowerW, learned);
  const float maximumSafeThreshold = std::max(
      0.05f, settings_.activePowerW - std::max(0.1f, settings_.powerHysteresisW));
  return std::min(candidate, maximumSafeThreshold);
}

uint32_t ChargerController::sessionElapsedMs(uint32_t nowMs) const {
  if (state_ == ChargerState::IDLE) return 0;
  if (!relayRequested_) {
    return static_cast<uint32_t>(sessionStoppedAtMs_ - sessionStartedAtMs_);
  }
  return static_cast<uint32_t>(nowMs - sessionStartedAtMs_);
}

uint32_t ChargerController::decisionRemainingMs(uint32_t nowMs) const {
  if (state_ != ChargerState::WAITING_DECISION) return 0;
  const uint32_t used = static_cast<uint32_t>(nowMs - decisionStartedAtMs_);
  return used >= settings_.decisionWaitMs ? 0 : settings_.decisionWaitMs - used;
}

float ChargerController::sessionAverageActivePowerW() const {
  return activePowerSamples_ == 0
             ? 0.0f
             : static_cast<float>(activePowerSum_ / activePowerSamples_);
}

uint32_t ChargerController::activeSampleCount() const {
  return activePowerSamples_;
}

const char* ChargerController::stateName(ChargerState state) {
  switch (state) {
    case ChargerState::IDLE: return "idle";
    case ChargerState::CHARGING: return "charging";
    case ChargerState::VERIFYING_FULL: return "verifying_full";
    case ChargerState::WAITING_DECISION: return "waiting_decision";
    case ChargerState::STOPPED_FULL: return "stopped_full";
    case ChargerState::STOPPED_OVERHEAT: return "stopped_overheat";
    case ChargerState::STOPPED_SENSOR_FAULT: return "stopped_sensor_fault";
    case ChargerState::STOPPED_MANUAL: return "stopped_manual";
  }
  return "unknown";
}

const char* ChargerController::eventName(ChargerEvent event) {
  switch (event) {
    case ChargerEvent::NONE: return "none";
    case ChargerEvent::SESSION_STARTED: return "session_started";
    case ChargerEvent::FULL_SUSPECTED: return "full_suspected";
    case ChargerEvent::FULL_CONFIRMED: return "full_confirmed";
    case ChargerEvent::CONTINUE_SELECTED: return "continue_selected";
    case ChargerEvent::MANUAL_STOPPED: return "manual_stopped";
    case ChargerEvent::OVERHEAT_STOPPED: return "overheat_stopped";
    case ChargerEvent::SENSOR_FAULT_STOPPED: return "sensor_fault_stopped";
    case ChargerEvent::START_BLOCKED_HOT: return "start_blocked_hot";
    case ChargerEvent::START_BLOCKED_SENSOR: return "start_blocked_sensor";
  }
  return "unknown";
}
