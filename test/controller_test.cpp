#include <cassert>
#include <iostream>

#include "ChargerController.h"

SensorReading reading(float power, float temperature = 30.0f,
                      bool powerValid = true, bool temperatureValid = true) {
  SensorReading value;
  value.voltageV = 12.0f;
  value.currentA = power / 12.0f;
  value.powerW = power;
  value.temperatureC = temperature;
  value.powerValid = powerValid;
  value.temperatureValid = temperatureValid;
  return value;
}

ChargerSettings fastSettings() {
  ChargerSettings settings;
  settings.activePowerW = 6.0f;
  settings.fixedFullPowerW = 2.0f;
  settings.minimumChargeMs = 0;
  settings.fullConfirmMs = 500;
  settings.decisionWaitMs = 1000;
  settings.continuePauseMs = 1000;
  settings.sensorFaultDelayMs = 500;
  return settings;
}

void driveToWaiting(ChargerController& controller, uint32_t& now) {
  controller.update(reading(8.0f), now);
  for (int i = 0; i < 12; ++i) {
    now += 100;
    controller.update(reading(0.2f), now);
    if (controller.state() == ChargerState::VERIFYING_FULL) break;
  }
  assert(controller.state() == ChargerState::VERIFYING_FULL);
  now += 600;
  controller.update(reading(0.2f), now);
  assert(controller.state() == ChargerState::WAITING_DECISION);
}

void testFullCycle() {
  ChargerController controller(fastSettings());
  uint32_t now = 0;
  controller.update(reading(8.0f), now);
  assert(controller.start(now));
  driveToWaiting(controller, now);
  now += 1100;
  controller.update(reading(0.2f), now);
  assert(controller.state() == ChargerState::STOPPED_FULL);
  assert(!controller.relayRequested());
  assert(controller.lastEvent() == ChargerEvent::FULL_CONFIRMED);
  const uint32_t stoppedDuration = controller.sessionElapsedMs(now);
  assert(controller.sessionElapsedMs(now + 5000) == stoppedDuration);
}

void testContinueCancelsCountdown() {
  ChargerController controller(fastSettings());
  uint32_t now = 0;
  controller.update(reading(8.0f), now);
  assert(controller.start(now));
  driveToWaiting(controller, now);
  assert(controller.continueCharging(now));
  assert(controller.state() == ChargerState::CHARGING);
  assert(controller.relayRequested());
}

void testOverheatWinsImmediately() {
  ChargerController controller(fastSettings());
  controller.update(reading(8.0f), 0);
  assert(controller.start(0));
  controller.update(reading(8.0f, 60.0f), 100);
  assert(controller.state() == ChargerState::STOPPED_OVERHEAT);
  assert(!controller.relayRequested());
}

void testSensorFailureIsFailSafe() {
  ChargerController controller(fastSettings());
  controller.update(reading(8.0f), 0);
  assert(controller.start(0));
  controller.update(reading(8.0f, 30.0f, true, false), 100);
  assert(controller.relayRequested());
  controller.update(reading(8.0f, 30.0f, true, false), 700);
  assert(controller.state() == ChargerState::STOPPED_SENSOR_FAULT);
  assert(!controller.relayRequested());
}

void testHotRestartIsBlocked() {
  ChargerController controller(fastSettings());
  controller.update(reading(8.0f, 50.0f), 0);
  assert(!controller.start(0));
  assert(controller.lastEvent() == ChargerEvent::START_BLOCKED_HOT);
}

void testWarmAmbientCanStart() {
  ChargerController controller(fastSettings());
  controller.update(reading(8.0f, 34.0f), 0);
  assert(controller.start(0));
  assert(controller.relayRequested());
}

void testOverheatRequiresRecoveryBeforeRestart() {
  ChargerController controller(fastSettings());
  controller.update(reading(8.0f, 30.0f), 0);
  assert(controller.start(0));
  controller.update(reading(8.0f, 50.0f), 100);
  assert(controller.state() == ChargerState::STOPPED_OVERHEAT);

  controller.update(reading(8.0f, 34.0f), 200);
  assert(!controller.start(200));

  controller.update(reading(8.0f, 30.0f), 300);
  assert(controller.start(300));
}

int main() {
  testFullCycle();
  testContinueCancelsCountdown();
  testOverheatWinsImmediately();
  testSensorFailureIsFailSafe();
  testHotRestartIsBlocked();
  testWarmAmbientCanStart();
  testOverheatRequiresRecoveryBeforeRestart();
  std::cout << "7 controller tests passed.\n";
}

