#pragma once

#include <stdint.h>

// ESP32 chính: module relay 2 kênh, dự án chỉ sử dụng kênh IN1.
constexpr uint8_t RELAY_PIN = 27;
// Phần lớn module relay 5 V phổ biến kích ở mức LOW. Đổi thành false nếu
// module của bạn kích ở mức HIGH.
constexpr bool RELAY_ACTIVE_LOW = true;
constexpr uint8_t POTENTIOMETER_PIN = 34;
constexpr uint8_t STATUS_LED_PIN = 2;

// ESP32 cảm biến nhiệt: chân DATA của module DHT11.
constexpr uint8_t DHT_DATA_PIN = 4;

constexpr char AP_SSID[] = "EV-Charger-Demo";
constexpr char AP_PASSWORD[] = "12345678";
constexpr char TEMPERATURE_NODE_TOKEN[] = "voltguard-temp-node";

constexpr uint32_t SENSOR_INTERVAL_MS = 500;
constexpr uint32_t INA219_RETRY_INTERVAL_MS = 2000;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 12000;
constexpr uint32_t TEMPERATURE_STALE_MS = 10000;

