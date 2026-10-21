# Sơ đồ đấu nối hiện tại

Tài liệu đấu nối đầy đủ nằm tại [README.md](../README.md). Cấu hình firmware hiện tại sử dụng module 2 relay thay cho MOSFET.

## ESP32 chính

| Chân | Kết nối |
|---|---|
| GPIO21 | INA219 SDA |
| GPIO22 | INA219 SCL |
| GPIO34 | Chân giữa biến trở 10 kΩ |
| GPIO27 | Relay IN1 |
| GPIO2 | LED trạng thái tích hợp |

## Đường tải DC 12 V

```text
12 V+ → INA219 VIN+ → INA219 VIN- → Relay COM1
Relay NO1 → Điện trở sứ 25 Ω/10 W → 12 V-
```

Chỉ dùng `COM1–NO1`; để `NC1` và toàn bộ kênh relay 2 trống. Firmware mặc định coi module relay là loại kích mức thấp. Nếu module kích mức cao, đổi `RELAY_ACTIVE_LOW` trong `include/AppConfig.h` thành `false` rồi nạp lại.

> Chỉ sử dụng nguồn DC 12 V cho mô hình. Không đấu điện lưới AC 220 V.
