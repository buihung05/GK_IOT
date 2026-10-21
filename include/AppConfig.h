#pragma once

#include <stdint.h>

// ESP32 chính: MOSFET điều khiển tải và biến trở mô phỏng mức sạc.
constexpr uint8_t MOSFET_GATE_PIN = 26;
constexpr uint8_t POTENTIOMETER_PIN = 34;
constexpr uint8_t MOSFET_PWM_CHANNEL = 0;
constexpr uint16_t MOSFET_PWM_FREQUENCY_HZ = 1000;
constexpr uint8_t MOSFET_PWM_RESOLUTION_BITS = 8;
constexpr uint8_t STATUS_LED_PIN = 2;

// ESP32 cảm biến nhiệt: chân DATA của module DHT11.
constexpr uint8_t DHT_DATA_PIN = 4;

constexpr char AP_SSID[] = "EV-Charger-Demo";
constexpr char AP_PASSWORD[] = "12345678";
constexpr char TEMPERATURE_NODE_TOKEN[] = "voltguard-temp-node";

constexpr uint32_t SENSOR_INTERVAL_MS = 500;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 12000;
constexpr uint32_t TEMPERATURE_STALE_MS = 10000;

