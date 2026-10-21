# VoltGuard – hướng dẫn đấu nối mô hình sạc thông minh 12 V

Phần cứng hiện tại gồm ESP32 chính, INA219, LM2596, biến trở 10 kΩ, module 2 relay, điện trở sứ 25 Ω/10 W và nguồn DC 12 V. ESP32 thứ hai cùng DHT11 là nút đo nhiệt độ không dây.

> Đây chỉ là mô hình học tập DC 12 V. Tuyệt đối không nối mạch với điện lưới 220 V, dù trên relay có ghi 250 VAC.

## 1. Kiểm tra relay

Các dòng `10A 250VAC`, `10A 125VAC`, `10A 30VDC` là định mức tiếp điểm, không phải điện áp cuộn relay. Kiểm tra chữ trên thân relay:

- `SRD-05VDC-SL-C`: module dùng nguồn 5 V.
- `SRD-12VDC-SL-C`: module dùng nguồn 12 V.

Sơ đồ dưới đây giả định module 2 relay loại **5 V**, có `VCC`, `GND`, `IN1`, `IN2` và `NC1`, `COM1`, `NO1`. Nếu module có jumper `JD-VCC–VCC`, giữ nguyên jumper cho bản demo.

## 2. Sơ đồ tổng thể

```text
Nguồn 12 V ── LM2596 5 V ── ESP32 chính
                   │            ├─ GPIO21/22 ← INA219
                   └─ relay VCC ├─ GPIO34 ← biến trở
                                └─ GPIO27 → relay IN1

Đường tải:
12 V+ → INA219 → COM1/NO1 relay → điện trở 25 Ω/10 W → 12 V-

Nút nhiệt độ:
ESP32 thứ hai → DHT11 GPIO4 → Wi-Fi → ESP32 chính
```

## 3. Bảng chân ESP32 chính

| Chân ESP32 | Kết nối |
|---|---|
| `VIN/5V` | LM2596 `OUT+` đã chỉnh đúng 5,0 V |
| `GND` | GND chung của LM2596, INA219, relay và biến trở |
| `3V3` | INA219 `VCC` và một chân ngoài biến trở |
| `GPIO21` | INA219 `SDA` |
| `GPIO22` | INA219 `SCL` |
| `GPIO34` | Chân giữa biến trở 10 kΩ |
| `GPIO27` | Module relay `IN1` |
| `GPIO2` | LED tích hợp báo trạng thái |
| `GPIO26` | Không nối trong cấu hình relay |

> Không nối relay vào GPIO26. Firmware hiện tại điều khiển relay bằng tín hiệu số trên GPIO27.

## 4. Nguồn và LM2596

```text
Nguồn 12 V (+) → LM2596 IN+
Nguồn 12 V (-) → LM2596 IN-

LM2596 OUT+ 5,0 V → ESP32 VIN/5V
                    └→ Relay VCC (chỉ relay 5 V)
LM2596 OUT-         → ESP32 GND
                    └→ Relay GND
```

Trước khi nối ESP32 hoặc relay, dùng đồng hồ chỉnh LM2596 xuống khoảng **5,0 V**. Không cấp 12 V trực tiếp vào `VIN/5V`, `3V3` hoặc GPIO ESP32. Nếu chưa có đồng hồ, hãy cấp ESP32 bằng USB và chưa nối LM2596 với ESP32.

## 5. INA219

| INA219 | Kết nối |
|---|---|
| `VCC` | ESP32 `3V3` |
| `GND` | ESP32 `GND` |
| `SDA` | ESP32 `GPIO21` |
| `SCL` | ESP32 `GPIO22` |
| `VIN+` | Nguồn 12 V (+) |
| `VIN-` | Relay `COM1` |

INA219 được mắc nối tiếp ở phía dương để đo toàn bộ dòng qua tải. Firmware tự dò dải địa chỉ INA219 từ `0x40` đến `0x4F`; module chưa thay đổi jumper thường dùng `0x40`.

## 6. Module 2 relay

Chỉ dùng kênh 1; để `IN2` và các tiếp điểm kênh 2 trống.

### Phần điều khiển, module relay 5 V

```text
Relay VCC → LM2596 OUT+ 5,0 V
Relay GND → GND chung
Relay IN1 → ESP32 GPIO27
Relay IN2 → không nối
```

### Phần tiếp điểm tải 12 V

```text
Nguồn 12 V (+)
      │
 INA219 VIN+
 INA219 VIN-
      │
 Relay COM1
 Relay NO1
      │
 Điện trở sứ 25 Ω/10 W
      │
Nguồn 12 V (-)
```

Dùng `COM1–NO1` để tải mặc định tắt khi relay chưa kích. Không dùng `NC1`.

Nhiều module relay kích mức thấp: `IN1 = LOW` là bật và `IN1 = HIGH` là tắt. Một số module hoạt động ngược lại; thử khi chưa nối tải để xác định. Nếu tín hiệu 3,3 V của ESP32 không kích ổn định module, không đưa 5 V vào GPIO; cần module tương thích 3,3 V hoặc mạch chuyển mức.

