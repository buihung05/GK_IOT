# Đấu dây mô hình VoltGuard 12 V

Mô hình gồm hai bo ESP32:

- **ESP32 chính:** INA219, biến trở, MOSFET và dashboard.
- **ESP32 nhiệt độ:** đọc module DHT11 rồi gửi số liệu qua Wi‑Fi đến ESP32 chính.

## 1. Đường tải 12 V

Sử dụng MOSFET kênh N logic-level theo kiểu đóng cắt low-side:

```text
12 V (+) ── INA219 VIN+
             INA219 VIN- ── Điện trở sứ 25 Ω / 10 W ── Drain MOSFET

12 V (-) ───────────────────────────────────────────── Source MOSFET
                                                            │
                                                        GND chung
```

Ở mức 12 V, tải 25 Ω tiêu thụ xấp xỉ:

```text
I = 12 / 25 = 0,48 A
P = 12² / 25 = 5,76 W
```

Điện trở 10 W vẫn có thể rất nóng. Không đặt nó trên breadboard, không chạm tay khi đang chạy và đặt cách xa dây nhựa, DHT11 và hai bo ESP32.

## 2. ESP32 chính

### INA219

| INA219 | ESP32 chính / nguồn |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SDA | GPIO 21 |
| SCL | GPIO 22 |
| VIN+ | 12 V (+) |
| VIN- | Đầu dương điện trở sứ 25 Ω |

Địa chỉ I²C mặc định được dùng là `0x40`.

### MOSFET

```text
GPIO 26 ── điện trở 100 Ω ── Gate MOSFET
                                  │
                            điện trở 10 kΩ
                                  │
                                 GND

Drain  ── đầu âm điện trở sứ 25 Ω
Source ── GND chung
```

Điện trở 100 Ω hạn dòng xung cho cổng MOSFET. Điện trở 10 kΩ kéo Gate xuống GND để MOSFET luôn tắt trong lúc ESP32 khởi động.

> Thứ tự chân Gate–Drain–Source không giống nhau ở mọi MOSFET. Phải kiểm tra đúng mã linh kiện và datasheet trước khi cắm. MOSFET phải có khả năng mở tốt với điện áp Gate 3,3 V; chữ “logic-level” trên tên bán hàng chưa đủ để xác nhận.

### Biến trở 10 kΩ

| Chân biến trở | Kết nối |
|---|---|
| Một chân ngoài | 3V3 |
| Chân giữa (wiper) | GPIO 34 |
| Chân ngoài còn lại | GND |

Firmware đọc biến trở và đổi thành PWM 0–100% trên GPIO 26. Xoay về mức cao để mô phỏng đang sạc; xoay giảm dần để mô phỏng pin gần đầy. Nếu chiều xoay bị ngược, đổi chỗ hai chân ngoài.

Không được cấp 5 V hoặc 12 V vào GPIO 34.

## 3. ESP32 cảm biến nhiệt và DHT11

| Module DHT11 | ESP32 nhiệt độ |
|---|---|
| VCC hoặc `+` | 3V3 |
| DATA, OUT hoặc `S` | GPIO 4 |
| GND hoặc `-` | GND |

Module DHT11 thường đã có điện trở kéo lên trên bo. Hai điện trở 4,7 kΩ trong danh sách không cần dùng. Nếu bạn thực tế có DHT11 loại cảm biến rời bốn chân, mắc một điện trở 4,7 kΩ giữa DATA và 3V3.

ESP32 nhiệt độ tự kết nối vào:

```text
Wi‑Fi: EV-Charger-Demo
Mật khẩu: 12345678
Máy nhận: http://192.168.4.1/api/temperature
```

DHT11 đo nhiệt độ không khí, không phải cảm biến tiếp xúc chính xác cho cell pin. Trong mô hình, đặt nó gần vùng cần theo dõi nhưng không ép sát điện trở sứ đang nóng. Firmware mặc định cảnh báo ở 35 °C, ngắt ở 45 °C và yêu cầu nhiệt độ giảm xuống tối đa 32 °C trước khi cho chạy lại.

## 4. Module LM2596 và nguồn chung

```text
Nguồn 12 V (+) ── LM2596 IN+
Nguồn 12 V (-) ── LM2596 IN-

LM2596 OUT+ (đã chỉnh 5,0 V) ── chân VIN/5V của hai ESP32
LM2596 OUT-                     ── GND của hai ESP32
```

Trước khi nối ESP32, cấp nguồn cho LM2596 và dùng đồng hồ đo để chỉnh đầu ra đúng **5,0 V**. Không nối 12 V trực tiếp vào chân 5V, 3V3 hoặc GPIO của ESP32.

Tất cả GND của mạch tải, LM2596, INA219 và ESP32 chính phải nối chung. Nếu ESP32 nhiệt độ được cấp từ một cổng USB riêng thì nó giao tiếp bằng Wi‑Fi nên không bắt buộc nối chung GND với mạch tải.

## 5. Vai trò các linh kiện

| Linh kiện | Vai trò |
|---|---|
| ESP32 chính | Thu thập số liệu, điều khiển MOSFET, chạy web |
| INA219 | Đo điện áp và dòng của tải 12 V |
| LM2596 | Hạ 12 V xuống 5 V cấp ESP32 |
| MOSFET logic-level | Điều chế PWM và ngắt tải |
| Điện trở 25 Ω / 10 W | Tải giả mô phỏng bộ sạc |
| Điện trở 10 kΩ | Kéo Gate MOSFET xuống GND |
| Điện trở 100 Ω | Nối tiếp giữa GPIO26 và Gate |
| Biến trở 10 kΩ | Điều chỉnh công suất mô phỏng |
| ESP32 nhiệt độ | Nút cảm biến không dây |
| Module DHT11 | Đo nhiệt độ và độ ẩm không khí |
| Hai điện trở 4,7 kΩ | Dự phòng; chỉ cần nếu DHT11 không phải module |

## 6. Trình tự lắp an toàn

1. Chưa gắn ESP32, chỉnh LM2596 từ 12 V xuống đúng 5,0 V.
2. Nạp firmware `temperature_node` cho ESP32 nhiệt độ.
3. Nạp firmware `esp32_hardware` cho ESP32 chính.
4. Chỉ nối phần điều khiển 3,3/5 V; kiểm tra dashboard nhận DHT11.
5. Ngắt nguồn, đấu INA219, điện trở tải và MOSFET.
6. Xoay biến trở về 0%, cấp nguồn và kiểm tra MOSFET không tự mở.
7. Trên dashboard nhấn **Bắt đầu phiên sạc**, sau đó tăng biến trở từ từ.
8. Theo dõi dòng điện; với tải 25 Ω và 12 V, dòng toàn tải nên quanh 0,48 A.

Nếu MOSFET nóng nhanh, số đo vượt xa 0,48 A, ESP32 reset hoặc dây có mùi nóng, phải ngắt nguồn ngay và kiểm tra lại chân MOSFET, điện áp LM2596 và điểm nối GND.