## 7. Biến trở 10 kΩ

```text
Chân ngoài thứ nhất → ESP32 3V3
Chân giữa           → ESP32 GPIO34
Chân ngoài còn lại  → ESP32 GND
```

Biến trở chỉ tạo tín hiệu mô phỏng cho thuật toán, không mắc nối tiếp với tải sứ. Nếu hướng xoay ngược mong muốn, đổi chỗ hai chân ngoài.

## 8. Điện trở tải

Với nguồn 12 V và tải 25 Ω:

```text
Dòng:      I = 12 / 25 ≈ 0,48 A
Công suất: P = 12² / 25 ≈ 5,76 W
```

Điện trở sẽ rất nóng:

- Không cắm điện trở công suất lên breadboard.
- Đặt trên bề mặt chịu nhiệt, thoáng khí và tránh xa dây nhựa.
- Không đặt DHT11 sát điện trở nếu muốn đo nhiệt độ môi trường.
- Không chạm tay khi đang chạy hoặc ngay sau khi ngắt.
- Ngắt nguồn nếu dây, đầu nối hoặc relay nóng bất thường.

## 9. ESP32 nhiệt độ và DHT11

| DHT11 | ESP32 nhiệt độ |
|---|---|
| `VCC` hoặc `+` | `3V3` |
| `DATA`, `OUT` hoặc `S` | `GPIO4` |
| `GND` hoặc `-` | `GND` |

Module DHT11 thường đã có điện trở kéo lên. Nếu là cảm biến rời, mắc 4,7 kΩ giữa `DATA` và `3V3`.

```text
Wi-Fi: EV-Charger-Demo
Mật khẩu: 12345678
Máy nhận: http://192.168.4.1/api/temperature
```

## 10. Trình tự lắp an toàn

1. Chưa nối ESP32 và relay, chỉnh LM2596 xuống đúng 5,0 V.
2. Nạp chương trình cho hai ESP32 bằng USB.
3. Ngắt toàn bộ nguồn trước khi đấu dây.
4. Nối INA219, biến trở và các chân điều khiển relay.
5. Chưa nối tải, bật thử để xác định relay kênh 1 kích LOW hay HIGH.
6. Ngắt nguồn và đấu tải qua `VIN+ → VIN- → COM1 → NO1 → 25 Ω → 12 V-`.
7. Kiểm tra không có dây chạm nhau rồi mới cấp 12 V.
8. Lần đầu chỉ chạy tải trong thời gian ngắn và theo dõi nhiệt độ.

## 11. Trạng thái firmware

Môi trường `esp32_hardware` điều khiển module relay kênh 1 bằng GPIO27. Mặc định firmware dùng relay kích mức thấp (`LOW` là bật), phù hợp với phần lớn module 2 relay 5 V. Nếu module kích mức cao, đổi `RELAY_ACTIVE_LOW` thành `false` trong `include/AppConfig.h` rồi nạp lại.

Relay sẽ ngắt khi người dùng yêu cầu, thuật toán xác nhận đầy hoặc phát hiện quá nhiệt. Chức năng tự ngắt vẫn chạy khi điện thoại mất Wi-Fi. Không chạy relay bằng PWM.

## 12. Nạp chương trình

ESP32 chính:

```cmd
"C:\Users\Admin\.platformio\penv\Scripts\platformio.exe" run -e esp32_hardware -t upload --upload-port COM4
```

Chỉ thử ESP32 chính khi chưa có ESP32+DHT11:

```cmd
"C:\Users\Admin\.platformio\penv\Scripts\platformio.exe" run -e esp32_hardware_no_temp -t upload --upload-port COM4
```

Môi trường `esp32_hardware_no_temp` vẫn chạy INA219, biến trở, relay, thuật toán đầy và web nhưng tạm vô hiệu hóa bảo vệ quá nhiệt. Chỉ dùng để thử trên bàn; khi có lại nút DHT11, nạp lại `esp32_hardware`.

ESP32 nhiệt độ:

```cmd
"C:\Users\Admin\.platformio\penv\Scripts\platformio.exe" run -e temperature_node -t upload --upload-port COM5
```

Serial Monitor:

```cmd
"C:\Users\Admin\.platformio\penv\Scripts\platformio.exe" device monitor -p COM4 -b 115200
```

Thay cổng COM theo thực tế. Đóng Serial Monitor bằng `Ctrl+C` trước khi nạp để tránh lỗi cổng đang bận.

## 13. Cảnh báo

- Chỉ dùng DC 12 V trong mô hình.
- Không đo AC 220 V bằng INA219.
- Không nối tiếp điểm `COM/NO/NC` với GPIO ESP32.
- DHT11 và thuật toán demo không thay thế BMS.
- Xác nhận relay là loại 5 V hay 12 V trước khi cấp nguồn.
